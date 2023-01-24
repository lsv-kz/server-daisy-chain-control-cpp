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

static struct pollfd *poll_fd;

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
        get_time(r->sTime);

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

            poll_fd[i].fd = r->clientSocket;
            poll_fd[i].events = r->event;
            ++i;
        }
    }

    return i;
}
//======================================================================
int poll_(int num_chld, int nfd, RequestManager *ReqMan)
{
    int ret = poll(poll_fd, nfd, conf->TimeoutPoll);
    if (ret == -1)
    {
        print_err("[%d]<%s:%d> Error poll(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        return -1;
    }
    else if (ret == 0)
        return 0;

    Connect *r = work_list_start, *next;
    for ( int i = 0; (i < nfd) && (ret > 0) && r; r = next, ++i)
    {
        next = r->next;
        if (poll_fd[i].revents == POLLOUT)
        {
            if (r->status == SEND_ENTITY)
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
                    //print_err(r, "<%s:%d> Error: EAGAIN\n", __func__, __LINE__);
                }
            }
            else if (r->status == SEND_RESP_HEADERS)
            {
                if (r->resp.len > 0)
                {
                    int wr = send(r->clientSocket, r->resp.p, r->resp.len, 0);
                    if (wr == -1)
                    {
                        if (errno == EAGAIN)
                            r->sock_timer = 0;
                        else
                        {
                            r->err = -1;
                            r->req_hd.iReferer = MAX_HEADERS - 1;
                            r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";

                            del_from_list(r);
                            end_response(r);
                        }
                    }
                    else
                    {
                        r->resp.p += wr;
                        r->resp.len -= wr;
                        if (r->resp.len == 0)
                        {
                            if (r->reqMethod != M_HEAD)
                                r->status = SEND_ENTITY;
                            else
                            {
                                del_from_list(r);
                                end_response(r);
                            }
                        }
                        else
                            r->sock_timer = 0;
                    }
                }
                else
                {
                    print_err(r, "<%s:%d> Error resp.len=%d\n", __func__, __LINE__, r->resp.len);
                    r->err = -1;
                    r->req_hd.iReferer = MAX_HEADERS - 1;
                    r->reqHdValue[r->req_hd.iReferer] = "Error send response headers";

                    del_from_list(r);
                    end_response(r);
                }
            }
            --ret;
        }
        else if (poll_fd[i].revents & POLLIN)
        {
            int rd = r->hd_read();
            if (rd == -EAGAIN)
            {
                print_err(r, "<%s:%d> Error hd_read(): EAGAIN\n", __func__, __LINE__);
                r->sock_timer = 0;
            }
            else if (rd < 0)
            {
                r->err = rd;
                del_from_list(r);
                end_response(r);
            }
            else if (rd > 0)
            {
                del_from_list(r);
                push_resp_list(r, ReqMan);
            }
            else // rd == 0
                r->sock_timer = 0;
            --ret;
        }
        else if (poll_fd[i].revents)
        {
            print_err(r, "<%s:%d> Error: events=0x%x, revents=0x%x\n", __func__, __LINE__, poll_fd[i].events, poll_fd[i].revents);
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

    return 1;
}
//======================================================================
void event_handler(RequestManager *ReqMan)
{
    int num_chld = ReqMan->get_num_chld();
    int count_resp = 0;
    size_buf = conf->SndBufSize;
    snd_buf = NULL;

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

    poll_fd = new(nothrow) struct pollfd [conf->MaxWorkConnections];
    if (!poll_fd)
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

        int ret = poll_(num_chld, count_resp, ReqMan);
        if (ret < 0)
        {
            print_err("[%d]<%s:%d> Error poll_()\n", num_chld, __func__, __LINE__);
            continue;
        }
    }

    delete [] poll_fd;
#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SendFile != 'y')
#endif
        if (snd_buf)
            delete [] snd_buf;
    //print_err("*** Exit [%s:proc=%d] ***\n", __func__, num_chld);
}
//======================================================================
void push_pollout_list(Connect *req)
{
    req->event = POLLOUT;
    lseek(req->fd, req->offset, SEEK_SET);
    req->sock_timer = 0;
    req->status = SEND_RESP_HEADERS;
    req->resp.p = req->resp.s.c_str();
    req->resp.len = req->resp.s.size();
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
    req->event = POLLIN;
    req->sock_timer = 0;
    req->status = READ_REQUEST;
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
