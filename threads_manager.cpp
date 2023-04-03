#include "main.h"

using namespace std;

static mutex mtx_conn;
static condition_variable cond_close_conn;
static int num_conn = 0, all_req = 0;
//======================================================================
RequestManager::RequestManager(unsigned int n)
{
    list_start = list_end = NULL;
    size_list = stop_manager = all_thr = 0;
    count_thr = num_wait_thr = 0;
    NumProc = n;
}
//----------------------------------------------------------------------
RequestManager::~RequestManager() {}
//----------------------------------------------------------------------
int RequestManager::get_num_chld()
{
    return NumProc;
}
//----------------------------------------------------------------------
int RequestManager::get_num_thr()
{
lock_guard<std::mutex> lg(mtx_thr);
    return count_thr;
}
//----------------------------------------------------------------------
int RequestManager::get_all_thr()
{
    return all_thr;
}
//----------------------------------------------------------------------
int RequestManager::start_thr()
{
mtx_thr.lock();
    int ret = ++count_thr;
    ++all_thr;
mtx_thr.unlock();
    return ret;
}
//----------------------------------------------------------------------
void RequestManager::wait_exit_thr(unsigned int n)
{
    unique_lock<mutex> lk(mtx_thr);
    while (n == count_thr)
    {
        cond_exit_thr.wait(lk);
    }
}
//----------------------------------------------------------------------
void push_resp_list(Connect *req, RequestManager *ReqMan)
{
ReqMan->mtx_thr.lock();
    req->next = NULL;
    req->prev = ReqMan->list_end;
    if (ReqMan->list_start)
    {
        ReqMan->list_end->next = req;
        ReqMan->list_end = req;
    }
    else
        ReqMan->list_start = ReqMan->list_end = req;

    ++ReqMan->size_list;
    ++all_req;
ReqMan->mtx_thr.unlock();
    ReqMan->cond_list.notify_one();
}
//----------------------------------------------------------------------
Connect *RequestManager::pop_resp_list()
{
unique_lock<mutex> lk(mtx_thr);
    ++num_wait_thr;
    while (list_start == NULL)
    {
        cond_list.wait(lk);
        if (stop_manager)
            return NULL;
    }
    --num_wait_thr;
    Connect *req = list_start;
    if (list_start->next)
    {
        list_start->next->prev = NULL;
        list_start = list_start->next;
    }
    else
        list_start = list_end = NULL;

    --size_list;
    if (num_wait_thr <= 1)
        cond_new_thr.notify_one();

    return req;
}
//----------------------------------------------------------------------
int RequestManager::wait_create_thr(int *n)
{
unique_lock<mutex> lk(mtx_thr);
    while (((size_list <= num_wait_thr) || (count_thr >= conf->MaxThreads)) && !stop_manager)
    {
        cond_new_thr.wait(lk);
    }

    *n = count_thr;
    return stop_manager;
}
//----------------------------------------------------------------------
int RequestManager::end_thr(int ret)
{
mtx_thr.lock();
    if (((count_thr > conf->MinThreads) && (size_list < num_wait_thr)) || ret)
    {
        --count_thr;
        ret = EXIT_THR;
    }
mtx_thr.unlock();
    if (ret)
    {
        cond_exit_thr.notify_all();
    }
    return ret;
}
//----------------------------------------------------------------------
void RequestManager::close_manager()
{
    stop_manager = 1;
    cond_new_thr.notify_one();
    cond_exit_thr.notify_one();
    cond_list.notify_all();
}
//======================================================================
void start_conn()
{
mtx_conn.lock();
    ++num_conn;
mtx_conn.unlock();
}
//======================================================================
int is_maxconn()
{
mtx_conn.lock();
    int n = 0;
    if (num_conn >= conf->MaxWorkConnections)
        n = 1;
mtx_conn.unlock();
    return n;
}
//======================================================================
void wait_close_all_conn()
{
unique_lock<mutex> lk(mtx_conn);
    while (num_conn > 0)
    {
        cond_close_conn.wait(lk);
    }
}
//======================================================================
void close_connect(Connect *req)
{
    shutdown(req->clientSocket, SHUT_RDWR);
    int err = close(req->clientSocket);
    if (err)
        print_err(req, "<%s:%d> Error close(): %s\n", __func__, __LINE__, strerror(errno));
    delete req;

mtx_conn.lock();
    --num_conn;
mtx_conn.unlock();
    cond_close_conn.notify_all();
}
//======================================================================
void end_response(Connect *req)
{
    if (req->connKeepAlive == 0 || req->err < 0)
    { // ----- Close connect -----
        if (req->err <= -RS101) // err < -100
        {
            req->respStatus = -req->err;
            req->err = -1;
            req->hdrs = "";
            if (send_message(req, NULL) == 1)
                return;
        }

        if (req->operation != READ_REQUEST)
        {
            print_log(req);
        }

        close_connect(req);
    }
    else
    { // ----- KeepAlive -----
    #ifdef TCP_CORK_
        if (conf->TcpCork == 'y')
        {
        #if defined(LINUX_)
            int optval = 0;
            setsockopt(req->clientSocket, SOL_TCP, TCP_CORK, &optval, sizeof(optval));
        #elif defined(FREEBSD_)
            int optval = 0;
            setsockopt(req->clientSocket, IPPROTO_TCP, TCP_NOPUSH, &optval, sizeof(optval));
        #endif
        }
    #endif
        print_log(req);
        req->init();
        req->timeout = conf->TimeoutKeepAlive;
        ++req->numReq;
        push_get_request(req);
    }
}
//======================================================================
void thr_create_manager(int numProc, RequestManager *ReqMan)
{
    int num_thr;
    thread thr;

    while (1)
    {
        if (ReqMan->wait_create_thr(&num_thr))
            break;
        try
        {
            thr = thread(response1, ReqMan);
        }
        catch (...)
        {
            print_err("[%d] <%s:%d> Error create thread: num_thr=%d, errno=%d\n", numProc, __func__, __LINE__, num_thr, errno);
            ReqMan->wait_exit_thr(num_thr);
            continue;
        }

        thr.detach();

        ReqMan->start_thr();
    }
}
//======================================================================
static unsigned int nProc;
static RequestManager *RM;
static unsigned long allConn = 0;
//======================================================================
static void signal_handler_child(int sig)
{
    if (sig == SIGINT)
    {
        print_err("[%d] <%s:%d> ### SIGINT ### all_conn=%lu, open_conn=%d, all_req=%d\n", nProc, __func__, __LINE__, allConn, num_conn, all_req);
    }
    else if (sig == SIGTERM)
    {
        print_err("<main> ####### SIGTERM #######\n");
        exit(0);
    }
    else if (sig == SIGSEGV)
    {
        print_err("[%d] <%s:%d> ### SIGSEGV ###\n", nProc, __func__, __LINE__);
        exit(1);
    }
    else if (sig == SIGUSR1)
        print_err("[%d] <%s:%d> ### SIGUSR1 ###\n", nProc, __func__, __LINE__);
    else if (sig == SIGUSR2)
        print_err("[%d] <%s:%d> ### SIGUSR2 ###\n", nProc, __func__, __LINE__);
    else
        print_err("[%d] <%s:%d> sig=%d\n", nProc, __func__, __LINE__, sig);
}
//======================================================================
Connect *create_req();
int write_(int fd, void *data, int size);
int read_(int fd, void *data, int size);
//======================================================================
void manager(int sockServer, unsigned int numProc, int fd_in, int fd_out, char sig)
{
    RequestManager *ReqMan = new(nothrow) RequestManager(numProc);
    if (!ReqMan)
    {
        print_err("<%s:%d> *********** Exit child %u ***********\n", __func__, __LINE__, numProc);
        close_logs();
        exit(1);
    }

    nProc = numProc;
    RM = ReqMan;
    //------------------------------------------------------------------
    if (signal(SIGINT, signal_handler_child) == SIG_ERR)
    {
        print_err("[%d] <%s:%d> Error signal(SIGINT): %s\n", numProc, __func__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (signal(SIGSEGV, signal_handler_child) == SIG_ERR)
    {
        print_err("[%d] <%s:%d> Error signal(SIGSEGV): %s\n", numProc, __func__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (signal(SIGTERM, signal_handler_child) == SIG_ERR)
    {
        fprintf(stderr, "<%s:%d> Error signal(SIGTERM): %s\n", __func__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (signal(SIGUSR1, signal_handler_child) == SIG_ERR)
    {
        print_err("[%d] <%s:%d> Error signal(SIGUSR1): %s\n", numProc, __func__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (signal(SIGUSR2, signal_handler_child) == SIG_ERR)
    {
        print_err("[%d] <%s:%d> Error signal(SIGUSR2): %s\n", numProc, __func__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }
    //------------------------------------------------------------------
    if (chdir(conf->DocumentRoot.c_str()))
    {
        print_err("[%d] <%s:%d> Error chdir(%s): %s\n", numProc, __func__, __LINE__, conf->DocumentRoot.c_str(), strerror(errno));
        exit(EXIT_FAILURE);
    }
    //------------------------------------------------------------------
    thread CgiHandler;
    try
    {
        CgiHandler = thread(cgi_handler, ReqMan);
    }
    catch (...)
    {
        print_err("[%d] <%s:%d> Error create thread(cgi_handler): errno=%d\n", numProc, __func__, __LINE__, errno);
        exit(errno);
    }
    //------------------------------------------------------------------
    thread EventHandler;
    try
    {
        EventHandler = thread(event_handler, ReqMan);
    }
    catch (...)
    {
        print_err("[%d] <%s:%d> Error create thread(event_handler): errno=%d\n", numProc, __func__, __LINE__, errno);
        exit(errno);
    }
    //------------------------------------------------------------------
    unsigned int n = 0;
    while (n < conf->MinThreads)
    {
        thread thr;
        try
        {
            thr = thread(response1, ReqMan);
        }
        catch (...)
        {
            print_err("[%d] <%s:%d> Error create thread: errno=%d\n", numProc, __func__, __LINE__, errno);
            exit(errno);
        }

        ReqMan->start_thr();
        thr.detach();
        ++n;
    }
    //------------------------------------------------------------------
    thread thrReqMan;
    try
    {
        thrReqMan = thread(thr_create_manager, numProc, ReqMan);
    }
    catch (...)
    {
        print_err("<%s:%d> Error create thread %d: errno=%d\n", __func__,
                __LINE__, ReqMan->get_all_thr(), errno);
        exit(errno);
    }
    //------------------------------------------------------------------
    printf("[%u] +++++ num threads=%u, pid=%u, uid=%u, gid=%u +++++\n", numProc,
                                ReqMan->get_num_thr(), getpid(), getuid(), getgid());
    //------------------------------------------------------------------
    static struct pollfd fdrd[2];
    fdrd[0].fd = fd_in;
    fdrd[0].events = POLLIN;

    fdrd[1].fd = sockServer;
    fdrd[1].events = POLLIN;

    unsigned char status = sig;
    int num_fd = 1, run = 1;
    if (write_(fd_out, &status, sizeof(status)) < 0)
        run = 0;

    while (run)
    {
        struct sockaddr_storage clientAddr;
        socklen_t addrSize = sizeof(struct sockaddr_storage);

        if ((0x7f & status) == CONNECT_ALLOW)
        {
            if (is_maxconn())
            {//------ the number of connections is the maximum ---------
                // Allow connections next worker process
                if (write_(fd_out, &status, sizeof(status)) < 0)
                    break;
                status = CONNECT_IGN;
                num_fd = 1;// poll(): pipe[in]
            }
            else
            {
                num_fd = 2;// poll(): pipe[in] and listen socket
                status = CONNECT_ALLOW;
            }
        }
        else
            num_fd = 1;// poll(): pipe[in]

        int ret_poll = poll(fdrd, num_fd, -1);
        if (ret_poll <= 0)
        {
            if (errno == EINTR)
                continue;
            print_err("[%d] <%s:%d> Error poll()=-1: %s\n", numProc, __func__, __LINE__, strerror(errno));
            break;
        }

        if (fdrd[0].revents == POLLIN)
        {
            --ret_poll;
            unsigned char ch;
            if (read_(fd_in, &ch, sizeof(ch)) <= 0)
            {
                print_err("[%d] <%s:%d> Error read(): %s\n", numProc, __func__, __LINE__, strerror(errno));
                break;
            }

            if (ch == PROC_CLOSE)
            {
                // Close next process
                write_(fd_out, &ch, sizeof(ch));
                break;
            }

            status = ch;
            continue;
        }

        if (fdrd[1].revents == POLLIN)
        {
            --ret_poll;

            int clientSocket = accept(sockServer, (struct sockaddr *)&clientAddr, &addrSize);
            if (clientSocket == -1)
            {
                if ((errno == EAGAIN) || (errno == EINTR) || (errno == EMFILE))
                    continue;
                break;
                print_err("[%d] <%s:%d>  Error accept(): %s\n", numProc, __func__, __LINE__, strerror(errno));
            }

            Connect *req;
            req = create_req();
            if (!req)
            {
                shutdown(clientSocket, SHUT_RDWR);
                close(clientSocket);
                break;
            }

            int opt = 1;
            ioctl(clientSocket, FIONBIO, &opt);

            req->init();
            req->numProc = numProc;
            req->numConn = ++allConn;
            req->numReq = 1;
            req->serverSocket = sockServer;
            req->clientSocket = clientSocket;
            get_time(req->sTime);
            req->timeout = conf->Timeout;

            if (getnameinfo((struct sockaddr *)&clientAddr,
                    addrSize,
                    req->remoteAddr,
                    sizeof(req->remoteAddr),
                    req->remotePort,
                    sizeof(req->remotePort),
                    NI_NUMERICHOST | NI_NUMERICSERV))
            {
                print_err(req, "<%s:%d> Error getnameinfo()=%d: %s\n", __func__, __LINE__, n, gai_strerror(n));
                req->remoteAddr[0] = 0;
            }

            start_conn();
            push_get_request(req);
        }

        if (ret_poll)
        {
            print_err("[%d] <%s:%d>  Error: pipe revents=0x%x; socket revents=0x%x\n",
                        numProc, __func__, __LINE__, fdrd[0].revents, fdrd[1].revents);
            break;
        }

        if (conf->NumCpuCores >= 4)
        {
            status = CONNECT_IGN;
            char ch = CONNECT_ALLOW;
            if (write_(fd_out, &ch, sizeof(ch)) < 0)
                break;
        }
    }

    wait_close_all_conn();

    close(sockServer);

    print_err("[%d] <%s:%d>  numThr=%d; allNumThr=%u; all_req=%u; open_conn=%d, status=%u\n", numProc,
                    __func__, __LINE__, ReqMan->get_num_thr(), ReqMan->get_all_thr(), all_req, num_conn, (unsigned int)status);

    ReqMan->close_manager();
    close_event_handler();
    close_cgi_handler();

    thrReqMan.join();
    EventHandler.join();
    CgiHandler.join();

    usleep(100000);
    close(fd_out);

    delete ReqMan;
}
//======================================================================
Connect *create_req(void)
{
    Connect *req = new(nothrow) Connect;
    if (!req)
        print_err("<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
    return req;
}
//======================================================================
int write_(int fd, void *data, int size)
{
    int ret, err;
a1:
    ret = write(fd, data, size);
    if (ret < 0)
    {
        err = errno;
        if (err == EINTR)
            goto a1;
        print_err("<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(err));
    }
    return ret;
}
//======================================================================
int read_(int fd, void *data, int size)
{
    int ret, err;
a1:
    ret = read(fd, data, size);
    if (ret < 0)
    {
        err = errno;
        if (err == EINTR)
            goto a1;
        print_err("<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(err));
    }

    return ret;
}
