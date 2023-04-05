#include "main.h"

using namespace std;
//======================================================================
static Connect *work_list_start = NULL;
static Connect *work_list_end = NULL;

static Connect *wait_list_start = NULL;
static Connect *wait_list_end = NULL;

struct pollfd *cgi_poll_fd;

static mutex mtx_;
static condition_variable cond_;

static int close_thr = 0;
static unsigned int num_wait, num_work;
//----------------------------------------------------------------------
int cgi_set_size_chunk(Connect *req);
static void cgi_set_poll_list(Connect *r, int*);
static void cgi_worker(Connect* r);
void cgi_set_status_readheaders(Connect *r);
int timeout_cgi(Connect *r);
int cgi_create_pipes(Connect *req);
const char *get_cgi_status(int n);
const char *get_cgi_type(int n);
const char *get_cgi_dir(int n);

void fcgi_set_poll_list(Connect *r, int *i);
void fcgi_worker(Connect* r);
int timeout_fcgi(Connect *r);
int fcgi_create_connect(Connect *req);
void fcgi_create_param(Connect *req);

void scgi_set_poll_list(Connect *r, int *i);
void scgi_worker(Connect* r);
int timeout_scgi(Connect *r);
int scgi_create_connect(Connect *req);
//======================================================================
const char *get_script_name(const char *name)
{
    const char *p;
    if (!name)
        return "";

    if ((p = strchr(name + 1, '/')))
        return p;

    return "";
}
//======================================================================
void wait_pid(Connect *req)
{
    int n = waitpid(req->cgi->pid, NULL, WNOHANG); // no blocking
    if (n == -1)
    {
        //print_err(req, "<%s:%d> Error waitpid(%d): %s\n", __func__, __LINE__, req->cgi->pid, strerror(errno));
    }
    else if (n == 0)
    {
        if (kill(req->cgi->pid, SIGKILL) == 0)
            waitpid(req->cgi->pid, NULL, 0);
        else
            print_err(req, "<%s:%d> Error kill(%d): %s\n", __func__, __LINE__, req->cgi->pid, strerror(errno));
    }
}
//======================================================================
void cgi_del_from_list(Connect *r)
{
    if (r->cgi)
    {
        if ((r->cgi_type == CGI) || 
            (r->cgi_type == PHPCGI))
        {
            if (r->cgi->from_script > 0)
            {
                close(r->cgi->from_script);
                r->cgi->from_script = -1;
            }
    
            if (r->cgi->to_script > 0)
            {
                close(r->cgi->to_script);
                r->cgi->to_script = -1;
            }
            
            wait_pid(r);
        }
        else if ((r->cgi_type == PHPFPM) || 
                (r->cgi_type == FASTCGI) ||
                (r->cgi_type == SCGI))
        {
            if (r->fcgi.fd > 0)
                close(r->fcgi.fd);
        }
        
        delete r->cgi;
    }

    r->scriptName = "";

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
mtx_.lock();
    --num_work;
mtx_.unlock();
}
//======================================================================
static void cgi_add_work_list()
{
mtx_.lock();
    if ((num_work < conf->MaxCgiProc) && wait_list_end)
    {
        int n_max = conf->MaxCgiProc - num_work;
        Connect *r = wait_list_end;

        for ( ; (n_max > 0) && r; r = wait_list_end, --n_max)
        {
            wait_list_end = r->prev;
            if (wait_list_end == NULL)
                wait_list_start = NULL;
            --num_wait;
            //--------------------------
            r->cgi = new (nothrow) Cgi;
            if (!r->cgi)
            {
                r->err = -RS500;
                cgi_del_from_list(r);
                end_response(r);
                continue;
            }

            if ((r->cgi_type == CGI) || (r->cgi_type == PHPCGI))
            {
                int ret = cgi_create_pipes(r);
                if (ret < 0)
                {
                    print_err(r, "<%s:%d> Error cgi_create_pipes()\n", __func__, __LINE__);
                    r->err = ret;
                    end_response(r);
                    continue;
                }
            }
            else if ((r->cgi_type == PHPFPM) || (r->cgi_type == FASTCGI))
            {
                int ret = fcgi_create_connect(r);
                if (ret < 0)
                {
                    r->err = ret;
                    end_response(r);
                    continue;
                }

                fcgi_create_param(r);
            }
            else if (r->cgi_type == SCGI)
            {
                int ret = scgi_create_connect(r);
                if (ret < 0)
                {
                    r->err = ret;
                    end_response(r);
                    continue;
                }
                
            }
            else
            {
                print_err(r, "<%s:%d> operation=%d, cgi_type=%ld\n", __func__, __LINE__, r->operation, r->cgi_type);
                end_response(r);
                continue;
            }
            //--------------------------
            if (work_list_end)
                work_list_end->next = r;
            else
                work_list_start = r;

            r->prev = work_list_end;
            r->next = NULL;
            work_list_end = r;
            ++num_work;
        }
    }

mtx_.unlock();
}
//======================================================================
static int set_poll_list()
{
    int i = 0;
    time_t t = time(NULL);

    Connect *r = work_list_start, *next = NULL;
    for ( ; r; r = next)
    {
        next = r->next;
        if (((t - r->sock_timer) >= r->timeout) && (r->sock_timer != 0))
        {
            if ((r->cgi_type == CGI) || (r->cgi_type == PHPCGI))
                r->err = timeout_cgi(r);
            else if ((r->cgi_type == PHPFPM) || (r->cgi_type == FASTCGI))
                r->err = timeout_cgi(r);
            else if (r->cgi_type == SCGI)
                r->err = timeout_scgi(r);
            else
            {
                print_err(r, "<%s:%d> operation=%d, cgi_type=%ld\n", __func__, __LINE__, r->operation, r->cgi_type);
                r->err = -1;
            }

            print_err(r, "<%s:%d> op=%d, Timeout=%ld\n", __func__, __LINE__, r->operation, t - r->sock_timer);

            r->req_hd.iReferer = MAX_HEADERS - 1;
            r->reqHdValue[r->req_hd.iReferer] = "Timeout";
            cgi_del_from_list(r);
            end_response(r);
        }
        else
        {
            if (r->sock_timer == 0)
                r->sock_timer = t;
            
            switch (r->cgi_type)
            {
                case CGI:
                case PHPCGI:
                    cgi_set_poll_list(r, &i);
                    break;
                case PHPFPM:
                case FASTCGI:
                    fcgi_set_poll_list(r, &i);
                    break;
                case SCGI:
                    scgi_set_poll_list(r, &i);
                    break;
                default:
                    print_err(r, "<%s:%d> ??? Error: CGI_TYPE=%s\n", __func__, __LINE__, get_cgi_type(r->cgi_type));
                    r->err = -RS500;
                    cgi_del_from_list(r);
                    end_response(r);
                    break;
            }
        }
    }

    return i;
}
//======================================================================
static int cgi_poll(int num_chld, int nfd, RequestManager *ReqMan)
{
    int ret = poll(cgi_poll_fd, nfd, conf->TimeoutPoll);
    if (ret == -1)
    {
        print_err("[%d]<%s:%d> Error poll(): %s\n", num_chld, __func__, __LINE__, strerror(errno));
        return -1;
    }
    else if (ret == 0)
    {
        //print_err("[%d]<%s:%d> poll()=0\n", num_chld, __func__, __LINE__);
        return 0;
    }

    Connect *r = work_list_start, *next;
    int i = 0;
    for ( ; (i < nfd) && (ret > 0) && r; r = next, ++i)
    {
        next = r->next;
        if (cgi_poll_fd[i].revents == POLLOUT)
        {
            r->poll_status = WORK;
            --ret;
        }
        else if (cgi_poll_fd[i].revents & POLLIN)
        {
            r->poll_status = WORK;
            --ret;
        }
        else if (cgi_poll_fd[i].revents)
        {
            --ret;

            if (cgi_poll_fd[i].fd == r->clientSocket)
            {
                print_err(r, "<%s:%d> Error: fd=%d, events=0x%x(0x%x), send_bytes=%lld\n", 
                        __func__, __LINE__, r->clientSocket, cgi_poll_fd[i].events, cgi_poll_fd[i].revents, r->send_bytes);
                r->req_hd.iReferer = MAX_HEADERS - 1;
                r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
                r->err = -1;
                cgi_del_from_list(r);
                end_response(r);
                continue;
            }
            else
            {
                switch (r->cgi_type)
                {
                    case CGI:
                    case PHPCGI:
                        if ((r->cgi->status.cgi == CGI_SEND_ENTITY) && (r->cgi->dir == FROM_CGI))
                        {
                            if (r->mode_send == CHUNK)
                            {
                                r->cgi->len_buf = 0;
                                cgi_set_size_chunk(r);
                                r->cgi->dir = TO_CLIENT;
                                r->mode_send = CHUNK_END;
                            }
                            else
                            {
                                cgi_del_from_list(r);
                                end_response(r);
                            }
                        }
                        else
                        {
                            print_err(r, "<%s:%d> Error: events=0x%x(0x%x)\n", 
                                    __func__, __LINE__, cgi_poll_fd[i].events, cgi_poll_fd[i].revents);
                            if (cgi_poll_fd[i].fd == r->clientSocket)
                            {
                                r->req_hd.iReferer = MAX_HEADERS - 1;
                                r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
                                r->err = -1;
                            }
                            else
                            {
                                if (r->cgi->status.cgi <= CGI_READ_HTTP_HEADERS)
                                    r->err = -RS502;
                                else
                                    r->err = -1;
                            }
                            cgi_del_from_list(r);
                            end_response(r);
                        }
                        break;
                    case PHPFPM:
                    case FASTCGI:
                        print_err(r, "<%s:%d> Error: events=0x%x(0x%x)\n", 
                                    __func__, __LINE__, cgi_poll_fd[i].events, cgi_poll_fd[i].revents);
                        if (cgi_poll_fd[i].fd == r->clientSocket)
                        {
                            r->req_hd.iReferer = MAX_HEADERS - 1;
                            r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
                            r->err = -1;
                        }
                        else
                        {
                            if (r->cgi->status.fcgi <= FASTCGI_READ_HTTP_HEADERS)
                                r->err = -RS502;
                            else
                                r->err = -1;
                        }
                        cgi_del_from_list(r);
                        end_response(r);
                        break;
                    case SCGI:
                        if ((r->cgi->status.scgi == SCGI_SEND_ENTITY) && (r->cgi->dir == FROM_CGI))
                        {
                            if (r->mode_send == CHUNK)
                            {
                                r->cgi->len_buf = 0;
                                cgi_set_size_chunk(r);
                                r->cgi->dir = TO_CLIENT;
                                r->mode_send = CHUNK_END;
                            }
                            else
                            {
                                cgi_del_from_list(r);
                                end_response(r);
                            }
                        }
                        else
                        {
                            print_err(r, "<%s:%d> Error: events=0x%x(0x%x)\n", 
                                    __func__, __LINE__, cgi_poll_fd[i].events, cgi_poll_fd[i].revents);
                            if (cgi_poll_fd[i].fd == r->clientSocket)
                            {
                                r->req_hd.iReferer = MAX_HEADERS - 1;
                                r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
                                r->err = -1;
                            }
                            else
                            {
                                if (r->cgi->status.scgi <= SCGI_READ_HTTP_HEADERS)
                                    r->err = -RS502;
                                else
                                    r->err = -1;
                            }
                            cgi_del_from_list(r);
                            end_response(r);
                        }
                        break;
                    default:
                        print_err(r, "<%s:%d> ??? Error: CGI_TYPE=%s\n", __func__, __LINE__, get_cgi_type(r->cgi_type));
                        r->err = -1;
                        cgi_del_from_list(r);
                        end_response(r);
                        break;
                }
            }
        }
        else
        {
            r->poll_status = WAIT;
        }
    }

    return i;
}
//======================================================================
static void cgi_worker(int num_chld, int npoll, RequestManager *ReqMan)
{
    Connect *r = work_list_start, *next;
    for ( ; (npoll > 0) && r; r = next, --npoll)
    {
        next = r->next;
        if (r->poll_status == WAIT)
            continue;

        if ((r->cgi_type == CGI) || (r->cgi_type == PHPCGI))
        {
            cgi_worker(r);
        }
        else if ((r->cgi_type == PHPFPM) || (r->cgi_type == FASTCGI))
        {
            fcgi_worker(r);
        }
        else if (r->cgi_type == SCGI)
        {
            scgi_worker(r);
        }
    }
}
//======================================================================
void cgi_handler(RequestManager *ReqMan)
{
    int num_chld = ReqMan->get_num_chld();

    cgi_poll_fd = new(nothrow) struct pollfd [conf->MaxWorkConnections];
    if (!cgi_poll_fd)
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

        cgi_add_work_list();
        int size_poll_list = set_poll_list();
        if (size_poll_list > 0)
        {
            int npoll = cgi_poll(num_chld, size_poll_list, ReqMan);
            if (npoll > 0)
                cgi_worker(num_chld, npoll, ReqMan);
        }
    }

    delete [] cgi_poll_fd;
}
//======================================================================
void push_cgi(Connect *r)
{
    r->operation = DYN_PAGE;
    r->respStatus = RS200;
    r->sock_timer = 0;
    r->prev = NULL;
mtx_.lock();
    r->next = wait_list_start;
    if (wait_list_start)
        wait_list_start->prev = r;
    wait_list_start = r;
    if (!wait_list_end)
        wait_list_end = r;
    ++num_wait;
mtx_.unlock();
    cond_.notify_one();
}
//======================================================================
static int cgi_fork(Connect *r, int* serv_cgi, int* cgi_serv)
{
    struct stat st;

    if (r->reqMethod == M_POST)
    {
        if (r->req_hd.iReqContentType < 0)
        {
            print_err(r, "<%s:%d> Content-Type \?\n", __func__, __LINE__);
            return -RS400;
        }

        if (r->req_hd.reqContentLength < 0)
        {
            print_err(r, "<%s:%d> 411 Length Required\n", __func__, __LINE__);
            return -RS411;
        }

        if (r->req_hd.reqContentLength > conf->ClientMaxBodySize)
        {
            print_err(r, "<%s:%d> 413 Request entity too large: %lld\n", __func__, __LINE__, r->req_hd.reqContentLength);
            return -RS413;
        }
    }

    String path;
    switch (r->cgi_type)
    {
        case CGI:
            path << conf->ScriptPath << get_script_name(r->scriptName.c_str());
            break;
        case PHPCGI:
            path << conf->DocumentRoot << r->scriptName.c_str();
            break;
        default:
            print_err(r, "<%s:%d> ??? Error: CGI_TYPE=%s\n", __func__, __LINE__, get_cgi_type(r->cgi_type));
            r->connKeepAlive = 0;
            return -RS500;
    }
    
    if (stat(path.c_str(), &st) == -1)
    {
        print_err(r, "<%s:%d> script (%s) not found\n", __func__, __LINE__, path.c_str());
        r->connKeepAlive = 0;
        return -RS404;
    }
    //--------------------------- fork ---------------------------------
    pid_t pid = fork();
    if (pid < 0)
    {
        r->cgi->pid = pid;
        print_err(r, "<%s:%d> Error fork(): %s\n", __func__, __LINE__, strerror(errno));
        return -RS500;
    }
    else if (pid == 0)
    {
        //----------------------- child --------------------------------
        close(cgi_serv[0]);

        if (r->reqMethod == M_POST)
        {
            close(serv_cgi[1]);
            if (serv_cgi[0] != STDIN_FILENO)
            {
                if (dup2(serv_cgi[0], STDIN_FILENO) < 0)
                {
                    fprintf(stderr, "<%s:%d> Error dup2(): %s\n", __func__, __LINE__, strerror(errno));
                    exit(EXIT_FAILURE);
                }
                close(serv_cgi[0]);
            }
        }

        if (cgi_serv[1] != STDOUT_FILENO)
        {
            if (dup2(cgi_serv[1], STDOUT_FILENO) < 0)
            {
                fprintf(stderr, "<%s:%d> Error dup2(): %s\n", __func__, __LINE__, strerror(errno));
                exit(EXIT_FAILURE);
            }
            close(cgi_serv[1]);
        }
        
        if (r->cgi_type == PHPCGI)
            setenv("REDIRECT_STATUS", "true", 1);
        setenv("PATH", "/bin:/usr/bin:/usr/local/bin", 1);
        setenv("SERVER_SOFTWARE", conf->ServerSoftware.c_str(), 1);
        setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
        setenv("DOCUMENT_ROOT", conf->DocumentRoot.c_str(), 1);
        setenv("REMOTE_ADDR", r->remoteAddr, 1);
        setenv("REMOTE_PORT", r->remotePort, 1);
        setenv("REQUEST_URI", r->uri, 1);
        setenv("REQUEST_METHOD", get_str_method(r->reqMethod), 1);
        setenv("SERVER_PROTOCOL", get_str_http_prot(r->httpProt), 1);
        if (r->req_hd.iHost >= 0)
            setenv("HTTP_HOST", r->reqHdValue[r->req_hd.iHost], 1);
        if (r->req_hd.iReferer >= 0)
            setenv("HTTP_REFERER", r->reqHdValue[r->req_hd.iReferer], 1);
        if (r->req_hd.iUserAgent >= 0)
            setenv("HTTP_USER_AGENT", r->reqHdValue[r->req_hd.iUserAgent], 1);

        setenv("SCRIPT_NAME", r->scriptName.c_str(), 1);
        setenv("SCRIPT_FILENAME", path.c_str(), 1);
        
        if (r->reqMethod == M_POST)
        {
            if (r->req_hd.iReqContentType >= 0)
                setenv("CONTENT_TYPE", r->reqHdValue[r->req_hd.iReqContentType], 1);
            if (r->req_hd.iReqContentLength >= 0)
                setenv("CONTENT_LENGTH", r->reqHdValue[r->req_hd.iReqContentLength], 1);
        }

        setenv("QUERY_STRING", r->sReqParam ? r->sReqParam : "", 1);

        int err_ = 0;
        if (r->cgi_type == CGI)
        {
            execl(path.c_str(), base_name(r->scriptName.c_str()), NULL);
            err_ = errno;
        }
        else if (r->cgi_type == PHPCGI)
        {
            if (conf->UsePHP == "php-cgi")
            {
                execl(conf->PathPHP.c_str(), base_name(conf->PathPHP.c_str()), NULL);
                err_ = errno;
            }
        }

        printf( "Status: 500 Internal Server Error\r\n"
                "Content-type: text/html; charset=UTF-8\r\n"
                "\r\n"
                "<!DOCTYPE html>\n"
                "<html>\n"
                " <head>\n"
                "  <title>500 Internal Server Error</title>\n"
                "  <meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n"
                " </head>\n"
                " <body>\n"
                "  <h3>500 Internal Server Error</h3>\n"
                "  <p>%s</p>\n"
                "  <hr>\n"
                "  %s\n"
                " </body>\n"
                "</html>", strerror(err_), r->sTime.c_str());
        fclose(stdout);
        exit(EXIT_FAILURE);
    }
    else
    {
        r->cgi->pid = pid;
        if (r->reqMethod == M_POST)
        {
            if (r->req_hd.reqContentLength > 0)
            {
                r->sock_timer = 0;
                r->cgi->len_post = r->req_hd.reqContentLength - r->lenTail;
                r->cgi->status.cgi = CGI_STDIN;
                if (r->lenTail > 0)
                {
                    r->cgi->dir = TO_CGI;
                    r->timeout = conf->TimeoutCGI;
                    r->cgi->p = r->tail;
                    r->cgi->len_buf = r->lenTail;
                }
                else // [r->lenTail == 0]
                {
                    r->cgi->dir = FROM_CLIENT;
                    r->timeout = conf->Timeout;
                }
            }
            else // (r->req_hd.reqContentLength == 0)
            {
                cgi_set_status_readheaders(r);
            }
        }
        else // r->reqMethod == M_GET
        {
            cgi_set_status_readheaders(r);
        }

        r->tail = NULL;
        r->lenTail = 0;
        r->sock_timer = 0;

        r->mode_send = ((r->httpProt == HTTP11) && r->connKeepAlive) ? CHUNK : NO_CHUNK;
    }

    return 0;
}
//======================================================================
int cgi_create_pipes(Connect *req)
{
    int serv_cgi[2], cgi_serv[2];

    int n = pipe(cgi_serv);
    if (n == -1)
    {
        print_err(req, "<%s:%d> Error pipe()=%d\n", __func__, __LINE__, n);
        req->connKeepAlive = 0;
        return -RS500;
    }

    if (req->reqMethod == M_POST)
    {
        n = pipe(serv_cgi);
        if (n == -1)
        {
            print_err(req, "<%s:%d> Error pipe()=%d\n", __func__, __LINE__, n);
            req->connKeepAlive = 0;
            close(cgi_serv[0]);
            close(cgi_serv[1]);
            return -RS500;
        }
    }
    else
    {
        serv_cgi[0] = -1;
        serv_cgi[1] = -1;
    }

    n = cgi_fork(req, serv_cgi, cgi_serv);
    if (n < 0)
    {
        if (req->reqMethod == M_POST)
        {
            close(serv_cgi[0]);
            close(serv_cgi[1]);
        }

        close(cgi_serv[0]);
        close(cgi_serv[1]);
        return n;
    }
    else
    {
        if (req->reqMethod == M_POST)
            close(serv_cgi[0]);
        close(cgi_serv[1]);
        
        req->cgi->from_script = cgi_serv[0];
        req->cgi->to_script = serv_cgi[1];
    }

    return 0;
}
//======================================================================
int cgi_stdin(Connect *req)
{
    if (req->tail)
    {
        int fd;
        if ((req->cgi_type == CGI) || (req->cgi_type == PHPCGI))
            fd = req->cgi->to_script;
        else
            fd = req->fcgi.fd;
        int ret = write(fd, req->tail, req->lenTail);
        if (ret == -1)
        {
            print_err(req, "<%s:%d> Error write to script: %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
        else if (ret != req->lenTail)
        {
            print_err(req, "<%s:%d>  write() != len\n", __func__, __LINE__);
        }

        req->tail += ret;
        req->lenTail -= ret;
        
        if (req->lenTail == 0)
        {
            req->tail = NULL;
            if (req->cgi->len_post == 0)
            {
                close(req->cgi->to_script);
                req->cgi->to_script = -1;
                cgi_set_status_readheaders(req);
            }
            else
                req->cgi->dir = FROM_CLIENT;
        }
    }
    else if (req->cgi->dir == FROM_CLIENT)
    {
        int rd = (req->cgi->len_post > req->cgi->size_buf) ? req->cgi->size_buf : req->cgi->len_post;
        req->cgi->len_buf = read(req->clientSocket, req->cgi->buf, rd);
        if (req->cgi->len_buf == -1)
        {
            print_err(req, "<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
        else if (req->cgi->len_buf == 0)
        {
            print_err(req, "<%s:%d> Error read()=0\n", __func__, __LINE__);
            return -1;
        }
        
        req->cgi->len_post -= req->cgi->len_buf;
        req->cgi->dir = TO_CGI;
        req->cgi->p = req->cgi->buf;
    }
    else if (req->cgi->dir == TO_CGI)
    {
        int fd;
        if ((req->cgi_type == CGI) || (req->cgi_type == PHPCGI))
            fd = req->cgi->to_script;
        else
            fd = req->fcgi.fd;
        int n = write(fd, req->cgi->p, req->cgi->len_buf);
        if (n == -1)
        {
            print_err(req, "<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }

        req->cgi->p += n;
        req->cgi->len_buf -= n;

        if (req->cgi->len_buf == 0)
        {
            if (req->cgi->len_post == 0)
            {
                if ((req->cgi_type == CGI) || (req->cgi_type == PHPCGI))
                {
                    close(req->cgi->to_script);
                    req->cgi->to_script = -1;
                }
                cgi_set_status_readheaders(req);
            }
            else
                req->cgi->dir = FROM_CLIENT;
        }
    }
    
    return 0;
}
//======================================================================
int cgi_stdout(Connect *req)
{
    if (req->cgi->dir == FROM_CGI)
    {
        int fd;
        if ((req->cgi_type == CGI) || (req->cgi_type == PHPCGI))
            fd = req->cgi->from_script;
        else
            fd = req->fcgi.fd;
        req->cgi->len_buf = read(fd, req->cgi->buf + 8, req->cgi->size_buf);
        if (req->cgi->len_buf == -1)
        {
            print_err(req, "<%s:%d> Error read from script(): %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EAGAIN)
                return -EAGAIN;
            return -1;
        }
        else if (req->cgi->len_buf == 0)
        {
            if (req->mode_send == CHUNK)
            {
                req->cgi->len_buf = 0;
                cgi_set_size_chunk(req);
                req->cgi->dir = TO_CLIENT;
                req->mode_send = CHUNK_END;
                return 4;
            }
            return 0;
        }

        req->cgi->dir = TO_CLIENT;
        if (req->mode_send == CHUNK)
        {
            if (cgi_set_size_chunk(req))
                return -1;
        }
        else
            req->cgi->p = req->cgi->buf + 8;
        return req->cgi->len_buf;
    }
    else if (req->cgi->dir == TO_CLIENT)
    {
        int ret = write(req->clientSocket, req->cgi->p, req->cgi->len_buf);
        if (ret == -1)
        {
            print_err(req, "<%s:%d> Error send to client: %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EAGAIN)
                return -EAGAIN;
            return -1;
        }

        req->cgi->p += ret;
        req->cgi->len_buf -= ret;
        req->send_bytes += ret;
        if (req->cgi->len_buf == 0)
        {
            req->cgi->dir = FROM_CGI;
        }
        return ret;
    }

    return 0;
}
//======================================================================
void close_cgi_handler(void)
{
    close_thr = 1;
    cond_.notify_one();
}
//======================================================================
int cgi_find_empty_line(Connect *req)
{
    req->timeout = conf->Timeout;
    char *pCR, *pLF;
    while (req->lenTail > 0)
    {
        int i = 0, len_line = 0;
        pCR = pLF = NULL;
        while (i < req->lenTail)
        {
            char ch = *(req->p_newline + i);
            if (ch == '\r')// found CR
            {
                if (i == (req->lenTail - 1))
                    return 0;
                if (pCR)
                    return -RS502;
                pCR = req->p_newline + i;
            }
            else if (ch == '\n')// found LF
            {
                pLF = req->p_newline + i;
                if ((pCR) && ((pLF - pCR) != 1))
                    return -RS502;
                i++;
                break;
            }
            else
                len_line++;
            i++;
        }

        if (pLF) // found end of line '\n'
        {
            if (pCR == NULL)
                *pLF = 0;
            else
                *pCR = 0;

            if (len_line == 0)
            {
                req->lenTail -= i;
                if (req->lenTail > 0)
                    req->tail = pLF + 1;
                else
                    req->tail = NULL;
                return 1;
            }
///fprintf(stderr, "<%s:%d> [%s]\n", __func__, __LINE__, req->p_newline);
            if (!memchr(req->p_newline, ':', len_line))
            {
                //print_err(req, "<%s:%d> Error Line not header: [%s]\n", __func__, __LINE__, req->p_newline);
                return -RS502;
            }

            if (!strlcmp_case(req->p_newline, "Status", 6))
            {
                req->respStatus = atoi(req->p_newline + 7);
            }
            else
                req->hdrs << req->p_newline << "\r\n";

            req->lenTail -= i;
            req->p_newline = pLF + 1;
        }
        else if (pCR && (!pLF))
            return -RS502;
        else
            break;
    }

    return 0;
}
//======================================================================
int cgi_read_hdrs(Connect *req)
{
    int num_read = req->cgi->size_buf - req->cgi->len_buf - 1;
    if (num_read <= 0)
        return -RS505;
    //num_read = (num_read > 16) ? 16 : num_read;
    int fd;
    if ((req->cgi_type == CGI) || (req->cgi_type == PHPCGI))
        fd = req->cgi->from_script;
    else
        fd = req->fcgi.fd;

    int n = read(fd, req->cgi->buf + req->cgi->len_buf, num_read);
    if (n < 0)
    {
        if (errno == EAGAIN)
            return -EAGAIN;
        return -1;
    }
    else if (n == 0)
        return -1;

    req->lenTail += n;
    req->cgi->len_buf += n;
    req->cgi->buf[req->cgi->len_buf] = 0;

    n = cgi_find_empty_line(req);
    if (n == 1) // empty line found
        return req->cgi->len_buf;
    else if (n < 0) // error
        return n;

    return 0;
}
//======================================================================
int cgi_set_size_chunk(Connect *r)
{
    int size = r->cgi->len_buf;
    const char *hex = "0123456789ABCDEF";
    char *p = r->cgi->buf;
    int i = 7;
    p[i--] = '\n';
    p[i--] = '\r';

    for ( ; i >= 0; --i)
    {
        p[i] = hex[size % 16];
        size /= 16;
        if (size == 0)
            break;
    }

    if (size != 0)
        return -1;

    r->cgi->p = r->cgi->buf + i;
    memcpy(r->cgi->buf + 8 + r->cgi->len_buf, "\r\n", 2);
    r->cgi->len_buf += (8 - i + 2);

    return 0;
}
//======================================================================
static void cgi_set_poll_list(Connect *r, int *i)
{
    if (r->cgi->status.cgi == CGI_STDIN)
    {
        if (r->cgi->dir == FROM_CLIENT)
        {
            cgi_poll_fd[*i].fd = r->clientSocket;
            cgi_poll_fd[*i].events = POLLIN;
        }
        else if (r->cgi->dir == TO_CGI)
        {
            cgi_poll_fd[*i].fd = r->cgi->to_script;
            cgi_poll_fd[*i].events = POLLOUT;
        }
    }
    else if (r->cgi->status.cgi == CGI_READ_HTTP_HEADERS)
    {
        cgi_poll_fd[*i].fd = r->cgi->from_script;
        cgi_poll_fd[*i].events = POLLIN;
    }
    else if (r->cgi->status.cgi == CGI_SEND_HTTP_HEADERS)
    {
        cgi_poll_fd[*i].fd = r->clientSocket;
        cgi_poll_fd[*i].events = POLLOUT;
    }
    else if (r->cgi->status.cgi == CGI_SEND_ENTITY)
    {
        if (r->cgi->dir == FROM_CGI)
        {
            cgi_poll_fd[*i].fd = r->cgi->from_script;
            cgi_poll_fd[*i].events = POLLIN;
        }
        else if (r->cgi->dir == TO_CLIENT)
        {
            cgi_poll_fd[*i].fd = r->clientSocket;
            cgi_poll_fd[*i].events = POLLOUT;
        }
    }
    else
    {
        print_err(r, "<%s:%d> ??? Error: CGI_STATUS=%s\n", __func__, __LINE__, get_cgi_status(r->cgi->status.cgi));
        r->err = -1;
        cgi_del_from_list(r);
        end_response(r);
        return;
    }

    (*i)++;
}
//======================================================================
static void cgi_worker(Connect* r)
{
    if (r->cgi->status.cgi == CGI_STDIN)
    {
        int n = cgi_stdin(r);
        if (n < 0)
        {
            print_err(r, "<%s:%d> Error cgi_stdin\n", __func__, __LINE__);
            r->err = n;
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else if (r->cgi->status.cgi == CGI_READ_HTTP_HEADERS)
    {
        int rd = cgi_read_hdrs(r);
        if (rd == -EAGAIN)
            r->sock_timer = 0;
        else if (rd < 0)
        {
            r->err = rd;
            cgi_del_from_list(r);
            end_response(r);
        }
        else if (rd > 0)
        {
            if (create_response_headers(r))
            {
                print_err(r, "<%s:%d> Error create_response_headers()\n", __func__, __LINE__);
                r->err = -1;
                cgi_del_from_list(r);
                end_response(r);
            }
            else
            {
                r->resp_headers.p = r->resp_headers.s.c_str();
                r->resp_headers.len = r->resp_headers.s.size();
                r->cgi->status.cgi = CGI_SEND_HTTP_HEADERS;
                r->timeout = conf->Timeout;
                r->sock_timer = 0;
            }
        }
        else // rd == 0
            r->sock_timer = 0;
    }
    else if (r->cgi->status.cgi == CGI_SEND_HTTP_HEADERS)
    {
        if (r->resp_headers.len > 0)
        {
            int wr = write(r->clientSocket, r->resp_headers.p, r->resp_headers.len);
            if (wr < 0)
            {
                if (errno == EAGAIN)
                {
                    r->sock_timer = 0;
                }
                else
                {
                    r->err = -1;
                    r->req_hd.iReferer = MAX_HEADERS - 1;
                    r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
                    close(r->cgi->from_script);
                    r->cgi->from_script = -1;
                    cgi_del_from_list(r);
                    end_response(r);
                }
            }
            else
            {
                r->resp_headers.p += wr;
                r->resp_headers.len -= wr;
                if (r->resp_headers.len == 0)
                {
                    if (r->reqMethod == M_HEAD)
                    {
                        close(r->cgi->from_script);
                        r->cgi->from_script = -1;
                        cgi_del_from_list(r);
                        end_response(r);
                    }
                    else
                    {
                        r->cgi->status.cgi = CGI_SEND_ENTITY;
                        r->sock_timer = 0;
                        if (r->lenTail > 0)
                        {
                            if (r->mode_send == CHUNK)
                            {
                                memmove(r->cgi->buf + 8, r->tail, r->lenTail);
                                r->cgi->len_buf = r->lenTail;
                                r->tail = NULL;
                                r->lenTail = 0;
                                if (cgi_set_size_chunk(r))
                                {
                                    r->err = -1;
                                    cgi_del_from_list(r);
                                    end_response(r);
                                    return;
                                }
                            }
                            else
                            {
                                r->cgi->p = r->tail;
                                r->cgi->len_buf = r->lenTail;
                                r->lenTail = 0;
                            }
                            r->cgi->dir = TO_CLIENT;
                            r->timeout = conf->Timeout;
                        }
                        else
                        {
                            r->cgi->len_buf = 0;
                            r->cgi->p = NULL;
                            r->cgi->dir = FROM_CGI;
                            r->timeout = conf->TimeoutCGI;
                        }
                    }
                }
                else
                    r->sock_timer = 0;
            }
        }
        else
        {
            print_err(r, "<%s:%d> Error resp.len=%d\n", __func__, __LINE__, r->resp_headers.len);
            r->err = -1;
            r->req_hd.iReferer = MAX_HEADERS - 1;
            r->reqHdValue[r->req_hd.iReferer] = "Error send response headers";
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else if (r->cgi->status.cgi == CGI_SEND_ENTITY)
    {
        int ret = cgi_stdout(r);
        if (ret == -EAGAIN)
        {
            r->sock_timer = 0;
        }
        else if (ret < 0)
        {
            r->err = -1;
            cgi_del_from_list(r);
            end_response(r);
        }
        else if (ret == 0)
        {
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else
    {
        print_err(r, "<%s:%d> ??? Error: CGI_STATUS=%s\n", __func__, __LINE__, get_cgi_status(r->cgi->status.cgi));
        r->err = -1;
        cgi_del_from_list(r);
        end_response(r);
    }
}
//======================================================================
void cgi_set_status_readheaders(Connect *r)
{
    r->cgi->status.cgi = CGI_READ_HTTP_HEADERS;
    r->tail = NULL;
    r->lenTail = 0;
    r->p_newline = r->cgi->buf;
    r->cgi->len_buf = 0;
    r->timeout = conf->TimeoutCGI;
    r->sock_timer = 0;
}
//======================================================================
int timeout_cgi(Connect *r)
{
    if ((r->cgi->status.cgi == CGI_STDIN) && (r->cgi->dir == TO_CGI))
        return -RS504;
    else if (r->cgi->status.cgi == CGI_READ_HTTP_HEADERS)
        return -RS504;
    else
        return -1;
}
