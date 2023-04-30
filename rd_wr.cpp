#include "main.h"

using namespace std;

//    #define POLLIN      0x0001    /* Можно считывать данные */
//    #define POLLPRI     0x0002    /* Есть срочные данные */
//    #define POLLOUT     0x0004    /* Запись не будет блокирована */
//    #define POLLERR     0x0008    /* Произошла ошибка */
//    #define POLLHUP     0x0010    /* "Положили трубку" */
//    #define POLLNVAL    0x0020    /* Неверный запрос: fd не открыт */
//======================================================================
int poll_in(int fd, int timeout)
{
    int ret, tm;
    struct pollfd fdrd;

    tm = (timeout == -1) ? -1 : (timeout * 1000);

    fdrd.fd = fd;
    fdrd.events = POLLIN;
    while (1)
    {
        ret = poll(&fdrd, 1, tm);
        if (ret == -1)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        else if (!ret)
            return -RS408;

        return fdrd.revents;
    }

    return -1;
}
//======================================================================
int read_from_pipe(int fd, char *buf, int len, int timeout)
{
    int read_bytes = 0, ret;
    char *p;

    p = buf;
    while (len > 0)
    {
        ret = poll_in(fd, timeout);
        if (ret < 0)
            return ret;

        if (ret & POLLIN)
        {
            ret = read(fd, p, len);
            if (ret == -1)
            {
                print_err("<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
                return -1;
            }
            else if (ret == 0)
                break;

            p += ret;
            len -= ret;
            read_bytes += ret;
        }
        else if (ret & POLLHUP)
            break;
        else
            return -1;
    }

    return read_bytes;
}
//======================================================================
int write_to_client(Connect *req, const char *buf, int len)
{
    int ret = send(req->clientSocket, buf, len, 0);
    if (ret == -1)
    {
        print_err(req, "<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
        if (errno == EAGAIN)
            return -EAGAIN;
        else 
            return -1;
    }
    else
        return  ret;
}
//======================================================================
int read_from_client(Connect *req, char *buf, int len)
{
    int ret = recv(req->clientSocket, buf, len, 0);
    if (ret == -1)
    {
        if (errno == EAGAIN)
            return -EAGAIN;
        else
        {
            print_err(req, "<%s:%d> Error recv(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }
    }
    else
        return  ret;
}
//======================================================================
int find_empty_line(Connect *req)
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
                    return -RS400;
                pCR = req->p_newline + i;
            }
            else if (ch == '\n')// found LF
            {
                pLF = req->p_newline + i;
                if ((pCR) && ((pLF - pCR) != 1))
                    return -RS400;
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

            if (len_line == 0) // found empty line
            {
                if (req->countReqHeaders == 0) // empty lines before Starting Line
                {
                    if ((pLF - req->req.buf + 1) > 4) // more than two empty lines
                        return -RS400;
                    req->lenTail -= i;
                    req->p_newline = pLF + 1;
                    continue;
                }

                if (req->lenTail > 0) // tail after empty line (Message Body for POST method)
                {
                    req->tail = pLF + 1;
                    req->lenTail -= i;
                }
                else
                    req->tail = NULL;
                return 1;
            }

            if (req->countReqHeaders < MAX_HEADERS)
            {
                req->reqHdName[req->countReqHeaders] = req->p_newline;
                if (req->countReqHeaders == 0)
                {
                    int ret = parse_startline_request(req, req->reqHdName[0]);
                    if (ret < 0)
                        return ret;
                }

                req->countReqHeaders++;
            }
            else
                return -RS500;

            req->lenTail -= i;
            req->p_newline = pLF + 1;
        }
        else if (pCR && (!pLF))
            return -RS400;
        else
            break;
    }

    return 0;
}
//======================================================================
int hd_read(Connect *req)
{
    int num_read = SIZE_BUF_REQUEST - req->req.len - 1;
    if (num_read <= 0)
        return -RS414;
    int n = read_from_client(req, req->req.buf + req->req.len, num_read);
    if (n < 0)
    {
        if (n == -EAGAIN)
            return -EAGAIN;
        return -1;
    }
    else if (n == 0)
        return -1;

    req->lenTail += n;
    req->req.len += n;
    req->req.buf[req->req.len] = 0;

    n = find_empty_line(req);
    if (n == 1) // empty line found
        return req->req.len;
    else if (n < 0) // error
        return -1;

    return 0;
}
