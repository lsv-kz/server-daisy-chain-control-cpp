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
static void fcgi_create_param(Connect *req);
//======================================================================
void fcgi_set_header(Connect* r, int type)
{
    char *p = r->cgi->buf;
    *p++ = FCGI_VERSION_1;
    *p++ = (unsigned char)type;
    *p++ = (unsigned char) ((1 >> 8) & 0xff);
    *p++ = (unsigned char) ((1) & 0xff);

    *p++ = (unsigned char) ((r->cgi->len_buf >> 8) & 0xff);
    *p++ = (unsigned char) ((r->cgi->len_buf) & 0xff);

    *p++ = 0; // padding;
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
        r->cgi->dir = OUT;
        fcgi_set_header(r, FCGI_PARAMS);
    }
    else
    {
        fcgi_set_header(r, FCGI_PARAMS);
        r->operation = CGI_END_PARAMS;
    }

    r->cgi->dir = OUT;
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
static void fcgi_create_param(Connect *req)
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
    req->cgi->len_buf = 8;
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

    req->operation = FCGI_BEGIN;
}
//======================================================================
int fcgi_stdin(Connect *req)
{
    if (req->cgi->dir == IN)
    {
        int rd = (req->cgi->len_post > req->cgi->size_buf) ? req->cgi->size_buf : req->cgi->len_post;
        req->cgi->len_buf = read(req->clientSocket, req->cgi->buf + 8, rd);
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
        fcgi_set_header(req, FCGI_STDIN);
        req->cgi->dir = OUT;
    }
    else if (req->cgi->dir == OUT)
    {
        int n = write(req->fcgi.fd, req->cgi->p, req->cgi->len_buf);
        if (n == -1)
        {
            print_err(req, "<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EAGAIN)
            {
                req->sock_timer = 0;
                return 0;
            }
            return -1;
        }

        req->cgi->p += n;
        req->cgi->len_buf -= n;
        if (req->cgi->len_buf == 0)
            req->cgi->dir = IN;
    }

    return 0;
}
//=====================================================================================================================
int fcgi_stdout(Connect *r)
{
    if (r->cgi->dir == IN)
    {
        int len = 0;
        if ((r->cgi->status == READ_CONTENT) || 
            (r->cgi->status == READ_ERROR) ||
            (r->cgi->status == FCGI_CLOSE))
        {
            len = (r->fcgi.dataLen > r->cgi->size_buf) ? r->cgi->size_buf : r->fcgi.dataLen;
            if (len == 0)
            {
                r->cgi->status = READ_PADDING;
                return 1;
            }
        }
        else if (r->cgi->status == READ_PADDING)
        {
            len = (r->fcgi.paddingLen > r->cgi->size_buf) ? r->cgi->size_buf : r->fcgi.paddingLen;
            if (len == 0)
            {
                r->cgi->status = FCGI_READ_HEADER;
                r->cgi->p = r->cgi->buf;
                r->cgi->len_buf = 0;
                return 1;
            }
        }

        r->cgi->len_buf = read(r->fcgi.fd, r->cgi->buf + 8, len);
        if (r->cgi->len_buf == -1)
        {
            print_err(r, "<%s:%d> Error read from script(fd=%d): %s(%d)\n", 
                    __func__, __LINE__, r->fcgi.fd, strerror(errno), errno);
            if (errno == EAGAIN)
            {
                r->sock_timer = 0;
                return -EAGAIN;
            }
            else
            {
                r->err = -1;
                cgi_del_from_list(r);
                end_response(r);
                return -1;
            }
        }
        else if (r->cgi->len_buf == 0)
        {
            r->err = -1;
            cgi_del_from_list(r);
            end_response(r);
            return -1;
        }
//fwrite(req->fcgi.buf + 8, 1, req->fcgi.len, stderr);
//fprintf(stderr, "\n");
        if (r->cgi->status == READ_CONTENT)
        {
            r->cgi->dir = OUT;
            r->fcgi.dataLen -= r->cgi->len_buf;
            if (r->mode_send == CHUNK)
            {
                if (cgi_set_size_chunk(r))
                    return -1;
            }
            else
                r->cgi->p = r->cgi->buf + 8;
        }
        else if (r->cgi->status == READ_PADDING)
        {
            r->fcgi.paddingLen -= r->cgi->len_buf;
            if (r->fcgi.paddingLen == 0)
            {
                r->cgi->status = FCGI_READ_HEADER;
                r->cgi->p = r->cgi->buf;
                r->cgi->len_buf = 0;
            }
        }
        else if (r->cgi->status == READ_ERROR)
        {
            r->fcgi.dataLen -= r->cgi->len_buf;
            if (r->fcgi.dataLen == 0)
            {
                r->cgi->status = READ_PADDING;
            }
            *(r->cgi->buf + 8 + r->cgi->len_buf) = 0;
            fprintf(stderr, "%s\n", r->cgi->buf + 8);
        }
        else if (r->cgi->status == FCGI_CLOSE)
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
                    r->cgi->len_buf = 0;
                    cgi_set_size_chunk(r);
                    r->cgi->dir = OUT;
                    r->mode_send = CHUNK_END;
                }
            }
        }

        return r->cgi->len_buf;
    }
    else if (r->cgi->dir == OUT)
    {
        int ret = write(r->clientSocket, r->cgi->p, r->cgi->len_buf);
        if (ret == -1)
        {
            print_err(r, "<%s:%d> Error send to client: %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EAGAIN)
            {
                r->sock_timer = 0;
                return -EAGAIN;
            }
            else
            {
                r->err = -1;
                cgi_del_from_list(r);
                end_response(r);
                return -1;
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
                    r->cgi->status = FCGI_READ_HEADER;
                    r->cgi->p = r->cgi->buf;
                    r->cgi->len_buf = 0;
                }
                else
                    r->cgi->status = READ_PADDING;
            }

            r->cgi->dir = IN;
        }
        return ret;
    }

    return 0;
}
//======================================================================
int fcgi_read_hdrs(Connect *r)
{
    int num_read = r->cgi->size_buf - r->cgi->len_buf - 1;
    if (num_read <= 0)
        return -RS505;
    num_read = (num_read > 256) ? 256 : num_read;
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
        r->cgi->status = SEND_HEADERS;
        r->fcgi.headers = true;
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
            print_err(r, "<%s:%d> Error write to fcgi: %s\n", __func__, __LINE__, strerror(errno));
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
int fcgi_read(Connect* r, int len)
{
    int ret = read(r->fcgi.fd, r->cgi->p, len);
    if (ret == -1)
    {
        if (errno == EAGAIN)
        {
            r->sock_timer = 0;
            return 0;
        }
        else
        {
            print_err(r, "<%s:%d> Error read from fcgi: %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
    }
    else
    {
        r->cgi->len_buf += ret;
        r->cgi->p += ret;
        r->sock_timer = 0;
    }

    return ret;
}
//======================================================================
int fcgi_read_header(Connect* r)
{
    int n = 0;

    if (r->cgi->len_buf < 8)
    {
        int len = 8 - r->cgi->len_buf;
        n = fcgi_read(r, len);
        if (n <= 0)
        {
            if (n == 0)
                print_err(r, "<%s:%d> Error read from fcgi: read()=0\n", __func__, __LINE__);
            r->err = -1;
            cgi_del_from_list(r);
            end_response(r);
            return n;
        }
    }

    r->fcgi.fcgi_type = (unsigned char)r->cgi->buf[1];
    r->fcgi.paddingLen = (unsigned char)r->cgi->buf[6];
    r->fcgi.dataLen = ((unsigned char)r->cgi->buf[4]<<8) | (unsigned char)r->cgi->buf[5];
    r->cgi->status = READ_CONTENT;

    return r->cgi->len_buf;
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
            fcgi_sock = -RS502;
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
    if (r->operation == CGI_CONNECT)
    {
        if (fcgi_create_connect(r))
        {
            r->err = -RS502;
            cgi_del_from_list(r);
            end_response(r);
            return;
        }
        fcgi_create_param(r);
    }

    if (r->operation == FCGI_BEGIN)
    {
        cgi_poll_fd[*i].fd = r->fcgi.fd;
        cgi_poll_fd[*i].events = POLLOUT;
    }
    else if (r->operation == CGI_PARAMS)
    {
        if (r->cgi->dir == IN)
        {
            fcgi_set_param(r);
        }

        cgi_poll_fd[*i].fd = r->fcgi.fd;
        cgi_poll_fd[*i].events = POLLOUT;
    }
    else if (r->operation == CGI_END_PARAMS)
    {
        cgi_poll_fd[*i].fd = r->fcgi.fd;
        cgi_poll_fd[*i].events = POLLOUT;
    }
    else if (r->operation == CGI_STDIN)
    {
        if (r->lenTail > 0)
        {
            if (r->cgi->dir == IN)
            {
                if (r->lenTail > r->cgi->size_buf)
                    r->cgi->len_buf = r->cgi->size_buf;
                else
                    r->cgi->len_buf = r->lenTail;
                memcpy(r->cgi->buf + 8, r->tail, r->cgi->len_buf);
                r->lenTail -= r->cgi->len_buf;
                if (r->lenTail == 0)
                    r->tail = NULL;
                else
                    r->tail += r->cgi->len_buf;

                fcgi_set_header(r, FCGI_STDIN);
                r->cgi->dir = OUT;
            }
            cgi_poll_fd[*i].fd = r->fcgi.fd;
            cgi_poll_fd[*i].events = POLLOUT;
        }
        else if (r->cgi->dir == IN)
        {
            if (r->cgi->len_post <= 0)
            {
                r->operation = CGI_END_STDIN;
                r->cgi->len_buf = 0;
                fcgi_set_header(r, FCGI_STDIN);
                r->cgi->dir = OUT;
            }

            cgi_poll_fd[*i].fd = r->clientSocket;
            cgi_poll_fd[*i].events = POLLIN;
        }
        else if (r->cgi->dir == OUT)
        {
            cgi_poll_fd[*i].fd = r->fcgi.fd;
            cgi_poll_fd[*i].events = POLLOUT;
        }
    }
    else if (r->operation == CGI_END_STDIN)
    {
        r->cgi->dir = OUT;
        cgi_poll_fd[*i].fd = r->fcgi.fd;
        cgi_poll_fd[*i].events = POLLOUT;
    }
    else if (r->operation == CGI_STDOUT)//===================================================
    {
        if (r->cgi->status == FCGI_READ_HEADER)
        {
            cgi_poll_fd[*i].fd = r->fcgi.fd;
            cgi_poll_fd[*i].events = POLLIN;
        }
        else if (r->cgi->status == READ_HEADERS)
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
                    {
                        r->cgi->p = r->cgi->buf + 8;
                    }
                    r->cgi->dir = OUT;

                    cgi_poll_fd[*i].fd = r->clientSocket;
                    cgi_poll_fd[*i].events = POLLOUT;
                }
            }
            else
            {
                if (r->cgi->dir == IN)
                {
                    if (r->fcgi.dataLen == 0)
                    {
                        if (r->fcgi.paddingLen == 0)
                        {
                            r->cgi->status = FCGI_READ_HEADER;
                            r->cgi->p = r->cgi->buf;
                            r->cgi->len_buf = 0;
                        }
                        else
                        {
                            r->cgi->status = READ_PADDING;
                            r->cgi->dir = IN;
                        }
                    }
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
        else if (r->cgi->status == READ_ERROR)
        {
            cgi_poll_fd[*i].fd = r->fcgi.fd;
            cgi_poll_fd[*i].events = POLLIN;
        }
        else if (r->cgi->status == READ_PADDING)
        {
            cgi_poll_fd[*i].fd = r->fcgi.fd;
            cgi_poll_fd[*i].events = POLLIN;
        }
        else if (r->cgi->status == FCGI_CLOSE)
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
    }
    (*i)++;
}
//======================================================================
void fcgi_(Connect* r)
{
    if (r->operation == FCGI_BEGIN)
    {
        int ret = write_to_fcgi(r);
        if (ret > 0)
        {
            if (r->cgi->len_buf == 0)
            {
                r->operation = CGI_PARAMS;
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
    else if (r->operation == CGI_PARAMS)
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
    else if (r->operation == CGI_END_PARAMS)
    {
        int ret = write_to_fcgi(r);
        if (ret > 0)
        {
            if (r->cgi->len_buf == 0)
            {
                r->cgi->len_post = r->req_hd.reqContentLength - r->lenTail;
                r->operation = CGI_STDIN;
                r->cgi->dir = IN;
            }
        }
        else if (ret == -1)
        {
            r->err = -1;
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else if (r->operation == CGI_STDIN)
    {
        int n = fcgi_stdin(r);
        if (n < 0)
        {
            print_err(r, "<%s:%d> Error cgi_stdin\n", __func__, __LINE__);
            r->err = -RS502;
            cgi_del_from_list(r);
            end_response(r);
        }
    }
    else if (r->operation == CGI_END_STDIN)
    {
        int n = fcgi_stdin(r);
        if (n < 0)
        {
            print_err(r, "<%s:%d> Error cgi_stdin\n", __func__, __LINE__);
            r->err = -RS502;
            cgi_del_from_list(r);
            end_response(r);
        }
        else
        {
            if (r->cgi->len_buf == 0)
            {
                r->operation = CGI_STDOUT;
                r->fcgi.headers = false;
                r->cgi->status = FCGI_READ_HEADER;
                r->cgi->p = r->cgi->buf;
                r->cgi->len_buf = 0;
            }
        }
    }
    else if (r->operation == CGI_STDOUT)//==================================================================
    {
        if (r->cgi->status == FCGI_READ_HEADER)
        {
            int ret = fcgi_read_header(r);
            if (ret == 8)
            {
                switch (r->fcgi.fcgi_type)
                {
                    case FCGI_STDOUT:
                    {
                        if (r->fcgi.headers == false)
                        {
                            r->cgi->status = READ_HEADERS;
                            r->tail = NULL;
                            r->p_newline = r->cgi->p = r->cgi->buf;
                            r->cgi->len_buf = 0;
                        }
                        else
                        {
                            if (r->fcgi.dataLen > 0)
                            {
                                r->cgi->status = READ_CONTENT;
                            }
                            else
                                r->cgi->status = READ_PADDING;
                        }
                    }
                    break;
                    case FCGI_STDERR:
                        r->cgi->status = READ_ERROR;
                        break;
                    case FCGI_END_REQUEST:
                    case FCGI_ABORT_REQUEST:
                        r->cgi->status = FCGI_CLOSE;
                        break;
                    default:
                        print_err(r, "<%s:%d> Error type=%d\n", __func__, __LINE__, r->fcgi.fcgi_type);
                        r->err = -1;
                        cgi_del_from_list(r);
                        end_response(r);
                }
            }
        }
        else if (r->cgi->status == READ_HEADERS)
        {
            if (fcgi_read_hdrs(r) > 0)
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
        }
        else if (r->cgi->status == SEND_HEADERS)
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
                            r->cgi->status = READ_CONTENT;
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
                                r->cgi->dir = OUT;
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
                                r->cgi->dir = IN;
                        }
                    }
                    else
                        r->sock_timer = 0;
                }
            }
        }
        else if (r->cgi->status == READ_CONTENT)
        {
            fcgi_stdout(r);
        }
        else if ((r->cgi->status == READ_ERROR) ||
                 (r->cgi->status == READ_PADDING))
        {
            r->cgi->dir = IN;
            fcgi_stdout(r);
        }
        else if (r->cgi->status == FCGI_CLOSE)
        {
            if (r->mode_send == CHUNK_END)
            {
                r->cgi->dir = OUT;
                fcgi_stdout(r);
                if (r->cgi->len_buf == 0)
                {
                    cgi_del_from_list(r);
                    end_response(r);
                }
            }
            else
            {
                r->cgi->dir = IN;
                fcgi_stdout(r);
            }
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
