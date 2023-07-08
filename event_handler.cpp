#include "main.h"

#if defined(LINUX_)
    #include <sys/sendfile.h>
#elif defined(FREEBSD_)
    #include <sys/uio.h>
#endif

using namespace std;
//======================================================================
static Connect *work_list_start = NULL;
static Connect *work_list_end = NULL;

static Connect *wait_list_start = NULL;
static Connect *wait_list_end = NULL;

static struct pollfd *poll_fd;

static mutex mtx_;
static condition_variable cond_;

static int close_thr = 0;
static int num_poll, num_work;
static int size_buf;
static char *snd_buf;

int send_html(Connect *r);
int create_multipart_head(Connect *req);
void set_part(Connect *r);
int send_headers(Connect *r);
static void worker(Connect *r);
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
                return ERR_TRY_AGAIN;
            print_err(req, "<%s:%d> Error sendfile(): %s\n", __func__, __LINE__, strerror(errno));
            return wr;
        }
    #elif defined(FREEBSD_)
        off_t wr_bytes;
        int ret = sendfile(req->fd, req->clientSocket, req->offset, len, NULL, &wr_bytes, 0);
        if (ret == -1)
        {
            if (errno == EAGAIN)
            {
                if (wr_bytes == 0)
                    return ERR_TRY_AGAIN;
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

        wr = write_to_client(req, snd_buf, rd);
        if (wr < 0)
        {
            if (wr == ERR_TRY_AGAIN)
            {
                lseek(req->fd, -rd, SEEK_CUR);
                return ERR_TRY_AGAIN;
            }

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
    if ((r->source_entity == FROM_FILE) || (r->source_entity == MULTIPART_ENTITY))
        close(r->fd);

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
static void add_work_list()
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
}
//======================================================================
static int set_poll()
{
    num_work = num_poll = 0;
    time_t t = time(NULL);
    Connect *r = work_list_start, *next = NULL;
    for ( ; r; r = next)
    {
        next = r->next;

        if (r->sock_timer == 0)
            r->sock_timer = t;

        if (r->io_status == WORK)
        {
            ++num_work;
            continue;
        }

        if ((t - r->sock_timer) >= r->timeout)
        {
            print_err(r, "<%s:%d> operation=%s, Timeout=%ld\n", __func__, __LINE__, get_str_operation(r->operation), t - r->sock_timer);
            del_from_list(r);
            if (r->operation > READ_REQUEST)
            {
                r->err = -1;
                r->req_hd.iReferer = MAX_HEADERS - 1;
                r->reqHdValue[r->req_hd.iReferer] = "Timeout";
                end_response(r);
            }
            else
            {
                r->err = -1;
                end_response(r);
            }
        }
        else
        {
            poll_fd[num_poll].fd = r->clientSocket;
            poll_fd[num_poll].events = r->event;
            ++num_poll;
        }
    }

    return num_poll;
}
//======================================================================
static int worker(int num_chld)
{
    int ret = 0;
    if (num_poll > 0)
    {
        int time_poll = conf->TimeoutPoll;
        if (num_work > 0)
            time_poll = 0;

        ret = poll(poll_fd, num_poll, time_poll);
        if (ret == -1)
        {
            print_err("[%d]<%s:%d> Error poll(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
            return -1;
        }
        else if (ret == 0)
        {
            if (num_work == 0)
                return 0;
        }
    }
    else
    {
        if (num_work == 0)
            return 0;
    }

    int i = 0, all = ret + num_work;
    Connect *r = work_list_start, *next;
    for ( ; (all > 0) && r; r = next)
    {
        next = r->next;

        if (r->io_status == WORK)
        {
            --all;
            worker(r);
        }
        else
        {
            if ((poll_fd[i].revents == POLLOUT) || (poll_fd[i].revents & POLLIN))
            {
                --all;
                r->io_status = WORK;
                worker(r);
            }
            else if (poll_fd[i].revents)
            {
                --all;
                print_err(r, "<%s:%d> Error: events=0x%x(0x%x)\n", __func__, __LINE__, poll_fd[i].events, poll_fd[i].revents);
                del_from_list(r);
                if (r->operation > READ_REQUEST)
                {
                    r->req_hd.iReferer = MAX_HEADERS - 1;
                    r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
                }

                r->err = -1;
                end_response(r);
            }
            /*else if (poll_fd[i].revents == 0)
            {
                // --all; NO!!!!!
            }*/

            ++i;
        }
    }

    return i;
}
//======================================================================
void event_handler(int num_chld)
{
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

        add_work_list();
        set_poll();
        if (worker(num_chld) < 0)
            break;
    }

    delete [] poll_fd;
#if defined(SEND_FILE_) && (defined(LINUX_) || defined(FREEBSD_))
    if (conf->SendFile != 'y')
#endif
        if (snd_buf)
            delete [] snd_buf;
}
//======================================================================
void add_wait_list(Connect *r)
{
    r->io_status = WORK;
    r->sock_timer = 0;
    r->next = NULL;
mtx_.lock();
    r->prev = wait_list_end;
    if (wait_list_start)
    {
        wait_list_end->next = r;
        wait_list_end = r;
    }
    else
        wait_list_start = wait_list_end = r;
mtx_.unlock();
    cond_.notify_one();
}
//======================================================================
void push_send_file(Connect *r)
{
    lseek(r->fd, r->offset, SEEK_SET);
    r->resp_headers.p = r->resp_headers.s.c_str();
    r->resp_headers.len = r->resp_headers.s.size();

    r->event = POLLOUT;
    r->source_entity = FROM_FILE;
    r->operation = SEND_RESP_HEADERS;
    add_wait_list(r);
}
//======================================================================
void push_pollin_list(Connect *r)
{
    r->event = POLLIN;
    r->source_entity = NO_ENTITY;
    add_wait_list(r);
}
//======================================================================
void push_send_multipart(Connect *r)
{
    r->resp_headers.p = r->resp_headers.s.c_str();
    r->resp_headers.len = r->resp_headers.s.size();
    
    r->event = POLLOUT;
    r->source_entity = MULTIPART_ENTITY;
    r->operation = SEND_RESP_HEADERS;
    add_wait_list(r);
}
//======================================================================
void push_send_html(Connect *r)
{
    r->event = POLLOUT;
    r->source_entity = FROM_DATA_BUFFER;
    r->operation = SEND_RESP_HEADERS;
    add_wait_list(r);
}
//======================================================================
void close_event_handler(void)
{
    close_thr = 1;
    cond_.notify_one();
}
//======================================================================
int send_html(Connect *r)
{
    int ret = write_to_client(r, r->html.p, r->html.len);
    if (ret < 0)
    {
        if (ret == ERR_TRY_AGAIN)
            return ERR_TRY_AGAIN;
        return -1;
    }

    r->html.p += ret;
    r->html.len -= ret;
    r->send_bytes += ret;
    if (r->html.len == 0)
        ret = 0;

    return ret;
}
//======================================================================
void set_part(Connect *r)
{
    if ((r->mp.rg = r->rg.get()))
    {
        r->mp.status = SEND_HEADERS;
        r->resp_headers.len = create_multipart_head(r);
        r->resp_headers.p = r->mp.hdr.c_str();
        
        r->offset = r->mp.rg->start;
        r->respContentLength = r->mp.rg->len;
        lseek(r->fd, r->offset, SEEK_SET);
    }
    else
    {
        r->mp.status = SEND_END;
        r->mp.hdr = "";
        r->mp.hdr << "\r\n--" << boundary << "--\r\n";
        r->resp_headers.len = r->mp.hdr.size();
        r->resp_headers.p = r->mp.hdr.c_str();
    }
}
//======================================================================
int send_headers(Connect *r)
{
    int wr = write_to_client(r, r->resp_headers.p, r->resp_headers.len);
    if (wr < 0)
    {
        if (wr == ERR_TRY_AGAIN)
        {
            r->io_status = POLL;
            return ERR_TRY_AGAIN;
        }
        else
        {
            r->err = -1;
            r->req_hd.iReferer = MAX_HEADERS - 1;
            r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
            del_from_list(r);
            end_response(r);
            return -1;
        }
    }
    else if (wr > 0)
    {
        r->resp_headers.p += wr;
        r->resp_headers.len -= wr;
        r->sock_timer = 0;
    }
    
    return wr;
}
//======================================================================
static void worker(Connect *r)
{
    if (r->operation == SEND_ENTITY)
    {
        if (r->source_entity == FROM_FILE)
        {
            int wr = send_part_file(r);
            if (wr == 0)
            {
                del_from_list(r);
                end_response(r);
            }
            else if (wr == ERR_TRY_AGAIN)
            {
                r->io_status = POLL;
            }
            else if (wr < 0)
            {
                r->err = wr;
                r->req_hd.iReferer = MAX_HEADERS - 1;
                r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
                del_from_list(r);
                end_response(r);
            }
            else // (wr > 0)
                r->sock_timer = 0;
        }
        else if (r->source_entity == MULTIPART_ENTITY)
        {
            if (r->mp.status == SEND_HEADERS)
            {
                int wr = send_headers(r);
                if (wr > 0)
                {
                    r->send_bytes += wr;
                    if (r->resp_headers.len == 0)
                        r->mp.status = SEND_PART;
                }
            }
            else if (r->mp.status == SEND_PART)
            {
                int wr = send_part_file(r);
                if (wr < 0)
                {
                    if (wr == ERR_TRY_AGAIN)
                        r->io_status = POLL;
                    else
                    {
                        r->err = wr;
                        r->req_hd.iReferer = MAX_HEADERS - 1;
                        r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
                        del_from_list(r);
                        end_response(r);
                    }
                }
                else if (wr == 0)
                {
                    r->sock_timer = 0;
                    set_part(r);
                }
                else
                    r->sock_timer = 0;
            }
            else if (r->mp.status == SEND_END)
            {
                int wr = send_headers(r);
                if (wr > 0)
                {
                    r->send_bytes += wr;
                    if (r->resp_headers.len == 0)
                    {
                        del_from_list(r);
                        end_response(r);
                    }
                }
            }
        }
        else if (r->source_entity == FROM_DATA_BUFFER)
        {
            int wr = send_html(r);
            if (wr == 0)
            {
                del_from_list(r);
                end_response(r);
            }
            else if (wr == ERR_TRY_AGAIN)
                r->io_status = POLL;
            else if (wr < 0)
            {
                r->err = -1;
                r->req_hd.iReferer = MAX_HEADERS - 1;
                r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
                del_from_list(r);
                end_response(r);
            }
            else
                r->sock_timer = 0;
        }
    }
    else if (r->operation == SEND_RESP_HEADERS)
    {
        int wr = send_headers(r);
        if (wr > 0)
        {
            if (r->resp_headers.len == 0)
            {
                if (r->reqMethod == M_HEAD)
                {
                    del_from_list(r);
                    end_response(r);
                }
                else
                {
                    if (r->source_entity == FROM_DATA_BUFFER)
                    {
                        if (r->html.len == 0)
                        {
                            del_from_list(r);
                            end_response(r);
                        }
                        else
                            r->operation = SEND_ENTITY;
                    }
                    else if (r->source_entity == FROM_FILE)
                    {
                        r->operation = SEND_ENTITY;
                    }
                    else if (r->source_entity == MULTIPART_ENTITY)
                    {
                        r->operation = SEND_ENTITY;
                        set_part(r);
                    }
                }
            }
        }
    }
    else if (r->operation == READ_REQUEST)
    {
        int ret = read_request_headers(r);
        if (ret < 0)
        {
            if (ret == ERR_TRY_AGAIN)
                r->io_status = POLL;
            else
            {
                r->err = -1;
                del_from_list(r);
                end_response(r);
            }
        }
        else if (ret > 0)
        {
            del_from_list(r);
            push_resp_list(r);
        }
        else
            r->sock_timer = 0;
    }
    else
    {
        print_err(r, "<%s:%d> ? operation=%s\n", __func__, __LINE__, get_str_operation(r->operation));
        r->err = -1;
        del_from_list(r);
        end_response(r);
    }
}
