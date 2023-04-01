#include "main.h"

using namespace std;
//======================================================================
#define FCGI_RESPONDER  1

#define FCGI_VERSION_1           1
#define FCGI_BEGIN_REQUEST       1
#define FCGI_ABORT_REQUEST       2
#define FCGI_END_REQUEST         3
#define FCGI_PARAMS              4
#define FCGI_STDIN               5
#define FCGI_STDOUT              6
#define FCGI_STDERR              7
#define FCGI_DATA                8
#define FCGI_GET_VALUES          9
#define FCGI_GET_VALUES_RESULT  10
#define FCGI_UNKNOWN_TYPE       11
#define FCGI_MAXTYPE            (FCGI_UNKNOWN_TYPE)
#define requestId               1

extern struct pollfd *cgi_poll_fd;

int get_sock_fcgi(Connect *r, const char *script);
void cgi_del_from_list(Connect *r);
int cgi_set_size_chunk(Connect *r);
int cgi_find_empty_line(Connect *req);
//======================================================================
void fcgi_set_header(Connect* r, int type)
{
    r->fcgi.fcgi_type = type;
    r->fcgi.paddingLen = 0;
    char *p = r->cgi->buf;
    *p++ = FCGI_VERSION_1;
    *p++ = (unsigned char)type;
    *p++ = (unsigned char) ((1 >> 8) & 0xff);
    *p++ = (unsigned char) ((1) & 0xff);
    *p++ = (unsigned char) ((r->fcgi.dataLen >> 8) & 0xff);
    *p++ = (unsigned char) ((r->fcgi.dataLen) & 0xff);
    *p++ = r->fcgi.paddingLen;
    *p = 0;
    
    r->cgi->p = r->cgi->buf;
    r->cgi->len_buf += 8;
}
//======================================================================
void fcgi_set_param(Connect *r)
{
    r->cgi->len_buf = 0;
    r->cgi->p = r->cgi->buf + 8;

    for ( ; r->fcgi.i_param < r->fcgi.size_par; ++r->fcgi.i_param)
    {
        int len_name = r->fcgi.vPar[r->fcgi.i_param].name.size();
        int len_val = r->fcgi.vPar[r->fcgi.i_param].val.size();
        int len = len_name + len_val;
        len += len_name > 127 ? 4 : 1;
        len += len_val > 127 ? 4 : 1;
        if (len > (r->cgi->size_buf - r->cgi->len_buf))
        {
            break;
        }

        if (len_name < 0x80)
            *(r->cgi->p++) = (unsigned char)len_name;
        else
        {
            *(r->cgi->p++) = (unsigned char)((len_name >> 24) | 0x80);
            *(r->cgi->p++) = (unsigned char)(len_name >> 16);
            *(r->cgi->p++) = (unsigned char)(len_name >> 8);
            *(r->cgi->p++) = (unsigned char)len_name;
        }

        if (len_val < 0x80)
            *(r->cgi->p++) = (unsigned char)len_val;
        else
        {
            *(r->cgi->p++) = (unsigned char)((len_val >> 24) | 0x80);
            *(r->cgi->p++) = (unsigned char)(len_val >> 16);
            *(r->cgi->p++) = (unsigned char)(len_val >> 8);
            *(r->cgi->p++) = (unsigned char)len_val;
        }

        memcpy(r->cgi->p, r->fcgi.vPar[r->fcgi.i_param].name.c_str(), len_name);
        r->cgi->p += len_name;
        if (len_val > 0)
        {
            memcpy(r->cgi->p, r->fcgi.vPar[r->fcgi.i_param].val.c_str(), len_val);
            r->cgi->p += len_val;
        }

        r->cgi->len_buf += len;
    }

    if (r->cgi->len_buf > 0)
    {
        r->fcgi.dataLen = r->cgi->len_buf;
        fcgi_set_header(r, FCGI_PARAMS);
    }
    else
    {
        r->fcgi.dataLen = r->cgi->len_buf;
        fcgi_set_header(r, FCGI_PARAMS);
    }
}
//======================================================================
int fcgi_create_connect(Connect *req)
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

    if (req->cgi_type == PHPFPM)
        req->fcgi.fd = create_fcgi_socket(conf->PathPHP.c_str());
    else if (req->cgi_type == FASTCGI)
        req->fcgi.fd = get_sock_fcgi(req, req->scriptName);
    else
    {
        print_err(req, "<%s:%d> ? req->scriptType=%d\n", __func__, __LINE__, req->cgi_type);
        return -RS500;
    }

    if (req->fcgi.fd < 0)
    {
        return req->fcgi.fd;
    }

    return 0;
}
//======================================================================
void fcgi_create_param(Connect *req)
{
    int i = 0;
    Param param;
    req->fcgi.vPar.clear();

    if (req->cgi_type == PHPFPM)
    {
        param.name = "REDIRECT_STATUS";
        param.val = "true";
        req->fcgi.vPar.push_back(param);
        ++i;
    }

    param.name = "PATH";
    param.val = "/bin:/usr/bin:/usr/local/bin";
    req->fcgi.vPar.push_back(param);
    ++i;

    param.name = "SERVER_SOFTWARE";
    param.val = conf->ServerSoftware;
    req->fcgi.vPar.push_back(param);
    ++i;

    param.name = "GATEWAY_INTERFACE";
    param.val = "CGI/1.1";
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

    if (req->cgi_type == PHPFPM)
    {
        String s;
        s << conf->DocumentRoot << req->scriptName;

        param.name = "SCRIPT_FILENAME";
        param.val = s;
        req->fcgi.vPar.push_back(param);
        ++i;
    }

    if (req->reqMethod == M_POST)
    {
        if (req->req_hd.iReqContentType >= 0)
        {
            param.name = "CONTENT_TYPE";
            param.val = req->reqHdValue[req->req_hd.iReqContentType];
            req->fcgi.vPar.push_back(param);
            ++i;
        }

        if (req->req_hd.iReqContentLength >= 0)
        {
            param.name = "CONTENT_LENGTH";
            param.val = req->reqHdValue[req->req_hd.iReqContentLength];
            req->fcgi.vPar.push_back(param);
            ++i;
        }
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
    }

    req->fcgi.size_par = i;
    req->fcgi.i_param = 0;
/*
    for (i = 0; i < req->fcgi.size_par; ++i)
    {
        fprintf(stderr, "%s=%s\n", req->fcgi.vPar[i].name.c_str(), req->fcgi.vPar[i].val.c_str());
    }
*/
    //fprintf(stderr, "size_par=%d\n", req->fcgi.size_par);
    //----------------------------------------------
    req->fcgi.dataLen = req->cgi->len_buf = 8;
    fcgi_set_header(req, FCGI_BEGIN_REQUEST);
    char *p = req->cgi->buf + 8;
    *(p++) = (unsigned char) ((FCGI_RESPONDER >> 8) & 0xff);
    *(p++) = (unsigned char) (FCGI_RESPONDER        & 0xff);
    *(p++) = 0;
    *(p++) = 0;
    *(p++) = 0;
    *(p++) = 0;
    *(p++) = 0;
    *(p++) = 0;

    req->cgi->status.fcgi = FASTCGI_BEGIN;
    req->timeout = conf->TimeoutCGI;
    req->sock_timer = 0;
}
//======================================================================
int fcgi_stdin(Connect *r)
{
    if (r->cgi->dir == SOCK_IN)
    {
        int rd = (r->cgi->len_post > r->cgi->size_buf) ? r->cgi->size_buf : r->cgi->len_post;
        r->cgi->len_buf = read(r->clientSocket, r->cgi->buf + 8, rd);
        if (r->cgi->len_buf == -1)
        {
            print_err(r, "<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
        else if (r->cgi->len_buf == 0)
        {
            print_err(r, "<%s:%d> Error read()=0\n", __func__, __LINE__);
            return -1;
        }

        r->cgi->len_post -= r->cgi->len_buf;
        r->fcgi.dataLen = r->cgi->len_buf;
        fcgi_set_header(r, FCGI_STDIN);
        r->cgi->dir = CGI_OUT;
    }
    else if (r->cgi->dir == CGI_OUT)
    {
        int n = write(r->fcgi.fd, r->cgi->p, r->cgi->len_buf);
        if (n == -1)
        {
            print_err(r, "<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EAGAIN)
            {
                r->sock_timer = 0;
                return 0;
            }
            return -1;
        }

        r->cgi->p += n;
        r->cgi->len_buf -= n;
        if (r->cgi->len_buf == 0)
            r->cgi->dir = SOCK_IN;
    }

    return 0;
}
//======================================================================
void fcgi_stdout(Connect *r)
{
    if (r->cgi->dir == CGI_IN)
    {
        if ((r->cgi->status.fcgi == FASTCGI_SEND_ENTITY) || 
            (r->cgi->status.fcgi == FASTCGI_READ_ERROR) ||
            (r->cgi->status.fcgi == FASTCGI_CLOSE))
        {
            if (r->fcgi.dataLen == 0)
            {
                r->cgi->status.fcgi = FASTCGI_READ_PADDING;
                return;
            }

            int len = (r->fcgi.dataLen > r->cgi->size_buf) ? r->cgi->size_buf : r->fcgi.dataLen;
            r->cgi->len_buf = read(r->fcgi.fd, r->cgi->buf + 8, len);
            if (r->cgi->len_buf == -1)
            {
                print_err(r, "<%s:%d> Error read from script(fd=%d): %s(%d)\n", 
                        __func__, __LINE__, r->fcgi.fd, strerror(errno), errno);
                if (errno == EAGAIN)
                {
                    r->sock_timer = 0;
                    return;
                }
                else
                {
                    r->err = -1;
                    cgi_del_from_list(r);
                    end_response(r);
                    return;
                }
            }
            else if (r->cgi->len_buf == 0)
            {
                r->err = -1;
                cgi_del_from_list(r);
                end_response(r);
                return;
            }

            if (r->cgi->status.fcgi == FASTCGI_SEND_ENTITY)
            {
                r->cgi->dir = SOCK_OUT;
                r->fcgi.dataLen -= r->cgi->len_buf;
                if (r->mode_send == CHUNK)
                {
                    if (cgi_set_size_chunk(r))
                        return;
                }
                else
                    r->cgi->p = r->cgi->buf + 8;
            }
            else if (r->cgi->status.fcgi == FASTCGI_READ_ERROR)
            {
                r->fcgi.dataLen -= r->cgi->len_buf;
                if (r->fcgi.dataLen == 0)
                {
                    r->cgi->status.fcgi = FASTCGI_READ_PADDING;
                }
                *(r->cgi->buf + 8 + r->cgi->len_buf) = 0;
                fprintf(stderr, "%s\n", r->cgi->buf + 8);
                if (r->fcgi.dataLen == 0)
                    fprintf(stderr, "\n");
            }
            else if (r->cgi->status.fcgi == FASTCGI_CLOSE)
            {
                r->fcgi.dataLen -= r->cgi->len_buf;
                if (r->fcgi.dataLen == 0)
                {
                    if (r->mode_send == NO_CHUNK)
                    {
                        r->connKeepAlive = 0;
                        cgi_del_from_list(r);
                        end_response(r);
                    }
                    else
                    {
                        r->mode_send = CHUNK_END;
                        r->cgi->len_buf = 0;
                        cgi_set_size_chunk(r);
                        r->cgi->dir = SOCK_OUT;
                        r->timeout = conf->Timeout;
                        r->sock_timer = 0;
                    }
                }
            }
        }
        else if (r->cgi->status.fcgi == FASTCGI_READ_PADDING)
        {
            if (r->fcgi.paddingLen > 0)
            {
                char buf[256];
            
                int len = (r->fcgi.paddingLen > (int)sizeof(buf)) ? sizeof(buf) : r->fcgi.paddingLen;
                int n = read(r->fcgi.fd, buf, len);
                if (n <= 0)
                {
                    print_err(r, "<%s:%d> Error read from script(fd=%d): %s(%d)\n", 
                            __func__, __LINE__, r->fcgi.fd, strerror(errno), errno);
                    if (errno == EAGAIN)
                    {
                        r->sock_timer = 0;
                        return;
                    }
                    else
                    {
                        r->err = -1;
                        cgi_del_from_list(r);
                        end_response(r);
                        return;
                    }
                }
                else
                {
                    r->fcgi.paddingLen -= n;
                }
            }

            if (r->fcgi.paddingLen == 0)
            {
                r->timeout = conf->TimeoutCGI;
                r->cgi->status.fcgi = FASTCGI_READ_HEADER;
                r->fcgi.ptr_header = r->cgi->buf;
                r->fcgi.len_header = 0;
            }
        }
    }
    else if (r->cgi->dir == SOCK_OUT)
    {
        int ret = write(r->clientSocket, r->cgi->p, r->cgi->len_buf);
        if (ret == -1)
        {
            print_err(r, "<%s:%d> Error send to client: %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EAGAIN)
            {
                r->sock_timer = 0;
                return;
            }
            else
            {
                r->err = -1;
                cgi_del_from_list(r);
                end_response(r);
                return;
            }
        }

        r->cgi->p += ret;
        r->cgi->len_buf -= ret;
        r->send_bytes += ret;
        if (r->cgi->len_buf == 0)
        {
            if (r->fcgi.dataLen == 0)
            {
                if (r->fcgi.paddingLen == 0)
                {
                    r->cgi->status.fcgi = FASTCGI_READ_HEADER;
                    r->fcgi.ptr_header = r->cgi->buf;
                    r->fcgi.len_header = 0;
                }
                else
                {
                    r->cgi->status.fcgi = FASTCGI_READ_PADDING;
                }
            }

            r->cgi->dir = CGI_IN;
            r->timeout = conf->TimeoutCGI;
            r->sock_timer = 0;
        }
    }
}
//======================================================================
int fcgi_read_http_headers(Connect *r)
{
    int num_read;
    if ((r->cgi->size_buf - r->cgi->len_buf - 1) >= r->fcgi.dataLen)
        num_read = r->fcgi.dataLen;
    else
        num_read = r->cgi->size_buf - r->cgi->len_buf - 1;
    if (num_read <= 0)
    {
        r->err = -RS502;
        cgi_del_from_list(r);
        end_response(r);
        return -1;
    }

    int n = read(r->fcgi.fd, r->cgi->p, num_read);
    if (n < 0)
    {
        if (errno == EAGAIN)
        {
            r->sock_timer = 0;
            return 0;
        }
        else
        {
            r->err = -RS502;
            cgi_del_from_list(r);
            end_response(r);
            return -1;
        }
    }
    else if (n == 0)
    {
        r->err = -RS502;
        cgi_del_from_list(r);
        end_response(r);
        return -1;
    }

    r->fcgi.dataLen -= n;
    r->lenTail += n;
    r->cgi->len_buf += n;
    r->cgi->p += n;
    *(r->cgi->p) = 0;

    n = cgi_find_empty_line(r);
    if (n == 1) // empty line found
    {
        r->cgi->status.fcgi = FASTCGI_SEND_HTTP_HEADERS;
        r->timeout = conf->Timeout;
        r->fcgi.http_headers_received = true;
        r->sock_timer = 0;
        return r->cgi->len_buf;
    }
    else if (n < 0) // error
    {
        r->err = -RS502;
        cgi_del_from_list(r);
        end_response(r);
        return n;
    }

    r->sock_timer = 0;
    return 0;
}
//======================================================================
int write_to_fcgi(Connect* r)
{
    int ret = write(r->fcgi.fd, r->cgi->p, r->cgi->len_buf);
    if (ret == -1)
    {
        if (errno == EAGAIN)
        {
            print_err(r, "<%s:%d> Error write to fcgi: %s\n", __func__, __LINE__, strerror(errno));
            r->sock_timer = 0;
            return 0;
        }
        else
        {
            print_err(r, "<%s:%d> Error write to fcgi: %s, %p, %p - %p, %d\n", 
                    __func__, __LINE__, strerror(errno), r->cgi->p, r->cgi->buf, r->cgi->buf + 4096, r->cgi->len_buf);
            return -1;
        }
    }
    else
    {
        r->cgi->len_buf -= ret;
        r->cgi->p += ret;
        r->sock_timer = 0;
    }
    
    return ret;
}
//======================================================================
int fcgi_read_header(Connect* r)
{
    int n = 0;

    if (r->fcgi.len_header < 8)
    {
        int len = 8 - r->fcgi.len_header;
        n = read(r->fcgi.fd, r->fcgi.ptr_header, len);
        if (n <= 0)
        {
            if (errno == EAGAIN)
            {
                r->sock_timer = 0;
                return 0;
            }
            print_err(r, "<%s:%d> Error fcgi_read_header(): %s\n", __func__, __LINE__, strerror(errno));
            if (n == 0)
                print_err(r, "<%s:%d> Error read from fcgi: read()=0\n", __func__, __LINE__);
            r->err = -1;
            cgi_del_from_list(r);
            end_response(r);
            return n;
        }
    }
    
    r->fcgi.len_header += n;
    r->fcgi.ptr_header += n;
    r->sock_timer = 0;
    if (r->fcgi.len_header == 8)
    {
        r->fcgi.fcgi_type = (unsigned char)r->cgi->buf[1];
        r->fcgi.paddingLen = (unsigned char)r->cgi->buf[6];
        r->fcgi.dataLen = ((unsigned char)r->cgi->buf[4]<<8) | (unsigned char)r->cgi->buf[5];
    }

    return r->fcgi.len_header;
}
//======================================================================
int get_sock_fcgi(Connect *req, const char *script)
{
    int fcgi_sock = -1, len;
    fcgi_list_addr *ps = conf->fcgi_list;

    if (!script)
    {
        print_err(req, "<%s:%d> Not found\n", __func__, __LINE__);
        return -RS404;
    }

    len = strlen(script);
    if (len > 64)
    {
        print_err(req, "<%s:%d> Error len name script\n", __func__, __LINE__);
        return -RS400;
    }

    for (; ps; ps = ps->next)
    {
        if (!strcmp(script, ps->script_name.c_str()))
            break;
    }

    if (ps != NULL)
    {
        fcgi_sock = create_fcgi_socket(ps->addr.c_str());
        if (fcgi_sock < 0)
        {
            fcgi_sock = -RS502;
        }
    }
    else
    {
        print_err(req, "<%s:%d> Not found: %s\n", __func__, __LINE__, script);
        fcgi_sock = -RS404;
    }

    return fcgi_sock;
}
//======================================================================
void fcgi_set_poll_list(Connect *r, int *i)
{
    if (r->cgi->status.fcgi == FASTCGI_BEGIN)
    {
        cgi_poll_fd[*i].fd = r->fcgi.fd;
        cgi_poll_fd[*i].events = POLLOUT;
    }
    else if (r->cgi->status.fcgi == FASTCGI_PARAMS)
    {
        if (r->cgi->len_buf == 0)
        {
            fcgi_set_param(r);
        }

        cgi_poll_fd[*i].fd = r->fcgi.fd;
        cgi_poll_fd[*i].events = POLLOUT;
    }
    else if (r->cgi->status.fcgi == FASTCGI_STDIN)
    {
        if (r->cgi->dir == SOCK_IN)
        {
            if (r->cgi->len_post > 0)
            {
                if (r->lenTail > 0)
                {
                    if (r->lenTail > r->cgi->size_buf)
                        r->cgi->len_buf = r->cgi->size_buf;
                    else
                        r->cgi->len_buf = r->lenTail;
                    memcpy(r->cgi->buf + 8, r->tail, r->cgi->len_buf);
                    r->lenTail -= r->cgi->len_buf;
                    r->cgi->len_post -= r->cgi->len_buf;
                    if (r->lenTail == 0)
                        r->tail = NULL;
                    else
                        r->tail += r->cgi->len_buf;

                    r->fcgi.dataLen = r->cgi->len_buf;
                    fcgi_set_header(r, FCGI_STDIN);
                    r->sock_timer = 0;
                    r->cgi->dir = CGI_OUT;
                    cgi_poll_fd[*i].fd = r->fcgi.fd;
                    cgi_poll_fd[*i].events = POLLOUT;
                }
                else
                {
                    r->sock_timer = 0;
                    r->cgi->dir = SOCK_IN;
                    cgi_poll_fd[*i].fd = r->clientSocket;
                    cgi_poll_fd[*i].events = POLLIN;
                }
            }
            else
            {
                r->cgi->len_buf = 0;
                r->fcgi.dataLen = r->cgi->len_buf;
                fcgi_set_header(r, FCGI_STDIN);  // post data = 0
                r->sock_timer = 0;
                r->cgi->dir = CGI_OUT;
                cgi_poll_fd[*i].fd = r->fcgi.fd;
                cgi_poll_fd[*i].events = POLLOUT;
            }
        }
        else if (r->cgi->dir == CGI_OUT)
        {
            cgi_poll_fd[*i].fd = r->fcgi.fd;
            cgi_poll_fd[*i].events = POLLOUT;
        }
    }
    else //======================CGI_STDOUT=============================!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    {
        if (r->fcgi.http_headers_received == false)
        {
            r->timeout = conf->TimeoutCGI;
            if (r->fcgi.dataLen == 0)
            {
                if (r->fcgi.paddingLen == 0)
                {
                    r->timeout = conf->TimeoutCGI;
                    r->cgi->status.fcgi = FASTCGI_READ_HEADER;
                    r->fcgi.ptr_header = r->cgi->buf;
                    r->fcgi.len_header = 0;
                }
                else
                    r->cgi->status.fcgi = FASTCGI_READ_PADDING;
            }
            else
                r->cgi->status.fcgi = FASTCGI_READ_HTTP_HEADERS;
        }

        {
            if (r->cgi->status.fcgi == FASTCGI_READ_HEADER)
            {
                cgi_poll_fd[*i].fd = r->fcgi.fd;
                cgi_poll_fd[*i].events = POLLIN;
            }
            else if (r->cgi->status.fcgi == FASTCGI_READ_HTTP_HEADERS)
            {
                cgi_poll_fd[*i].fd = r->fcgi.fd;
                cgi_poll_fd[*i].events = POLLIN;
            }
            else if (r->cgi->status.fcgi == FASTCGI_SEND_HTTP_HEADERS)
            {
                cgi_poll_fd[*i].fd = r->clientSocket;
                cgi_poll_fd[*i].events = POLLOUT;
            }
            else if (r->cgi->status.fcgi == FASTCGI_SEND_ENTITY)
            {
                if (r->lenTail > 0)
                {
                    if (r->cgi->dir == CGI_IN)
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
                        {
                            r->cgi->p = r->cgi->buf + 8;
                        }
                        r->cgi->dir = SOCK_OUT;
                        r->timeout = conf->Timeout;
                        r->sock_timer = 0;

                        cgi_poll_fd[*i].fd = r->clientSocket;
                        cgi_poll_fd[*i].events = POLLOUT;
                    }
                }
                else
                {
                    if (r->cgi->dir == CGI_IN)
                    {
                        if (r->fcgi.dataLen == 0)
                        {
                            if (r->fcgi.paddingLen == 0)
                            {
                                r->timeout = conf->TimeoutCGI;
                                r->cgi->status.fcgi = FASTCGI_READ_HEADER;
                                r->fcgi.ptr_header = r->cgi->buf;
                                r->fcgi.len_header = 0;
                            }
                            else
                            {
                                r->cgi->status.fcgi = FASTCGI_READ_PADDING;
                            }

                            r->cgi->dir = CGI_IN;
                            r->timeout = conf->TimeoutCGI;
                            r->sock_timer = 0;
                        }
                        cgi_poll_fd[*i].fd = r->fcgi.fd;
                        cgi_poll_fd[*i].events = POLLIN;
                    }
                    else if (r->cgi->dir == SOCK_OUT)
                    {
                        cgi_poll_fd[*i].fd = r->clientSocket;
                        cgi_poll_fd[*i].events = POLLOUT;
                    }
                }
            }
            else if (r->cgi->status.fcgi == FASTCGI_READ_ERROR)
            {
                cgi_poll_fd[*i].fd = r->fcgi.fd;
                cgi_poll_fd[*i].events = POLLIN;
            }
            else if (r->cgi->status.fcgi == FASTCGI_READ_PADDING)
            {
                cgi_poll_fd[*i].fd = r->fcgi.fd;
                cgi_poll_fd[*i].events = POLLIN;
            }
            else if (r->cgi->status.fcgi == FASTCGI_CLOSE)
            {
                if (r->mode_send == CHUNK_END)
                {
                    cgi_poll_fd[*i].fd = r->clientSocket;
                    cgi_poll_fd[*i].events = POLLOUT;
                }
                else
                {
                    cgi_poll_fd[*i].fd = r->fcgi.fd;
                    cgi_poll_fd[*i].events = POLLIN;
                }
            }
            else
            {
                print_err(r, "<%s:%d> ??? Error status=%d\n", __func__, __LINE__, r->cgi->status.fcgi);
                if (r->cgi->status.fcgi <= FASTCGI_READ_HTTP_HEADERS)
                    r->err = -RS502;
                else
                    r->err = -1;
                cgi_del_from_list(r);
                end_response(r);
                return;
            }
        }
    }
    (*i)++;
}
//======================================================================
void fcgi_(Connect* r)
{
    if (r->cgi->status.fcgi == FASTCGI_BEGIN)
    {
        int ret = write_to_fcgi(r);
        if (ret > 0)
        {
            r->sock_timer = 0;
            if (r->cgi->len_buf == 0)
            {
                r->cgi->status.fcgi = FASTCGI_PARAMS;
            }
        }
        else if (ret == -1)
        {
            r->err = -RS502;
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else if (r->cgi->status.fcgi == FASTCGI_PARAMS)
    {
        int ret = write_to_fcgi(r);
        if (ret > 0)
        {
            r->sock_timer = 0;
            if ((r->cgi->len_buf == 0) && (r->fcgi.dataLen == 0)) // end params
            {
                r->cgi->status.fcgi = FASTCGI_STDIN;
                r->cgi->dir = SOCK_IN;
                if (r->req_hd.reqContentLength > 0)
                    r->cgi->len_post = r->req_hd.reqContentLength;
                else
                    r->cgi->len_post = 0;
            }
        }
        else if (ret == -1)
        {
            r->err = -RS502;
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else if (r->cgi->status.fcgi == FASTCGI_STDIN)
    {
        int n = fcgi_stdin(r);
        if (n < 0)
        {
            r->err = -RS502;
            cgi_del_from_list(r);
            end_response(r);
        }
        else
        {
            if ((r->cgi->len_buf == 0) && (r->fcgi.dataLen == 0)) //end post data
            {
                r->cgi->status.fcgi = FASTCGI_READ_HEADER;
                r->fcgi.dataLen = 0;
                r->fcgi.paddingLen = 0;

                r->fcgi.http_headers_received = false;
                r->tail = NULL;
                r->p_newline = r->cgi->p = r->cgi->buf + 8;
                r->cgi->len_buf = 0;
                return;
            }
        }
    }
    else//====================== FASTCGI_STDOUT=============================
    {
        if (r->cgi->status.fcgi == FASTCGI_READ_HEADER)
        {
            int ret = fcgi_read_header(r);
            if (ret == 8)
            {
                switch (r->fcgi.fcgi_type)
                {
                    case FCGI_STDOUT:
                    {
                        if (r->fcgi.http_headers_received == true)
                        {
                            if (r->fcgi.dataLen > 0)
                            {
                                r->cgi->status.fcgi = FASTCGI_SEND_ENTITY;
                            }
                            else
                                r->cgi->status.fcgi = FASTCGI_READ_PADDING;
                        }
                    }
                    break;
                    case FCGI_STDERR:
                        r->cgi->status.fcgi = FASTCGI_READ_ERROR;
                        break;
                    case FCGI_END_REQUEST:
                    case FCGI_ABORT_REQUEST:
                        r->cgi->status.fcgi = FASTCGI_CLOSE;
                        break;
                    default:
                        print_err(r, "<%s:%d> Error type=%d\n", __func__, __LINE__, r->fcgi.fcgi_type);
                        r->err = -1;
                        cgi_del_from_list(r);
                        end_response(r);
                }
            }
        }
        else if (r->cgi->status.fcgi == FASTCGI_READ_HTTP_HEADERS)
        {
            int ret = fcgi_read_http_headers(r);
            if (ret > 0)
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
                    r->cgi->status.fcgi = FASTCGI_SEND_HTTP_HEADERS;
                    r->timeout = conf->Timeout;
                }
            }
            else if (ret < 0)
            {
                r->err = -RS502;
                cgi_del_from_list(r);
                end_response(r);
            }
        }
        else if (r->cgi->status.fcgi == FASTCGI_SEND_HTTP_HEADERS)
        {
            if (r->resp_headers.len > 0)
            {
                int wr = write(r->clientSocket, r->resp_headers.p, r->resp_headers.len);
                if (wr < 0)
                {
                    if (errno == EAGAIN)
                        r->sock_timer = 0;
                    else
                    {
                        r->err = -1;
                        r->req_hd.iReferer = MAX_HEADERS - 1;
                        r->reqHdValue[r->req_hd.iReferer] = "Connection reset by peer";
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
                            cgi_del_from_list(r);
                            end_response(r);
                        }
                        else
                        {
                            r->cgi->status.fcgi = FASTCGI_SEND_ENTITY;
                            r->sock_timer = 0;
                            if (r->lenTail > 0)
                            {
                                r->cgi->len_buf = (r->lenTail > r->cgi->size_buf) ? r->cgi->size_buf : r->lenTail;
                                memmove(r->cgi->buf + 8, r->tail, r->cgi->len_buf);
                                r->lenTail -= r->cgi->len_buf;
                                if (r->lenTail == 0)
                                    r->tail = NULL;
                                else
                                    r->tail += r->cgi->len_buf;
                                r->cgi->dir = SOCK_OUT;
                                r->timeout = conf->Timeout;
                                r->sock_timer = 0;
                                if (r->mode_send == CHUNK)
                                {
                                    if (cgi_set_size_chunk(r))
                                    {
                                        r->err = -1;
                                        cgi_del_from_list(r);
                                        end_response(r);
                                    }
                                }
                                else
                                    r->cgi->p = r->cgi->buf + 8;
                            }
                            else
                            {
                                r->cgi->dir = CGI_IN;
                                r->timeout = conf->TimeoutCGI;
                                r->sock_timer = 0;
                            }
                        }
                    }
                    else
                        r->sock_timer = 0;
                }
            }
        }
        else if (r->cgi->status.fcgi == FASTCGI_SEND_ENTITY)
        {
            fcgi_stdout(r);
        }
        else if ((r->cgi->status.fcgi == FASTCGI_READ_ERROR) ||
                (r->cgi->status.fcgi == FASTCGI_READ_PADDING))
        {
            r->cgi->dir = CGI_IN;
            r->timeout = conf->TimeoutCGI;
            r->sock_timer = 0;
            fcgi_stdout(r);
        }
        else if (r->cgi->status.fcgi == FASTCGI_CLOSE)
        {
            if (r->mode_send == CHUNK_END)
            {
                r->cgi->dir = SOCK_OUT;
                r->timeout = conf->Timeout;
                r->sock_timer = 0;
                fcgi_stdout(r);
                if (r->cgi->len_buf == 0)
                {
                    cgi_del_from_list(r);
                    end_response(r);
                }
            }
            else
            {
                r->cgi->dir = CGI_IN;
                r->timeout = conf->TimeoutCGI;
                r->sock_timer = 0;
                fcgi_stdout(r);
            }
        }
        else
        {
            print_err(r, "<%s:%d> Error operation=%d\n", __func__, __LINE__, r->operation);
            r->err = -1;
            cgi_del_from_list(r);
            end_response(r);
        }
    }
}
//======================================================================
int timeout_fcgi(Connect *r)
{
    if ((r->cgi->status.fcgi == FASTCGI_STDIN) && (r->cgi->dir == CGI_OUT))
        return -RS504;
    else if (r->cgi->status.fcgi == FASTCGI_READ_HTTP_HEADERS)
        return -RS504;
    else
        return -1;
}
