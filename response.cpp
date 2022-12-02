#include "classes.h"

using namespace std;
//======================================================================
void response1(RequestManager *ReqMan)
{
    const char *p;
    Connect *req;

    while (1)
    {
        req = ReqMan->pop_resp_list();
        if (!req)
        {
            ReqMan->end_thr(1);
            return;
        }
        else if (req->clientSocket < 0)
        {
            ReqMan->end_thr(1);
            delete req;
            return;
        }
        //--------------------------------------------------------------
        int ret = parse_startline_request(req, req->reqHdName[0]);
        if (ret)
        {
            print_err(req, "<%s:%d>  Error parse_startline_request(): %d\n", __func__, __LINE__, ret);
            goto end;
        }

        for (int i = 1; i < req->countReqHeaders; ++i)
        {
            ret = parse_headers(req, req->reqHdName[i], i);
            if (ret < 0)
            {
                print_err(req, "<%s:%d>  Error parse_headers(): %d\n", __func__, __LINE__, ret);
                goto end;
            }
        }
    #ifdef TCP_CORK_
        if (conf->tcp_cork == 'y')
        {
        #if defined(LINUX_)
            int optval = 1;
            setsockopt(req->clientSocket, SOL_TCP, TCP_CORK, &optval, sizeof(optval));
        #elif defined(FREEBSD_)
            int optval = 1;
            setsockopt(req->clientSocket, IPPROTO_TCP, TCP_NOPUSH, &optval, sizeof(optval));
        #endif
        }
    #endif
        //--------------------------------------------------------------
        if ((req->httpProt != HTTP10) && (req->httpProt != HTTP11))
        {
            req->err = -RS505;
            goto end;
        }

        if (req->numReq >= (unsigned int)conf->MaxRequestsPerClient || (req->httpProt == HTTP10))
            req->connKeepAlive = 0;
        else if (req->req_hd.iConnection == -1)
            req->connKeepAlive = 1;

        if ((p = strchr(req->uri, '?')))
        {
            req->uriLen = p - req->uri;
            req->sReqParam = p + 1;
        }
        else
            req->sReqParam = NULL;

        if (decode(req->uri, req->uriLen, req->decodeUri, sizeof(req->decodeUri)) <= 0)
        {
            print_err(req, "<%s:%d> Error: decode URI\n", __func__, __LINE__);
            req->err = -RS404;
            goto end;
        }

        clean_path(req->decodeUri);
        req->lenDecodeUri = strlen(req->decodeUri);

        if (strstr(req->uri, ".php") && (conf->UsePHP != "php-cgi") && (conf->UsePHP != "php-fpm"))
        {
            print_err(req, "<%s:%d> Error UsePHP=%s\n", __func__, __LINE__, conf->UsePHP.c_str());
            req->err = -RS404;
            goto end;
        }

        if (req->req_hd.iUpgrade >= 0)
        {
            print_err(req, "<%s:%d> req->upgrade: %s\n", __func__, __LINE__, req->reqHdValue[req->req_hd.iUpgrade]);
            req->err = -RS505;
            goto end;
        }
        //--------------------------------------------------------------
        if ((req->reqMethod == M_GET) || (req->reqMethod == M_HEAD) || (req->reqMethod == M_POST))
        {
            int ret = response2(req);
            if (ret == 1)
            {// "req" may be free !!!
                ret = ReqMan->end_thr(0);
                if (ret == EXIT_THR)
                    return;
                else
                    continue;
            }

            req->err = ret;
        }
        else if (req->reqMethod == M_OPTIONS)
            req->err = options(req);
        else
            req->err = -RS501;

    end:
        end_response(req);

        ret = ReqMan->end_thr(0);
        if (ret)
        {
            return;
        }
    }
}
//======================================================================
int send_file(Connect *req);
int send_multypart(Connect *req, ArrayRanges& rg, char *rd_buf, int size);
int create_multipart_head(Connect *req, Range *ranges, char *buf, int len_buf);
const char boundary[] = "---------a9b5r7a4c0a2d5a1b8r3a";
//======================================================================
long long file_size(const char *s)
{
    struct stat st;

    if (!stat(s, &st))
        return st.st_size;
    else
        return -1;
}
//======================================================================
int fastcgi(Connect* req, const char* uri)
{
    const char* p = strrchr(uri, '/');
    if (!p)
        return -RS404;

    fcgi_list_addr* i = conf->fcgi_list;
    for (; i; i = i->next)
    {
        if (i->script_name[0] == '~')
        {
            if (!strcmp(p, i->script_name.c_str() + 1))
                break;
        }
        else
        {
            if (uri == i->script_name)
                break;
        }
    }

    if (!i)
        return -RS404;

    int ret;
    if (i->type == fast_cgi)
    {
        req->scriptType = fast_cgi;
        req->scriptName = i->script_name.c_str();
        ret = fcgi(req);
        req->scriptName = NULL;
    }
    else if (i->type == s_cgi)
    {
        req->scriptType = s_cgi;
        req->scriptName = i->script_name.c_str();
        ret = scgi(req);
        req->scriptName = NULL;
    }
    else
        ret = -1;
    return ret;
}
//======================================================================
int response2(Connect *req)
{
    struct stat st;
    char *p = strstr(req->decodeUri, ".php");
    if (p && (*(p + 4) == 0))
    {
        int ret;
        if ((conf->UsePHP != "php-cgi") && (conf->UsePHP != "php-fpm"))
        {
            print_err(req, "<%s:%d> Not found: %s\n", __func__, __LINE__, req->decodeUri);
            return -RS404;
        }

        struct stat st;
        if (stat(req->decodeUri + 1, &st) == -1)
        {
            print_err(req, "<%s:%d> script (%s) not found\n", __func__, __LINE__, req->decodeUri);
            return -RS404;
        }

        req->scriptName = req->decodeUri;

        if (conf->UsePHP == "php-fpm")
        {
            req->scriptType = php_fpm;
            ret = fcgi(req);
        }
        else if (conf->UsePHP == "php-cgi")
        {
            req->scriptType = php_cgi;
            ret = cgi(req);
        }
        else
            ret = -1;

        req->scriptName = NULL;
        return ret;
    }

    if (!strncmp(req->decodeUri, "/cgi-bin/", 9) || !strncmp(req->decodeUri, "/cgi/", 5))
    {
        int ret;
        req->scriptType = cgi_ex;
        req->scriptName = req->decodeUri;
        ret = cgi(req);
        return ret;
    }
    //------------------------------------------------------------------
    string path;
//  path.reserve(req->conf->DocumentRoot.size() + req->lenDecodeUri + 256);
//  path = req->conf->DocumentRoot;

    path.reserve(1 + req->lenDecodeUri + 16);
    path += '.';
    path += req->decodeUri;
    if (path[path.size()-1] == '/')
        path.resize(path.size() - 1);

    if (lstat(path.c_str(), &st) == -1)
    {
        if (errno == EACCES)
            return -RS403;
        return fastcgi(req, req->decodeUri);
    }
    else
    {
        if ((!S_ISDIR(st.st_mode)) && (!S_ISREG(st.st_mode)))
        {
            print_err(req, "<%s:%d> Error: file (!S_ISDIR && !S_ISREG) \n", __func__, __LINE__);
            return -RS403;
        }
    }
    //------------------------------------------------------------------
    if (S_ISDIR(st.st_mode))
    {
        if (req->reqMethod == M_POST)
            return -RS404;

        if (req->uri[req->uriLen - 1] != '/')
        {
            req->uri[req->uriLen] = '/';
            req->uri[req->uriLen + 1] = '\0';
            req->respStatus = RS301;

            String hdrs(127);
            hdrs << "Location: " << req->uri << "\r\n";
            if (hdrs.error())
            {
                print_err(req, "<%s:%d> Error\n", __func__, __LINE__);
                return -RS500;
            }

            String s(256);
            s << "The document has moved <a href=\"" << req->uri << "\">here</a>.";
            if (s.error())
            {
                print_err(req, "<%s:%d> Error create_header()\n", __func__, __LINE__);
                return -RS500;
            }

            send_message(req, s.c_str(), &hdrs);
            return 0;
        }
        //--------------------------------------------------------------
        int len = path.size();
        path += "/index.html";
        if ((stat(path.c_str(), &st) != 0) || (conf->index_html != 'y'))
        {
            errno = 0;
            path.resize(len);

            int ret;
            if ((conf->UsePHP != "n") && (conf->index_php == 'y'))
            {
                path += "/index.php";
                if (!stat(path.c_str(), &st))
                {
                    string s = req->decodeUri;
                    s += "index.php";
                    req->scriptName = s.c_str();

                    if (conf->UsePHP == "php-fpm")
                    {
                        req->scriptType = php_fpm;
                        ret = fcgi(req);
                    }
                    else if (conf->UsePHP == "php-cgi")
                    {
                        req->scriptType = php_cgi;
                        ret = cgi(req);
                    }
                    else
                        ret = -1;

                    req->scriptName = NULL;
                    return ret;
                }
                path.resize(len);
            }

            if (conf->index_pl == 'y')
            {
                req->scriptType = cgi_ex;
                req->scriptName = "/cgi-bin/index.pl";
                ret = cgi(req);
                req->scriptName = NULL;
                if (ret == 0)
                    return ret;
            }
            else if (conf->index_fcgi == 'y')
            {
                req->scriptType = fast_cgi;
                req->scriptName = "/index.fcgi";
                ret = fcgi(req);
                req->scriptName = NULL;
                if (ret == 0)
                    return ret;
            }

            path.reserve(path.capacity() + 256);
            if (conf->AutoIndex == 'y')
                return index_dir(req, path);
            else
                return -RS403;
        }
    }
    //--------------------- send file ----------------------------------
    req->fileSize = file_size(path.c_str());
    req->numPart = 0;
    req->respContentType = content_type(path.c_str());
    //------------------------------------------------------------------
    req->fd = open(path.c_str(), O_RDONLY);
    if (req->fd == -1)
    {
        if (errno == EACCES)
            return -RS403;
        else
        {
            print_err(req, "<%s:%d> Error open(%s): %s\n", __func__, __LINE__, 
                                    path.c_str(), strerror(errno));
            return -RS500;
        }
    }
    path.reserve(0);

    int ret = send_file(req);
    if (ret != 1)
        close(req->fd);

    return ret;
}
//======================================================================
int send_file(Connect *req)
{
    if (req->req_hd.iRange >= 0)
    {
        int err;

        ArrayRanges rg(req->sRange, req->fileSize);
        if ((err = rg.error()))
        {
            print_err(req, "<%s:%d> Error create_ranges\n", __func__, __LINE__);
            return err;
        }

        req->numPart = rg.size();
        req->respStatus = RS206;
        if (req->numPart > 1)
        {
            int size = conf->SndBufSize;
            char *rd_buf = new(nothrow) char [size];
            if (!rd_buf)
            {
                print_err(req, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
                return -1;
            }

            int n = send_multypart(req, rg, rd_buf, size);
            delete [] rd_buf;
            return n;
        }
        else if (req->numPart == 1)
        {
            Range *pr = rg.get(0);
            if (pr)
            {
                req->offset = pr->start;
                req->respContentLength = pr->len;
                if (req->reqMethod == M_HEAD)
                {
                    if (send_response_headers(req, NULL))
                        return -1;
                    return 0;
                }
            }
            else
                return -RS500;
        }
        else
        {
            print_err(req, "<%s:%d> ???\n", __func__, __LINE__);
            exit(1);
            return -RS416;
        }
    }
    else
    {
        req->respStatus = RS200;
        req->offset = 0;
        req->respContentLength = req->fileSize;
        if (req->reqMethod == M_HEAD)
        {
            if (send_response_headers(req, NULL))
                return -1;
            return 0;
        }
    }

    if (send_response_headers(req, NULL))
        return -1;
    push_pollout_list(req);

    return 1;
}
//======================================================================
int send_multypart(Connect *req, ArrayRanges& rg, char *rd_buf, int size)
{
    int n;
    long long send_all_bytes = 0;
    char buf[1024];
    Range *range;

    for (int i = 0; (range = rg.get(i)) && (i < req->numPart); ++i)
    {
        send_all_bytes += (range->len);
        send_all_bytes += create_multipart_head(req, range, buf, sizeof(buf));
    }
    send_all_bytes += snprintf(buf, sizeof(buf), "\r\n--%s--\r\n", boundary);
    req->respContentLength = send_all_bytes;

    String hdrs(256);
    hdrs << "Content-Type: multipart/byteranges; boundary=" << boundary << "\r\n";
    hdrs << "Content-Length: " << send_all_bytes << "\r\n";
    if (hdrs.error())
    {
        print_err(req, "<%s:%d> Error create response headers\n", __func__, __LINE__);
        return -1;
    }

    if (send_response_headers(req, &hdrs))
        return -1;
    if (req->reqMethod == M_HEAD)
        return 0;

    send_all_bytes = 0;

    for (int i = 0; (range = rg.get(i)) && (i < req->numPart); ++i)
    {
        if ((n = create_multipart_head(req, range, buf, sizeof(buf))) == 0)
        {
            print_err(req, "<%s:%d> Error create_multipart_head()=%d\n", __func__, __LINE__, n);
            return -1;
        } 

        n = write_timeout(req->clientSocket, buf, strlen(buf), conf->Timeout);
        if (n < 0)
        {
            print_err(req, "<%s:%d> Error: write_timeout(), %lld bytes\n", __func__, __LINE__, send_all_bytes);
            return -1;
        }

        send_all_bytes += n;
        send_all_bytes += range->len;

        if (send_largefile(req, rd_buf, size, range->start, &range->len))
        {
            print_err(req, "<%s:%d> Error: send_file_ux()\n", __func__, __LINE__);
            return -1;
        }
    }

    req->send_bytes = send_all_bytes;
    snprintf(buf, sizeof(buf), "\r\n--%s--\r\n", boundary);
    n = write_timeout(req->clientSocket, buf, strlen(buf), conf->Timeout);
    if (n < 0)
    {
        print_err(req, "<%s:%d> Error: write_timeout() %lld bytes\n", __func__, __LINE__, send_all_bytes);
        return -1;
    }
    req->send_bytes += n;
    return 0;
}
//======================================================================
int create_multipart_head(Connect *req, struct Range *ranges, char *buf, int len_buf)
{
    int n, all = 0;

    n = snprintf(buf, len_buf, "\r\n--%s\r\n", boundary);
    buf += n;
    len_buf -= n;
    all += n;

    if (req->respContentType && (len_buf > 0))
    {
        n = snprintf(buf, len_buf, "Content-Type: %s\r\n", req->respContentType);
        buf += n;
        len_buf -= n;
        all += n;
    }
    else
        return 0;

    if (len_buf > 0)
    {
        n = snprintf(buf, len_buf,
            "Content-Range: bytes %lld-%lld/%lld\r\n\r\n",
            ranges->start, ranges->end, req->fileSize);
        buf += n;
        len_buf -= n;
        all += n;
    }
    else
        return 0;

    return all;
}
//======================================================================
int options(Connect *req)
{
    req->respStatus = RS200;
    if (send_response_headers(req, NULL))
    {
        return -1;
    }

    return 0;
}
