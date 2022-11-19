#include "main.h"

#if defined(LINUX_)
    #include <sys/sendfile.h>
#elif defined(FREEBSD_)
    #include <sys/uio.h>
#endif

using namespace std;

//=============================================================================
// push_pollin_list()  >------------|                                        //
//                                  |                                        //
// push_pollout_list() >------------|                                        //
//                                  |                                        //
//                                  V                                        //
//                              [wait_list] -> [work_list] -> end_response() //
//                                                                           //
// [wait_list] - temporary storage                                           //
// [work_list] - storage for working connections                             //
//=============================================================================
static Connect *work_list_start = NULL;
static Connect *work_list_end = NULL;

static Connect *wait_list_start = NULL;
static Connect *wait_list_end = NULL;

static Connect **conn_array;
static struct pollfd *pollfd_array;

static mutex mtx_;
static condition_variable cond_;

static int close_thr = 0;
static int size_buf;
static char *snd_buf;
//======================================================================
int send_part_file(Connect *req)
{
    int rd, wr, len;
    errno = 0;

    if (req->respContentLength == 0)
        return 0;
#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SendFile == 'y')
    {
        if (req->respContentLength >= size_buf)
            len = size_buf;
        else
            len = req->respContentLength;
    #if defined(LINUX_)
        wr = sendfile(req->clientSocket, req->fd, &req->offset, len);
        if (wr == -1)
        {
            if (errno == EAGAIN)
                return -EAGAIN;
            print_err(req, "<%s:%d> Error sendfile(): %s\n", __func__, __LINE__, strerror(errno));
            return wr;
        }
    #elif defined(FREEBSD_)
        off_t wr_bytes;
        int ret = sendfile(req->fd, req->clientSocket, req->offset, len, NULL, &wr_bytes, 0);// SF_NODISKIO SF_NOCACHE
        if (ret == -1)
        {
            if (errno == EAGAIN)
            {
                if (wr_bytes == 0)
                    return -EAGAIN;
                req->offset += wr_bytes;
                wr = wr_bytes;
            }
            else
            {
                print_err("<%s:%d> Error sendfile(): %s\n", __func__, __LINE__, strerror(errno));
                return -1;
            }
        }
        else if (ret == 0)
        {
            req->offset += wr_bytes;
            wr = wr_bytes;
        }
        else
        {
            print_err("<%s:%d> Error sendfile()=%d, wr_bytes=%ld\n", __func__, __LINE__, ret, wr_bytes);
            return -1;
        }
    #endif
    }
    else
#endif
    {
        if (req->respContentLength >= size_buf)
            len = size_buf;
        else
            len = req->respContentLength;

        rd = read(req->fd, snd_buf, len);
        if (rd <= 0)
        {
            if (rd == -1)
                print_err(req, "<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
            return rd;
        }

        wr = write(req->clientSocket, snd_buf, rd);
        if (wr == -1)
        {
            if (errno == EAGAIN)
            {
                lseek(req->fd, -rd, SEEK_CUR);
                return -EAGAIN;
            }
            print_err(req, "<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
            return wr;
        }
        else if (rd != wr)
            lseek(req->fd, wr - rd, SEEK_CUR);
    }
    
    req->send_bytes += wr;
    req->respContentLength -= wr;
    if (req->respContentLength == 0)
        wr = 0;

    return wr;
}
//======================================================================
static void del_from_list(Connect *r)
{
    if (r->event == POLLOUT)
        close(r->fd);
    else
        get_time(r->sLogTime);
    
    if (r->prev && r->next)
    {
        r->prev->next = r->next;
        r->next->prev = r->prev;
    }
    else if (r->prev && !r->next)
    {
        r->prev->next = r->next;
        work_list_end = r->prev;
    }
    else if (!r->prev && r->next)
    {
        r->next->prev = r->prev;
        work_list_start = r->next;
    }
    else if (!r->prev && !r->next)
        work_list_start = work_list_end = NULL;
}
//======================================================================
int set_list()
{
mtx_.lock();
    if (wait_list_start)
    {
        if (work_list_end)
            work_list_end->next = wait_list_start;
        else
            work_list_start = wait_list_start;
        
        wait_list_start->prev = work_list_end;
        work_list_end = wait_list_end;
        wait_list_start = wait_list_end = NULL;
    }
mtx_.unlock();

    int i = 0;
    time_t t = time(NULL);
    Connect *r = work_list_start, *next = NULL;
    for ( ; r; r = next)
    {
        next = r->next;
        
        if (((t - r->sock_timer) >= r->timeout) && (r->sock_timer != 0))
        {
            if (r->lenBufReq)
            {
                r->err = -1;
                print_err(r, "<%s:%d> Timeout = %ld\n", __func__, __LINE__, t - r->sock_timer);
                r->req_hd.iReferer = MAX_HEADERS - 1;
                r->reqHdValue[r->req_hd.iReferer] = "Timeout";
            }
            else
                r->err = NO_PRINT_LOG;

            del_from_list(r);
            end_response(r);
        }
        else
        {
            if (r->sock_timer == 0)
                r->sock_timer = t;

            pollfd_array[i].fd = r->clientSocket;
            pollfd_array[i].events = r->event;
            conn_array[i] = r;
            ++i;
        }
    }

    return i;
}
//======================================================================
int poll_(int num_chld, int i, int nfd, RequestManager *ReqMan)
{
    int ret = poll(pollfd_array + i, nfd, conf->TimeoutPoll);
    if (ret == -1)
    {
        print_err("[%d]<%s:%d> Error poll(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        return -1;
    }
    else if (ret == 0)
        return 0;

    for ( ; (i < nfd) && (ret > 0); ++i)
    {
        Connect *r = conn_array[i];
        if (pollfd_array[i].revents == POLLOUT)
        {
            int wr = send_part_file(r);
            if (wr == 0)
            {
                del_from_list(r);
                end_response(r);
            }
            else if (wr == -1)
            {
                r->err = wr;
                r->req_hd.iReferer = MAX_HEADERS - 1;
                r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";

                del_from_list(r);
                end_response(r);
            }
            else if (wr > 0) 
                r->sock_timer = 0;
            else if (wr == -EAGAIN)
            {
                r->sock_timer = 0;
                print_err(r, "<%s:%d> Error: EAGAIN\n", __func__, __LINE__);
            }
            --ret;
        }
        else if (pollfd_array[i].revents == POLLIN)
        {
            int n = r->hd_read();
            if (n == -EAGAIN)
                r->sock_timer = 0;
            else if (n < 0)
            {
                r->err = n;
                del_from_list(r);
                end_response(r);
            }
            else if (n > 0)
            {
                del_from_list(r);
                push_resp_list(r, ReqMan);
            }
            else // n == 0
                r->sock_timer = 0;
            --ret;
        }
        else if (pollfd_array[i].revents)
        {
            print_err(r, "<%s:%d> Error: events=0x%x, revents=0x%x\n", __func__, __LINE__, pollfd_array[i].events, pollfd_array[i].revents);
            if (r->event == POLLOUT)
            {
                r->req_hd.iReferer = MAX_HEADERS - 1;
                r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
                r->err = -1;
            }
            else
                r->err = NO_PRINT_LOG;

            del_from_list(r);
            end_response(r);
            --ret;
        }
    }

    return i;
}
//======================================================================
void event_handler(RequestManager *ReqMan)
{
    int num_chld = ReqMan->get_num_chld();
    int count_resp = 0;
    size_buf = conf->SndBufSize;
    snd_buf = NULL;

    if (conf->MaxEventConnections <= 0)
    {
        print_err("[%d]<%s:%d> Error config file: MaxEventConnections=%d\n", num_chld, __func__, __LINE__, conf->MaxEventConnections);
        exit(1);
    }

    if (conf->SndBufSize <= 0)
    {
        print_err("[%d]<%s:%d> Error config file: SndBufSize=%d\n", num_chld, __func__, __LINE__, conf->SndBufSize);
        exit(1);
    }

#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SendFile != 'y')
#endif
    {
        snd_buf = new (nothrow) char [size_buf];
        if (!snd_buf)
        {
            print_err("[%d]<%s:%d> Error malloc(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
            exit(1);
        }
    }

    pollfd_array = new(nothrow) struct pollfd [conf->MaxWorkConnections];
    if (!pollfd_array)
    {
        print_err("[%d]<%s:%d> Error malloc(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        exit(1);
    }

    conn_array = new(nothrow) Connect* [conf->MaxWorkConnections];
    if (!conn_array)
    {
        print_err("[%d]<%s:%d> Error malloc(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        exit(1);
    }

    while (1)
    {
        {
    unique_lock<mutex> lk(mtx_);
            while ((!work_list_start) && (!wait_list_start) && (!close_thr))
            {
                cond_.wait(lk);
            }

            if (close_thr)
                break;
        }
        
        count_resp = set_list();
        if (count_resp == 0)
            continue;

        int nfd;
        for (int i = 0; count_resp > 0; )
        {
            if (count_resp > conf->MaxEventConnections)
                nfd = conf->MaxEventConnections;
            else
                nfd = count_resp;

            int ret = poll_(num_chld, i, nfd, ReqMan);
            if (ret < 0)
            {
                print_err("[%d]<%s:%d> Error poll_()\n", num_chld, __func__, __LINE__);
                break;
            }
            else if (ret == 0)
                break;

            i += nfd;
            count_resp -= nfd;
        }
    }

    delete [] pollfd_array;
    delete [] conn_array;
#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SendFile != 'y')
#endif
        if (snd_buf) delete [] snd_buf;
    //print_err("*** Exit [%s:proc=%d] ***\n", __func__, num_chld);
}
//======================================================================
void push_pollout_list(Connect *req)
{
    req->event = POLLOUT;
    lseek(req->fd, req->offset, SEEK_SET);
    req->sock_timer = 0;
    req->next = NULL;
mtx_.lock();
    req->prev = wait_list_end;
    if (wait_list_start)
    {
        wait_list_end->next = req;
        wait_list_end = req;
    }
    else
        wait_list_start = wait_list_end = req;
mtx_.unlock();
    cond_.notify_one();
}
//======================================================================
void push_pollin_list(Connect *req)
{
    req->init();
    req->event = POLLIN;
    req->sock_timer = 0;
    req->next = NULL;
mtx_.lock();
    req->prev = wait_list_end;
    if (wait_list_start)
    {
        wait_list_end->next = req;
        wait_list_end = req;
    }
    else
        wait_list_start = wait_list_end = req;
mtx_.unlock();
    cond_.notify_one();
}
//======================================================================
void close_event_handler(void)
{
    close_thr = 1;
    cond_.notify_one();
}
