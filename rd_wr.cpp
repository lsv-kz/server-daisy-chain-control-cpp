#include "main.h"

using namespace std;

//    #define POLLIN      0x0001    /* Можно считывать данные */
//    #define POLLPRI     0x0002    /* Есть срочные данные */
//    #define POLLOUT     0x0004    /* Запись не будет блокирована */
//    #define POLLERR     0x0008    /* Произошла ошибка */
//    #define POLLHUP     0x0010    /* "Положили трубку" */
//    #define POLLNVAL    0x0020    /* Неверный запрос: fd не открыт */
//======================================================================
int poll_read(int fd, int timeout)
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
int poll_write(int fd, int timeout)
{
    int ret, tm;
    struct pollfd fdwr;

    tm = (timeout == -1) ? -1 : (timeout * 1000);

    fdwr.fd = fd;
    fdwr.events = POLLOUT;
    while (1)
    {
        ret = poll(&fdwr, 1, tm);
        if (ret == -1)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        else if (!ret)
            return -RS408;

        return fdwr.revents;
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
        ret = poll_read(fd, timeout);
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
int read_from_client(Connect *req, char *buf, int len, int timeout)
{
    int read_bytes = 0, ret;
    char *p;

    p = buf;
    while (len > 0)
    {
        ret = poll_read(req->clientSocket, timeout);
        if (ret < 0)
            return ret;

        if (ret & POLLIN)
        {
            ret = read(req->clientSocket, p, len);
            if (ret == -1)
            {
                print_err(req, "<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
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
int write_to_pipe(int fd, const char *buf, int len, int timeout)
{
    int write_bytes = 0, ret;

    while (len > 0)
    {
        ret = poll_write(fd, timeout);
        if (ret < 0)
        {
            print_err("<%s:%d> Error poll(): %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EINTR)
                continue;
            return ret;
        }

        if (ret == POLLOUT)
        {
            ret = write(fd, buf, len);
            if (ret == -1)
            {
                print_err("<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
                if ((errno == EINTR) || (errno == EAGAIN))
                    continue;
                return -1;
            }

            write_bytes += ret;
            len -= ret;
            buf += ret;
        }
        else
            return -1;
    }

    return write_bytes;
}
//======================================================================
int write_to_client(Connect *req, const char *buf, int len, int timeout)
{
    int write_bytes = 0, ret;

    while (len > 0)
    {
        ret = poll_write(req->clientSocket, timeout);
        if (ret < 0)
        {
            print_err(req, "<%s:%d> Error poll(): %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EINTR)
                continue;
            return ret;
        }

        if (ret == POLLOUT)
        {
            ret = write(req->clientSocket, buf, len);
            if (ret == -1)
            {
                print_err(req, "<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
                if ((errno == EINTR) || (errno == EAGAIN))
                    continue;
                return -1;
            }

            write_bytes += ret;
            len -= ret;
            buf += ret;
        }
        else
            return -1;
    }

    return write_bytes;
}
//======================================================================
int socket_to_pipe(Connect *req, int fd_out, long long *cont_len)
{
    int wr_bytes = 0;
    int rd, wr, ret;
    char buf[512];

    for ( ; *cont_len > 0; )
    {
        rd = (*cont_len > (int)sizeof(buf)) ? (int)sizeof(buf) : *cont_len;

        ret = read_from_client(req, buf, rd, conf->Timeout);
        if (ret == -1)
            return -1;
        else if (ret == 0)
            break;

        *cont_len -= ret;
        wr = write_to_pipe(fd_out, buf, ret, conf->TimeoutCGI);
        if (wr <= 0)
            return wr;
        wr_bytes += wr;
    }

    return wr_bytes;
}
//======================================================================
int send_largefile(Connect *req, char *buf, int size, off_t offset, long long *cont_len)
{
    int rd, wr;

    lseek(req->fd, offset, SEEK_SET);

    for ( ; *cont_len > 0; )
    {
        if (*cont_len < size)
            rd = read(req->fd, buf, *cont_len);
        else
            rd = read(req->fd, buf, size);

        if (rd == -1)
        {
            print_err(req, "<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
            if (errno == EINTR)
                continue;
            return -1;
        }
        else if (rd == 0)
            break;

        wr = write_to_client(req, buf, rd, conf->Timeout);
        if (wr != rd)
        {
            print_err(req, "<%s:%d> Error write_to_sock()=%d, %d\n", __func__, __LINE__, wr, rd);
            return -1;
        }

        *cont_len -= wr;
    }

    return 0;
}
