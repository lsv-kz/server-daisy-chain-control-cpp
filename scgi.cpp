#include "main.h"
#include "scgi.h"

using namespace std;

int cgi_read_headers(Connect *req, int scgi_sock);
//======================================================================
int scgi_send_param(Connect *req, int scgi_sock)
{
    SCGI_client Scgi(scgi_sock, conf->TimeoutCGI);
    
    if (req->reqMethod == M_POST)
        Scgi.add("CONTENT_LENGTH", req->reqHdValue[req->req_hd.iReqContentLength]);
    else
    {
        Scgi.add("CONTENT_LENGTH", "0");
    }

    Scgi.add("REQUEST_METHOD", get_str_method(req->reqMethod));
    Scgi.add("REQUEST_URI", req->uri);

    if (req->reqMethod == M_POST)
    {
        Scgi.add("QUERY_STRING", NULL);
        Scgi.add("CONTENT_TYPE", req->reqHdValue[req->req_hd.iReqContentType]);
    }
    else if (req->reqMethod == M_GET)
    {
        Scgi.add("QUERY_STRING", req->sReqParam);
        Scgi.add("CONTENT_TYPE", NULL);
    }

    char *p = strchr(req->uri, '?');
    if (p)
        Scgi.add("DOCUMENT_URI", req->uri, p - req->uri);
    else
        Scgi.add("DOCUMENT_URI", req->uri);
    
    Scgi.add("DOCUMENT_ROOT", conf->DocumentRoot.c_str());
    Scgi.add("SCGI", "1");
    
    if (Scgi.error())
    {
        fprintf(stderr, "<%s:%d> Error send_param()\n", __func__, __LINE__);
        return -1;
    }
    
    Scgi.send_headers();
    
    if (req->reqMethod == M_POST)
    {
        if (req->tail)
        {
            int ret = Scgi.scgi_send(req->tail, req->lenTail);
            if (ret < 0)
            {
                print_err(req, "<%s:%d> Error scgi_send()\n", __func__, __LINE__);
                return -1;
            }
            req->req_hd.reqContentLength -= ret;
        }
        
        while (req->req_hd.reqContentLength > 0)
        {
            int rd;
            char buf[4096];
            if (req->req_hd.reqContentLength >= (long long)sizeof(buf))
                rd = sizeof(buf);
            else
                rd = (int)req->req_hd.reqContentLength;
            int ret = read_timeout(req->clientSocket, buf, rd, conf->Timeout);
            if (ret <= 0)
            {
                print_err(req, "<%s:%d> Error read_timeout()\n", __func__, __LINE__);
                return -1;
            }
            
            req->req_hd.reqContentLength -= ret;
            ret = Scgi.scgi_send(buf, ret);
            if (ret < 0)
            {
                print_err(req, "<%s:%d> Error scgi_send()\n", __func__, __LINE__);
                return -1;
            }
        }
    }
    
    int ret = cgi_read_headers(req, scgi_sock);
    return ret;
}
//======================================================================
int get_sock_fcgi(Connect *req, const char *script);
//======================================================================
int scgi(Connect *req)
{
    int  sock_scgi;
    
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
            if (req->req_hd.reqContentLength < 50000000)
            {
                if (req->tail)
                    req->req_hd.reqContentLength -= req->lenTail;
                client_to_cosmos(req, &req->req_hd.reqContentLength);
                if (req->req_hd.reqContentLength == 0)
                    return -RS413;
            }
            return -1;
        }
    }

    if (timedwait_close_cgi())
    {
        return -1;
    }

    sock_scgi = get_sock_fcgi(req, req->scriptName);
    if (sock_scgi <= 0)
    {
        print_err(req, "<%s:%d> Error connect to scgi\n", __func__, __LINE__);
        cgi_dec();
        if (sock_scgi == 0)
            return -RS400;
        else
            return -RS502;
    }
    
    int ret = scgi_send_param(req, sock_scgi);
    close(sock_scgi);

    cgi_dec();

    if (ret < 0)
        req->connKeepAlive = 0;
    return 0;
}
