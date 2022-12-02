#ifndef SCGI_SERVER_
#define SCGI_SERVER_

#include <iostream>
#include <cstdio>
#include <poll.h>

using namespace std;

const int SCGI_SIZE_BUF = 4096;
const int DATA_OFFSET = 10;
//======================================================================
class SCGI_client
{
    int err = 0;
    
    char *scgi_buf;
    int size_buf;
    
    int offset_out, all_send;
    int scgi_sock;
    int TimeoutCGI;
    //------------------------------------------------------------------
    SCGI_client() {}
public://===============================================================
    SCGI_client(int sock, int timeout)
    {
        scgi_sock = sock;
        TimeoutCGI = timeout;
        offset_out = DATA_OFFSET;
        err = 0;
        size_buf = SCGI_SIZE_BUF;
        scgi_buf = new (nothrow) char [size_buf];
        if (!scgi_buf)
            err = 1;
    }
    //------------------------------------------------------------------
    ~SCGI_client()
    {
        if (scgi_buf)
            delete [] scgi_buf;
    }
    //------------------------------------------------------------------
    int error() const { return err; }
    void add(const char *name, const char *val);
    void add(const char *name, const char *val, int len);
    int send_headers();
    int scgi_send(const char *data, int size);
    int scgi_read(char *buf, int size);
};
//======================================================================
    void SCGI_client::add(const char *name, const char *val)
    {
        if (err)
            return;

        if (name)
        {
            int len_name = strlen(name);
            if ((offset_out + len_name) < size_buf)
            {
                memcpy(scgi_buf + offset_out, name, len_name);
                offset_out += len_name;
                *(scgi_buf + offset_out) = 0;
                offset_out++;
            }
            else
            {
                err = 1;
                return;
            }
        }
        else
        {
            err = 1;
            return;
        }
        
        if (val)
        {
            int len_val = strlen(val);
            if ((offset_out + len_val) < size_buf)
            {
                memcpy(scgi_buf + offset_out, val, len_val);
                offset_out += len_val;
                *(scgi_buf + offset_out) = 0;
                offset_out++;
            }
            else
            {
                err = 1;
                return;
            }
        }
        else
        {
            if ((offset_out + 1) < size_buf)
            {
                *(scgi_buf + offset_out) = 0;
                offset_out++;
            }
            else
                err = 1;
        }
    }
    //==================================================================
    void SCGI_client::add(const char *name, const char *val, int len)
    {
        if (err)
            return;

        if (name)
        {
            int len_name = strlen(name);
            memcpy(scgi_buf + offset_out, name, len_name);
            offset_out += len_name;
            *(scgi_buf + offset_out) = 0;
            offset_out++;
        }
        else
        {
            *(scgi_buf + offset_out) = 0;
            offset_out++;
        }
        
        if (val)
        {
            int len_val = strlen(val);
            if (len > len_val)
            {
                err = 1;
                return;
            }
            
            memcpy(scgi_buf + offset_out, val, len);
            offset_out += len;
            *(scgi_buf + offset_out) = 0;
            offset_out++;
        }
        else
        {
            *(scgi_buf + offset_out) = 0;
            offset_out++;
        }
    }
    //==================================================================
    int SCGI_client::send_headers()
    {
        int i = DATA_OFFSET - 1, size = offset_out - DATA_OFFSET;
        scgi_buf[i--] = ':';
        for ( ; i >= 0; --i)
        {
            scgi_buf[i] = (size % 10) + '0';
            size /= 10;
            if (size == 0)
                break;
        }

        scgi_buf[offset_out++] = ',';
        if (scgi_send(scgi_buf + i, offset_out - i) < 0)
            return -1;

        return offset_out;
    }
    //==================================================================
    int SCGI_client::scgi_send(const char *data, int size)
    {
        if (err)
            return -1;

        int write_bytes = 0, ret = 0;
        struct pollfd fdwr;
        const char *p = data;

        fdwr.fd = scgi_sock;
        fdwr.events = POLLOUT;

        while (size > 0)
        {
            ret = poll(&fdwr, 1, TimeoutCGI * 1000);
            if (ret == -1)
            {
                if (errno == EINTR)
                    continue;
                break;
            }
            else if (!ret)
            {
                err = 1;
                ret = -1;
                break;
            }
            
            if (fdwr.revents != POLLOUT)
            {
                err = 1;
                ret = -1;
                break;
            }

            ret = write(scgi_sock, p, size);
            if (ret == -1)
            {
                if ((errno == EINTR) || (errno == EAGAIN))
                    continue;
                err = 1;
                break;
            }

            write_bytes += ret;
            size -= ret;
            p += ret;
        }
        
        if (ret <= 0)
        {
            err = 1;
            ret = -1;
        }
        else
        {
            all_send += write_bytes;
            ret = all_send;
        }
        offset_out = 0;
        
        return ret;
    }
    //==================================================================
    int SCGI_client::scgi_read(char *buf, int size)
    {
        int read_bytes = 0;
        struct pollfd fdrd;
        char *p = buf;
        
        fdrd.fd = scgi_sock;
        fdrd.events = POLLIN;
        
        while (size > 0)
        {
            int ret = poll(&fdrd, 1, TimeoutCGI * 1000);
            if (ret == -1)
            {
                if (errno == EINTR)
                    continue;
                err = 1;
                return -1;
            }
            else if (!ret)
            {
                err = 1;
                return -1;
            }
            
            if (fdrd.revents & POLLIN)
            {
                ret = read(scgi_sock, p, size);
                if (ret < 0)
                {
                    fprintf(stderr, "Error read(): %s\n", strerror(errno));
                    err = 1;
                    return -1;
                }
                else if (ret == 0)
                    break;
                else
                {
                    p += ret;
                    size -= ret;
                    read_bytes += ret;
                }
            }
        }

        return read_bytes;
    }

#endif
