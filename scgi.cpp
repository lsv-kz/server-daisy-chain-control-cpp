#include "main.h"

using namespace std;
//======================================================================
extern struct pollfd *cgi_poll_fd;

int get_sock_fcgi(Connect *req, const char *script);
int fcgi_create_connect(Connect *req);
void cgi_del_from_list(Connect *r);
int scgi_set_param(Connect *r);
int cgi_set_size_chunk(Connect *r);
int write_to_fcgi(Connect* r);
int cgi_read_hdrs(Connect *req);
int cgi_stdin(Connect *req);
int cgi_stdout(Connect *req);
//======================================================================
int scgi_set_size_data(Connect* r)
{
    int size = r->cgi->len_buf;
    int i = 7;
    char *p = r->cgi->buf;
    p[i--] = ':';
    
    for ( ; i >= 0; --i)
    {
        p[i] = (size % 10) + '0';
        size /= 10;
        if (size == 0)
            break;
    }
    
    if (size != 0)
        return -1;

    r->cgi->buf[8 + r->cgi->len_buf] = ',';
    r->cgi->p = r->cgi->buf + i;
    r->cgi->len_buf += (8 - i + 1);

    return 0;
}
//======================================================================
int scgi_create_connect(Connect *req)
{
    if (req->reqMethod == M_POST)
    {
        if (req->req_hd.iReqContentType < 0)
        {
            print_err(req, "<%s:%d> Content-Type \?\n", __func__, __LINE__);
            return -RS400;
        }

        if (req->req_hd.reqContentLength < 0)
        {
            print_err(req, "<%s:%d> 411 Length Required\n", __func__, __LINE__);
            return -RS411;
        }

        if (req->req_hd.reqContentLength > conf->ClientMaxBodySize)
        {
            print_err(req, "<%s:%d> 413 Request entity too large: %lld\n", __func__, __LINE__, req->req_hd.reqContentLength);
            return -RS413;
        }
    }

    req->fcgi.fd = get_sock_fcgi(req, req->scriptName);
    if (req->fcgi.fd < 0)
    {
        print_err(req, "<%s:%d> Error connect to scgi\n", __func__, __LINE__);
        return req->fcgi.fd;
    }

    int i = 0;
    Param param;
    req->fcgi.vPar.clear();

    param.name = "PATH";
    param.val = "/bin:/usr/bin:/usr/local/bin";
    req->fcgi.vPar.push_back(param);
    ++i;

    param.name = "SERVER_SOFTWARE";
    param.val = conf->ServerSoftware;
    req->fcgi.vPar.push_back(param);
    ++i;

    param.name = "SCGI";
    param.val = "1";
    req->fcgi.vPar.push_back(param);
    ++i;

    param.name = "DOCUMENT_ROOT";
    param.val = conf->DocumentRoot;
    req->fcgi.vPar.push_back(param);
    ++i;

    param.name = "REMOTE_ADDR";
    param.val = req->remoteAddr;
    req->fcgi.vPar.push_back(param);
    ++i;

    param.name = "REMOTE_PORT";
    param.val = req->remotePort;
    req->fcgi.vPar.push_back(param);
    ++i;

    param.name = "REQUEST_URI";
    param.val = req->uri;
    req->fcgi.vPar.push_back(param);
    ++i;
    
    param.name = "DOCUMENT_URI";
    param.val = req->decodeUri;
    req->fcgi.vPar.push_back(param);
    ++i;

    if (req->reqMethod == M_HEAD)
    {
        param.name = "REQUEST_METHOD";
        param.val = get_str_method(M_GET);
        req->fcgi.vPar.push_back(param);
        ++i;
    }
    else
    {
        param.name = "REQUEST_METHOD";
        param.val = get_str_method(req->reqMethod);
        req->fcgi.vPar.push_back(param);
        ++i;
    }

    param.name = "SERVER_PROTOCOL";
    param.val = get_str_http_prot(req->httpProt);
    req->fcgi.vPar.push_back(param);
    ++i;
    
    param.name = "SERVER_PORT";
    param.val = conf->ServerPort;
    req->fcgi.vPar.push_back(param);
    ++i;

    if (req->req_hd.iHost >= 0)
    {
        param.name = "HTTP_HOST";
        param.val = req->reqHdValue[req->req_hd.iHost];
        req->fcgi.vPar.push_back(param);
        ++i;
    }

    if (req->req_hd.iReferer >= 0)
    {
        param.name = "HTTP_REFERER";
        param.val = req->reqHdValue[req->req_hd.iReferer];
        req->fcgi.vPar.push_back(param);
        ++i;
    }

    if (req->req_hd.iUserAgent >= 0)
    {
        param.name = "HTTP_USER_AGENT";
        param.val = req->reqHdValue[req->req_hd.iUserAgent];
        req->fcgi.vPar.push_back(param);
        ++i;
    }

    param.name = "HTTP_CONNECTION";
    if (req->connKeepAlive == 1)
        param.val = "keep-alive";
    else
        param.val = "close";
    req->fcgi.vPar.push_back(param);
    ++i;

    param.name = "SCRIPT_NAME";
    param.val = req->decodeUri;
    req->fcgi.vPar.push_back(param);
    ++i;

    if (req->reqMethod == M_POST)
    {
        param.name = "CONTENT_TYPE";
        param.val = req->reqHdValue[req->req_hd.iReqContentType];
        req->fcgi.vPar.push_back(param);
        ++i;

        param.name = "CONTENT_LENGTH";
        param.val = req->reqHdValue[req->req_hd.iReqContentLength];
        req->fcgi.vPar.push_back(param);
        ++i;
    }
    else
    {
        param.name = "CONTENT_LENGTH";
        param.val = "0";
        req->fcgi.vPar.push_back(param);
        ++i;
        
        param.name = "CONTENT_TYPE";
        param.val = "";
        req->fcgi.vPar.push_back(param);
        ++i;
    }

    param.name = "QUERY_STRING";
    if (req->sReqParam)
        param.val = req->sReqParam;
    else
        param.val = "";
    req->fcgi.vPar.push_back(param);
    ++i;

    if (i != (int)req->fcgi.vPar.size())
    {
        print_err(req, "<%s:%d> Error: create fcgi param list\n", __func__, __LINE__);
        return -1;
    }

    req->fcgi.size_par = i;
    req->fcgi.i_param = 0;
    
    req->operation = CGI_PARAMS;
    req->cgi->dir = IN;
    req->cgi->p = req->cgi->buf + 8;

    return 0;
}
//======================================================================
void scgi_set_poll_list(Connect *r, int *i)
{
    if (r->operation == CGI_CONNECT)
    {
        if (scgi_create_connect(r))
        {
            r->err = -RS502;
            cgi_del_from_list(r);
            end_response(r);
            return;
        }
    }

    if (r->operation == CGI_PARAMS)
    {
        if (r->cgi->dir == IN)
        {
            int ret = scgi_set_param(r);
            if (ret < 0)
            {
                r->err = -RS502;
                cgi_del_from_list(r);
                end_response(r);
                return;
            }
            else if (ret == 0)
            {
                if (r->operation == CGI_STDOUT)
                {
                    cgi_poll_fd[*i].fd = r->fcgi.fd;
                    cgi_poll_fd[*i].events = POLLIN;
                }
                else if (r->operation == CGI_STDIN)
                {
                    cgi_poll_fd[*i].fd = r->clientSocket;
                    cgi_poll_fd[*i].events = POLLIN;
                }
            }
            else
            {
                cgi_poll_fd[*i].fd = r->fcgi.fd;
                cgi_poll_fd[*i].events = POLLOUT;
            }
        }
        else
        {
            cgi_poll_fd[*i].fd = r->fcgi.fd;
            cgi_poll_fd[*i].events = POLLOUT;
        }
    }
    else if (r->operation == CGI_STDIN)
    {
        if (r->lenTail > 0)
        {
            cgi_poll_fd[*i].fd = r->fcgi.fd;
            cgi_poll_fd[*i].events = POLLOUT;
        }
        else if (r->cgi->dir == IN)
        {
            cgi_poll_fd[*i].fd = r->clientSocket;
            cgi_poll_fd[*i].events = POLLIN;
        }
        else if (r->cgi->dir == OUT)
        {
            cgi_poll_fd[*i].fd = r->fcgi.fd;
            cgi_poll_fd[*i].events = POLLOUT;
        }
    }
    else if (r->operation == CGI_STDOUT)
    {
        if (r->cgi->status == READ_HEADERS)
        {
            cgi_poll_fd[*i].fd = r->fcgi.fd;
            cgi_poll_fd[*i].events = POLLIN;
        }
        else if (r->cgi->status == SEND_HEADERS)
        {
            cgi_poll_fd[*i].fd = r->clientSocket;
            cgi_poll_fd[*i].events = POLLOUT;
        }
        else if (r->cgi->status == READ_CONTENT)
        {
            if (r->lenTail > 0)
            {
                if (r->cgi->dir == IN)
                {
                    int len = (r->lenTail > r->cgi->size_buf) ? r->cgi->size_buf : r->lenTail;
                    memmove(r->cgi->buf + 8, r->tail, len);
                    r->lenTail -= len;
                    if (r->lenTail == 0)
                        r->tail = NULL;
                    else
                        r->tail += len;
                    r->cgi->len_buf = len;
                    if (r->mode_send == CHUNK)
                    {
                        if (cgi_set_size_chunk(r))
                        {
                            r->err = -1;
                            cgi_del_from_list(r);
                            end_response(r);
                            return;
                        }
                    }
                    else
                        r->cgi->p = r->cgi->buf + 8;
                    r->cgi->dir = OUT;

                    cgi_poll_fd[*i].fd = r->clientSocket;
                    cgi_poll_fd[*i].events = POLLOUT;
                }
            }
            else
            {
                if (r->cgi->dir == IN)
                {
                    cgi_poll_fd[*i].fd = r->fcgi.fd;
                    cgi_poll_fd[*i].events = POLLIN;
                }
                else if (r->cgi->dir == OUT)
                {
                    cgi_poll_fd[*i].fd = r->clientSocket;
                    cgi_poll_fd[*i].events = POLLOUT;
                }
            }
        }
    }

    (*i)++;
}
//======================================================================
int scgi_set_param(Connect *r)
{
    r->cgi->len_buf = 0;
    r->cgi->p = r->cgi->buf + 8;

    for ( ; r->fcgi.i_param < r->fcgi.size_par; ++r->fcgi.i_param)
    {
        int len_name = r->fcgi.vPar[r->fcgi.i_param].name.size();
        if (len_name == 0)
        {
            fprintf(stderr, "<%s:%d> Error: len_name=0\n", __func__, __LINE__);
            return -1;
        }

        int len_val = r->fcgi.vPar[r->fcgi.i_param].val.size();
        int len = len_name + len_val + 2;

        if (len > (r->cgi->size_buf - r->cgi->len_buf))
        {
            break;
        }

        memcpy(r->cgi->p, r->fcgi.vPar[r->fcgi.i_param].name.c_str(), len_name);
        r->cgi->p += len_name;
        
        memcpy(r->cgi->p, "\0", 1);
        r->cgi->p += 1;

        if (len_val > 0)
        {
            memcpy(r->cgi->p, r->fcgi.vPar[r->fcgi.i_param].val.c_str(), len_val);
            r->cgi->p += len_val;
        }

        memcpy(r->cgi->p, "\0", 1);
        r->cgi->p += 1;

        r->cgi->len_buf += len;
    }

    if (r->cgi->len_buf > 0)
    {
        scgi_set_size_data(r);
        r->cgi->dir = OUT;
    }
    else
    {
        if (r->req_hd.reqContentLength > 0)
        {
            r->cgi->len_post = r->req_hd.reqContentLength - r->lenTail;
            r->operation = CGI_STDIN;
            r->cgi->dir = IN;
        }
        else
        {
            r->operation = CGI_STDOUT;
            r->cgi->status = READ_HEADERS;
            r->tail = NULL;
            r->p_newline = r->cgi->p = r->cgi->buf;
            r->cgi->len_buf = 0;
        }
    }

    return r->cgi->len_buf;
}
//======================================================================
void scgi_(Connect* r)
{
    if (r->operation == CGI_PARAMS)
    {
        int ret = write_to_fcgi(r);
        if (ret > 0)
        {
            if (r->cgi->len_buf == 0)
            {
                r->cgi->dir = IN;
                r->cgi->p = r->cgi->buf + 8;
            }
        }
        else if (ret == -1)
        {
            r->err = -RS502;
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else if (r->operation == CGI_STDIN)
    {
        int n = cgi_stdin(r);
        if (n < 0)
        {
            print_err(r, "<%s:%d> Error cgi_stdin\n", __func__, __LINE__);
            r->err = -1;
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else if (r->operation == CGI_STDOUT)
    {
        if (r->cgi->status == READ_HEADERS)
        {
            int ret = cgi_read_hdrs(r);
            if (ret == -EAGAIN)
                r->sock_timer = 0;
            if (ret < 0)
            {
                r->err = -RS502;
                cgi_del_from_list(r);
                end_response(r);
            }
            else if (ret > 0)
            {
                r->mode_send = ((r->httpProt == HTTP11) && r->connKeepAlive) ? CHUNK : NO_CHUNK;
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
                    r->cgi->status = SEND_HEADERS;
                }
            }
            else // ret == 0
                r->sock_timer = 0;
        }
        else if (r->cgi->status == SEND_HEADERS)
        {
            if (r->resp_headers.len > 0)
            {
                int wr = write(r->clientSocket, r->resp_headers.p, r->resp_headers.len);
                if (wr == -EAGAIN)
                {
                    r->sock_timer = 0;
                }
                else if (wr < 0)
                {
                    r->err = -1;
                    r->req_hd.iReferer = MAX_HEADERS - 1;
                    r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
                    cgi_del_from_list(r);
                    end_response(r);
                }
                else
                {
                    r->resp_headers.p += wr;
                    r->resp_headers.len -= wr;
                    if (r->resp_headers.len == 0)
                    {
                        if (r->reqMethod == M_HEAD)
                        {
                            cgi_del_from_list(r);
                            end_response(r);
                        }
                        else
                        {
                            r->cgi->status = READ_CONTENT;
                            r->sock_timer = 0;
                            r->cgi->dir = IN;
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
        else if (r->cgi->status == READ_CONTENT)
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
            r->err = -RS502;
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else
    {
        print_err(r, "<%s:%d> ??? Error operation=%d\n", __func__, __LINE__, r->operation);
        r->err = -1;
        cgi_del_from_list(r);
        end_response(r);
    }
}
