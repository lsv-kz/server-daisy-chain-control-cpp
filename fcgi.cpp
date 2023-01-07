#include "classes.h"
#include "fcgi.h"

using namespace std;
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
int fcgi_read_headers(FCGI_client& Fcgi, String *hdrs, int *stat)
{
    char *p;

    while (1)
    {
        int hd_len = Fcgi.fcgi_read_http_header(&p);
        if (hd_len < 0)
        {
            print_err("<%s:%d> fcgi_read_http_header()=%d\n", __func__, __LINE__, hd_len);
            return -1;
        }
        
        if (hd_len == 0)
            break;
//fwrite(p, 1, n, stderr);
//fprintf(stderr, "\n");
        if (!strncmp("Status", p, 6))
            *stat = atoi(p + 7);
        else
            *(hdrs) << p << "\r\n";
    }

    return 0;
}
//======================================================================
int fcgi_(Connect *req, int fcgi_sock, FCGI_client & Fcgi)
{
    if (req->reqMethod == M_POST)
    {
        if (req->tail)
        {
            int err = Fcgi.send_to_fcgi_server(req->tail, req->lenTail);
            if (err)
            {
                return -RS502;
            }

            req->req_hd.reqContentLength -= req->lenTail;
        }

        while (req->req_hd.reqContentLength > 0)
        {
            char buf[4096];
            int rd = (req->req_hd.reqContentLength > (long long)sizeof(buf)) ? sizeof(buf) : (int)req->req_hd.reqContentLength;
            int ret = read_from_client(req, buf, rd, conf->Timeout);
            if (ret < 0)
            {
                print_err(req, "<%s:%d> Error: reaf_from_client()\n", __func__, __LINE__);
                return ret;
            }

            int err = Fcgi.send_to_fcgi_server(buf, ret);
            if (err)
                return -RS502;

            req->req_hd.reqContentLength -= ret;
        }
    }

    // End FCGI_STDIN
    if (Fcgi.send_to_fcgi_server(NULL, 0))
    {
        print_err(req, "<%s:%d> Error: End FCGI_STDIN\n", __func__, __LINE__);
        return -RS502;
    }

    String hdrs(256);
    if (hdrs.error())
    {
        fprintf(stderr, "<%s:%d> Error create String object\n", __func__, __LINE__);
        return -RS500;
    }

    int chunk_mode;
    if (req->reqMethod == M_HEAD)
        chunk_mode = NO_SEND;
    else
        chunk_mode = ((req->httpProt == HTTP11) && req->connKeepAlive) ? SEND_CHUNK : SEND_NO_CHUNK;

    ClChunked chunk(req, chunk_mode);

    if (chunk_mode == SEND_CHUNK)
        hdrs << "Transfer-Encoding: chunked\r\n";

    req->respStatus = RS200;

    char *p;

    int ret = fcgi_read_headers(Fcgi, &hdrs, &req->respStatus);
    if (ret < 0)
    {
        fprintf(stderr, "<%s:%d> Error fcgi_read_headers()=%d\n", __func__, __LINE__, ret);
        return -RS502;
    }
    
    if (create_response_headers(req, &hdrs) == -1)
        return -1;
    if (write_to_client(req, req->resp.s.c_str(), req->resp.s.size(), conf->Timeout) < 0)
    {
        print_err(req, "<%s:%d> Sent to client response error\n", __func__, __LINE__);
        req->req_hd.iReferer = MAX_HEADERS - 1;
        req->reqHdValue[req->req_hd.iReferer] = "Error send response headers";
            return -1;
    }

    if (req->respStatus == RS204)
        return 0;

    while (1)
    {
        int n = Fcgi.read_from_fcgi_server(&p);
        if (n < 0)
        {
            fprintf(stderr, "<%s:%d> Error Fcgi.read_from_server()\n", __func__, __LINE__);
            return -1;
        }
        else if (n == 0)
            break;

        *(p + n) = 0;
        chunk.add_arr(p, n);
    }

    ret = chunk.end();
    req->respContentLength = chunk.len_entity();
    if (ret < 0)
    {
        print_err(req, "<%s:%d> Error chunk.end(): %d\n", __func__, __LINE__, ret);
        return -1;
    }

    if (chunk_mode == NO_SEND)
    {
        if (create_response_headers(req, &hdrs))
        {
            print_err("<%s:%d> Error send_header_response()\n", __func__, __LINE__);
            return -1;
        }

        if (write_to_client(req, req->resp.s.c_str(), req->resp.s.size(), conf->Timeout) < 0)
        {
            print_err(req, "<%s:%d> Sent to client response error\n", __func__, __LINE__);
            req->req_hd.iReferer = MAX_HEADERS - 1;
            req->reqHdValue[req->req_hd.iReferer] = "Error send response headers";
            return -1;
        }
    }
    else
        req->send_bytes = req->respContentLength;

    return 0;
}
//======================================================================
int fcgi_send_param(Connect *req, int fcgi_sock)
{
    FCGI_client Fcgi(fcgi_sock, conf->TimeoutCGI);
    if (req->scriptType == php_fpm)
        Fcgi.param("REDIRECT_STATUS", "true");
    Fcgi.param("PATH", "/bin:/usr/bin:/usr/local/bin");
    Fcgi.param("SERVER_SOFTWARE", conf->ServerSoftware.c_str());
    Fcgi.param("GATEWAY_INTERFACE", "CGI/1.1");
    Fcgi.param("DOCUMENT_ROOT", conf->DocumentRoot.c_str());
    Fcgi.param("REMOTE_ADDR", req->remoteAddr);
    Fcgi.param("REMOTE_PORT", req->remotePort);
    Fcgi.param("REQUEST_URI", req->uri);

    if (req->reqMethod == M_HEAD)
        Fcgi.param("REQUEST_METHOD", get_str_method(M_GET));
    else
        Fcgi.param("REQUEST_METHOD", get_str_method(req->reqMethod));

    Fcgi.param("SERVER_PROTOCOL", get_str_http_prot(req->httpProt));

    if (req->req_hd.iHost >= 0)
        Fcgi.param("HTTP_HOST", req->reqHdValue[req->req_hd.iHost]);

    if (req->req_hd.iReferer >= 0)
        Fcgi.param("HTTP_REFERER", req->reqHdValue[req->req_hd.iReferer]);

    if (req->req_hd.iUserAgent >= 0)
        Fcgi.param("HTTP_USER_AGENT", req->reqHdValue[req->req_hd.iUserAgent]);

    Fcgi.param("SCRIPT_NAME", req->decodeUri);

    if (req->scriptType == php_fpm)
    {
        String s;
        s << conf->DocumentRoot << req->scriptName;
        Fcgi.param("SCRIPT_FILENAME", s.c_str());
    }

    if (req->reqMethod == M_POST)
    {
        if (req->req_hd.iReqContentType >= 0)
        {
            Fcgi.param("CONTENT_TYPE", req->reqHdValue[req->req_hd.iReqContentType]);
            //print_err(req, "<%s:%d> %s\n", __func__, __LINE__, req->reqHdValue[req->req_hd.iReqContentType]);
        }

        if (req->req_hd.iReqContentLength >= 0)
            Fcgi.param("CONTENT_LENGTH", req->reqHdValue[req->req_hd.iReqContentLength]);
    }

    Fcgi.param("QUERY_STRING", req->sReqParam);

    Fcgi.param(NULL, 0); // End FCGI_PARAMS
    if (Fcgi.error())
    {
        print_err(req, "<%s:%d> Error send_param()\n", __func__, __LINE__);
        return -RS502;
    }

    int ret = fcgi_(req, fcgi_sock, Fcgi);

    return ret;
}
//======================================================================
int fcgi(Connect *req)
{
    int  sock_fcgi, ret = 0;

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

    if (timedwait_close_cgi())
        return -1;

    if (req->scriptType == php_fpm)
        sock_fcgi = create_fcgi_socket(conf->PathPHP.c_str());
    else if (req->scriptType == fast_cgi)
        sock_fcgi = get_sock_fcgi(req, req->scriptName);
    else
    {
        print_err(req, "<%s:%d> req->scriptType ?\n", __func__, __LINE__);
        ret = -RS500;
        goto err_exit;
    }

    if (sock_fcgi < 0)
    {
        ret = sock_fcgi;
        goto err_exit;
    }

    ret = fcgi_send_param(req, sock_fcgi);

    close(sock_fcgi);

err_exit:
    cgi_dec();
    if (ret < 0)
        req->connKeepAlive = 0;
    return ret;
}
