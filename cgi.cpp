#include "classes.h"

using namespace std;

mutex mtx_chld;
condition_variable condCloseCGI;
unsigned int num_cgi_chlds = 0;
//======================================================================
int timedwait_close_cgi()
{
    int ret = 0;
unique_lock<mutex> lk(mtx_chld);
    while (num_cgi_chlds >= conf->MaxCgiProc)
    {
        if (condCloseCGI.wait_for(lk, chrono::milliseconds(1000 * conf->TimeoutCGI)) == cv_status::timeout)
        {
            print_err("<%s:%d> Timeout: %d s\n", __func__, __LINE__, conf->TimeoutCGI);
            ret = -RS503;
        }
    }

    if (ret == 0)
        ++num_cgi_chlds;

    return ret;
}
//======================================================================
void cgi_dec()
{
mtx_chld.lock();
    --num_cgi_chlds;
mtx_chld.unlock();
    condCloseCGI.notify_one();
}
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
int wait_pid(Connect *req, int pid)
{
    int n = waitpid(pid, NULL, WNOHANG); // no blocking
    if (n == -1)
    {
        print_err(req, "<%s:%d> Error waitpid(%d): %s\n", __func__, __LINE__, pid, strerror(errno));
        req->err = -1;
    }
    else if (n == 0)
    {
        if (kill(pid, SIGKILL) == 0)
            waitpid(pid, NULL, 0);
        else
        {
            print_err(req, "<%s:%d> Error kill(%d): %s\n", __func__, __LINE__, pid, strerror(errno));
            req->err = -1;
        }
    }

    return req->err;
}
//======================================================================
void kill_script(int pid)
{
    if (kill(pid, SIGKILL) == 0)
        waitpid(pid, NULL, 0);
    else
        print_err("<%s:%d> Error kill(%d): %s\n", __func__, __LINE__, pid, strerror(errno));
}
//======================================================================
int cgi_chunk(Connect *req, String *hdrs, int cgi_serv_in, char *tail_ptr, int tail_len)
{
    int chunk;
    if (req->reqMethod == M_HEAD)
        chunk = NO_SEND;
    else
        chunk = ((req->httpProt == HTTP11) && req->connKeepAlive) ? SEND_CHUNK : SEND_NO_CHUNK;

    ClChunked chunk_buf(req, chunk);
    //------------ read from script -------------
    if (req->reqMethod == M_HEAD)
    {
        int n = cgi_to_cosmos(cgi_serv_in, conf->TimeoutCGI);
        if (n < 0)
        {
            print_err("<%s:%d> Error send_header_response()\n", __func__, __LINE__);
            return -1;
        }

        req->respContentLength = tail_len + n;
        if (send_response_headers(req, hdrs))
        {
            print_err("<%s:%d> Error send_header_response()\n", __func__, __LINE__);
            return -1;
        }

        return 0;
    }

    if (chunk == SEND_CHUNK)
        (*hdrs) << "Transfer-Encoding: chunked\r\n";

    if (send_response_headers(req, hdrs))
        return -1;
    //-------------------------- send entity ---------------------------
    if (tail_len > 0)
    {
        int n = chunk_buf.add_arr(tail_ptr, tail_len);
        if (n < 0)
        {
            print_err(req, "<%s:%d> Error chunk_buf.add_arr(): %d\n", __func__, __LINE__, n);
            return -1;
        }
    }

    int n = chunk_buf.cgi_to_client(cgi_serv_in);
    if (n < 0)
    {
        print_err(req, "<%s:%d> Error chunk_buf.cgi_to_client()=%d\n", __func__, __LINE__, n);
        return -1;
    }

    int ret = chunk_buf.end();
    req->send_bytes = chunk_buf.all();
    if (ret < 0)
    {
        print_err(req, "<%s:%d> Error chunk_buf.end(): %d\n", __func__, __LINE__, ret);
        return -1;
    }

    return 0;
}
//======================================================================
int cgi_read_headers(Connect *req, int cgi_serv_in)
{
    req->respStatus = RS200;

    String hdrs(256);
    if (hdrs.error())
    {
        print_err(req, "<%s:%d> Error create String object\n", __func__, __LINE__);
        return -RS500;
    }

    const char *err_str = "Error: Blank line not found";
    int ReadFromScript = 0;
    const int size = 256;
    char buf[size];
    char *start_ptr = buf;
    int line = 0;
    while (line < 10)
    {
        int len;
        char *end_ptr, *str;

        end_ptr = (char*)memchr(start_ptr, '\n', ReadFromScript);
        if (end_ptr == NULL)
        {
            if (ReadFromScript > 0)
            {
                for (int n = 0; n < ReadFromScript; ++n)
                    buf[n] = *(start_ptr--);
            }
            else
                ReadFromScript = 0;

            start_ptr = buf;

            int rd = size - ReadFromScript;
            if (rd <= 0)
            {
                err_str = "Error: Buffer for read is small";
                break;
            }

            int ret = read_timeout(cgi_serv_in, buf + ReadFromScript, rd, conf->TimeoutCGI);
            if (ret <= 0)
            {
                print_err(req, "<%s:%d> read_from_script()=%d, read_len=%d\n", __func__, __LINE__, ret, rd);
                if (ret)
                    err_str = "Error: Read from script";
                else
                    err_str = "Error: Blank line not found";
                break;
            }

            ReadFromScript += ret;
            continue;
        }

        str = start_ptr;
        len = end_ptr - start_ptr;
        start_ptr = end_ptr + 1;
        ReadFromScript -= (len + 1);

        if (len > 0)
        {
            if (*(end_ptr - 1) == '\r') --len;
        }

        *(str + len) = '\0';

        if (len == 0)
        {
            err_str = NULL;
            break;
        }

        ++line;

        if (!memchr(str, ':', len))
        {
            err_str = "Error: Line not header";
            break;
        }

        if (!strlcmp_case(str, "Status", 6))
        {
            req->respStatus = atoi(str + 7);//  req->respStatus = strtol(str + 7, NULL, 10);
            continue;
        }

        if (!strlcmp_case(str, "Date", 4) || \
            !strlcmp_case(str, "Server", 6) || \
            !strlcmp_case(str, "Accept-Ranges", 13) || \
            !strlcmp_case(str, "Content-Length", 14) || \
            !strlcmp_case(str, "Connection", 10))
        {
            continue;
        }

        hdrs << str << "\r\n";
        if (hdrs.error())
        {
            err_str = "Error: Create header";
            break;
        }
    }

    if (err_str)
    {
        print_err(req, "<%s:%d> %s\n ReadFromScript=%d\n", __func__, __LINE__, err_str, ReadFromScript);
        return -RS500;
    }

    return cgi_chunk(req, &hdrs, cgi_serv_in, start_ptr, ReadFromScript);
}
//======================================================================
int cgi_fork(Connect *req, int *serv_cgi, int *cgi_serv, String& path)
{
    int wr_bytes, n;
    //--------------------------- fork ---------------------------------
    pid_t pid = fork();
    if (pid < 0)
    {
        print_err(req, "<%s:%d> Error fork(): %s\n", __func__, __LINE__, strerror(errno));
        close(serv_cgi[0]);
        close(serv_cgi[1]);
        close(cgi_serv[1]);
        return -RS500;
    }
    else if (pid == 0)
    {
        //----------------------- child --------------------------------
        close(cgi_serv[0]);
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

        if (cgi_serv[1] != STDOUT_FILENO)
        {
            if (dup2(cgi_serv[1], STDOUT_FILENO) < 0)
            {
                fprintf(stderr, "<%s:%d> Error dup2(): %s\n", __func__, __LINE__, strerror(errno));
                exit(EXIT_FAILURE);
            }
            close(cgi_serv[1]);
        }

        if (req->scriptType == php_cgi)
            setenv("REDIRECT_STATUS", "true", 1);
        setenv("PATH", "/bin:/usr/bin:/usr/local/bin", 1);
        setenv("SERVER_SOFTWARE", conf->ServerSoftware.c_str(), 1);
        setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
        setenv("DOCUMENT_ROOT", conf->DocumentRoot.c_str(), 1);
        setenv("REMOTE_ADDR", req->remoteAddr, 1);
        setenv("REMOTE_PORT", req->remotePort, 1);
        setenv("REQUEST_URI", req->uri, 1);
        setenv("REQUEST_METHOD", get_str_method(req->reqMethod), 1);
        setenv("SERVER_PROTOCOL", get_str_http_prot(req->httpProt), 1);
        if (req->req_hd.iHost >= 0)
            setenv("HTTP_HOST", req->reqHdValue[req->req_hd.iHost], 1);
        if (req->req_hd.iReferer >= 0)
            setenv("HTTP_REFERER", req->reqHdValue[req->req_hd.iReferer], 1);
        if (req->req_hd.iUserAgent >= 0)
            setenv("HTTP_USER_AGENT", req->reqHdValue[req->req_hd.iUserAgent], 1);

        setenv("SCRIPT_NAME", req->scriptName, 1);
        setenv("SCRIPT_FILENAME", path.c_str(), 1);

        if (req->reqMethod == M_POST)
        {
            if (req->req_hd.iReqContentType >= 0)
                setenv("CONTENT_TYPE", req->reqHdValue[req->req_hd.iReqContentType], 1);
            if (req->req_hd.iReqContentLength >= 0)
                setenv("CONTENT_LENGTH", req->reqHdValue[req->req_hd.iReqContentLength], 1);
        }

        setenv("QUERY_STRING", req->sReqParam ? req->sReqParam : "", 1);

        int err_ = 0;
        if (req->scriptType == cgi_ex)
        {
            execl(path.c_str(), base_name(req->scriptName), NULL);
            err_ = errno;
        }
        else if (req->scriptType == php_cgi)
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
                "</html>", strerror(err_), req->sLogTime.c_str());
        fclose(stdout);
        exit(EXIT_FAILURE);
    }
    else
    {
    //=========================== parent ===============================
        close(serv_cgi[0]);
        close(cgi_serv[1]);
        //------------ write to script ------------
        if (req->reqMethod == M_POST)
        {
            if (req->tail)
            {
                wr_bytes = write_timeout(serv_cgi[1], req->tail, req->lenTail, conf->TimeoutCGI);
                if (wr_bytes < 0)
                {
                    print_err(req, "<%s:%d> Error tail to script: %d\n", __func__, __LINE__, wr_bytes);
                    close(serv_cgi[1]);
                    return -RS502;
                }
                req->req_hd.reqContentLength -= wr_bytes;
            }

            wr_bytes = client_to_cgi(req->clientSocket, serv_cgi[1], &req->req_hd.reqContentLength);
            if ((wr_bytes <= 0) && (req->req_hd.reqContentLength))
            {
                print_err(req, "<%s:%d> Error client_to_script() = %d\n", __func__, __LINE__, wr_bytes);
                close(serv_cgi[1]);
                return -RS502;
            }
        }

        close(serv_cgi[1]);
        n = cgi_read_headers(req, cgi_serv[0]);
        if (n < 0)
            kill_script(pid);
        else
            wait_pid(req, pid);
        return n;
    }
}
//======================================================================
int cgi(Connect *req)
{
    int serv_cgi[2], cgi_serv[2];
    int n, ret;
    struct stat st;

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
    {
        return -1;
    }

    n = strlen(req->scriptName);

    String path;
    switch (req->scriptType)
    {
        case cgi_ex:
            path << conf->ScriptPath << get_script_name(req->scriptName);
            break;
        case php_cgi:
            path << conf->DocumentRoot << req->scriptName;
            break;
        default:
            print_err(req, "<%s:%d> ScriptType \?(404)\n", __func__, __LINE__);
            ret = -RS500;
            goto errExit1;
    }

    if (stat(path.c_str(), &st) == -1)
    {
        print_err(req, "<%s:%d> script (%s) not found\n", __func__, __LINE__, path.c_str());
        ret = -RS404;
        goto errExit1;
    }

    n = pipe(serv_cgi);
    if (n == -1)
    {
        print_err(req, "<%s:%d> Error pipe()=%d\n", __func__, __LINE__, n);
        ret = -RS500;
        goto errExit1;
    }

    n = pipe(cgi_serv);
    if (n == -1)
    {
        print_err(req, "<%s:%d> Error pipe()=%d\n", __func__, __LINE__, n);
        ret = -RS500;
        close(serv_cgi[0]);
        close(serv_cgi[1]);
        goto errExit1;
    }
    //------------------------------------------------------------------
    ret = cgi_fork(req, serv_cgi, cgi_serv, path);
    close(cgi_serv[0]);
    cgi_dec();

    return ret;

errExit1:
    cgi_dec();
    req->connKeepAlive = 0;
    return ret;
}
