#ifndef FCGI_SERVER_
#define FCGI_SERVER_

#include <iostream>
#include <cstdio>
#include <poll.h>

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

const int FCGI_SIZE_BUF = 1024;
//======================================================================
typedef struct {
    unsigned char type;
    int len;
    int paddingLen;
} fcgi_header;
//----------------------------------------------------------------------
class FCGI_client
{
    const char *str_zero = "\0\0\0\0\0\0\0\0";
    char fcgi_buf[FCGI_SIZE_BUF];

    int err = 0;

    int offset_out, all_send;

    int offset_in;
    int http_headers_size;

    const int SIZE_HEADER = 8;
    int fcgi_sock;
    int TimeoutCGI = 10;
    fcgi_header header = {0, 0, 0};
    int numPar = 0;
    //------------------------------------------------------------------
    void send_par(int end)
    {
        if (err)
            return;
        fcgi_set_header(fcgi_buf, FCGI_PARAMS, offset_out);

        if (end) // && ((offset_out + 16) <= FCGI_SIZE_BUF))
        {
            char s[8] = {1, 4, 0, 1, 0, 0, 0, 0};
            memcpy(fcgi_buf + SIZE_HEADER + offset_out, s, 8);
            offset_out += 8;
            end = 0;
        }

        fcgi_send();
        if (err)
        {
            return;
        }

        if (end)
        {
            send_par(0);
        }
    }
    //------------------------------------------------------------------
    void fcgi_send()
    {
        if (err)
            return;

        int write_bytes = 0, ret = 0, len = offset_out + 8;
        struct pollfd fdwr;
        char *p = fcgi_buf;

        fdwr.fd = fcgi_sock;
        fdwr.events = POLLOUT;

        while (len > 0)
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
                ret = -1;
                break;
            }

            if (fdwr.revents != POLLOUT)
            {
                ret = -1;
                break;
            }

            ret = write(fcgi_sock, p, len);
            if (ret == -1)
            {
                if ((errno == EINTR) || (errno == EAGAIN))
                    continue;
                break;
            }

            write_bytes += ret;
            len -= ret;
            p += ret;
        }

        if (ret <= 0)
            err = 1;
        else
            all_send += write_bytes;
        offset_out = 0;
    }
    //------------------------------------------------------------------
    int fcgi_read(char *buf, int len)
    {
        if (err)
            return -1;

        int read_bytes = 0, ret;
        struct pollfd fdrd;
        char *p;

        fdrd.fd = fcgi_sock;
        fdrd.events = POLLIN;
        p = buf;

        while (len > 0)
        {
            ret = poll(&fdrd, 1, TimeoutCGI * 1000);
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
                ret = read(fcgi_sock, p, len);
                if (ret <= 0)
                {
                    err = 1;
                    return -1;
                }
                else
                {
                    p += ret;
                    len -= ret;
                    read_bytes += ret;
                }
            }
            else
            {
                err = 1;
                return -1;
            }
        }
        return read_bytes;
    }
    //------------------------------------------------------------------
    void fcgi_set_header(char *p, int type, int len)
    {
        if (err)
            return;

        unsigned char padding = 8 - (len % 8);
        padding = (padding == 8) ? 0 : padding;

        *p++ = FCGI_VERSION_1;
        *p++ = (unsigned char)type;
        *p++ = (unsigned char) ((1 >> 8) & 0xff);
        *p++ = (unsigned char) ((1) & 0xff);

        *p++ = (unsigned char) ((len >> 8) & 0xff);
        *p++ = (unsigned char) ((len) & 0xff);

        *p++ = padding;
        *p = 0;

        if (padding)
        {
            memcpy(fcgi_buf + SIZE_HEADER + offset_out, str_zero, padding);
            offset_out += padding;
        }
    }
    //------------------------------------------------------------------
    void fcgi_send_begin()
    {
        if (err)
            return;
        char *ptr_buf = fcgi_buf + SIZE_HEADER;
        offset_out = 0;
        ptr_buf[offset_out++] = (unsigned char) ((FCGI_RESPONDER >> 8) & 0xff);
        ptr_buf[offset_out++] = (unsigned char) (FCGI_RESPONDER        & 0xff);
        ptr_buf[offset_out++] = 0;//  FCGI_KEEP_CONN : 0
        ptr_buf[offset_out++] = 0;
        ptr_buf[offset_out++] = 0;
        ptr_buf[offset_out++] = 0;
        ptr_buf[offset_out++] = 0;
        ptr_buf[offset_out++] = 0;
        fcgi_set_header(fcgi_buf, FCGI_BEGIN_REQUEST, offset_out);

        fcgi_send();
    }
    //------------------------------------------------------------------
    int fcgi_read_header(fcgi_header *header)
    {
        if (err)
            return -1;

        int n;
        char buf[8];

        n = fcgi_read(buf, 8);
        if (n <= 0)
            return n;

        header->type = (unsigned char)buf[1];
        header->paddingLen = (unsigned char)buf[6];
        header->len = ((unsigned char)buf[4]<<8) | (unsigned char)buf[5];
        return n;
    }

    FCGI_client() {}
public://===============================================================
    FCGI_client(int sock, int timeout)
    {
        fcgi_sock = sock;
        TimeoutCGI = timeout;
        offset_out = all_send = 0;
        offset_in = http_headers_size = 0;
        err = 0;
        fcgi_send_begin();
    }
    //------------------------------------------------------------------
    int error() const { return err; }
    int send_bytes() { return all_send; }
    void param(const char *name, const char *val);

    int get_line();
    int fcgi_read_http_header(char**);
    int read_from_fcgi_server(char **p);
    int send_to_fcgi_server(const char *p, int len);
};
//======================================================================
    void FCGI_client::param(const char *name, const char *val)
    {
        if (err)
            return;

        ++numPar;
        if (!name)
        {
            send_par(1);
            return;
        }

        size_t len_name = strlen(name), len_val, len;

        if (val)
            len_val = strlen(val);
        else
            len_val = 0;

        len = len_name + len_val;
        len += len_name > 127 ? 4 : 1;
        len += len_val > 127 ? 4 : 1;

        if ((offset_out + len + 16) > FCGI_SIZE_BUF)
        {
            send_par(0);
            if (err)
                return;
            if ((len + 16) > FCGI_SIZE_BUF)
            {
                std::cerr << "[" << name << "  " << val << "] len=" << len <<  "\n";
                err = 1;
                return;
            }
        }

        char *p = fcgi_buf + SIZE_HEADER + offset_out;
        if (len_name < 0x80)
            *p++ = (unsigned char)len_name;
        else
        {
            *p++ = (unsigned char)((len_name >> 24) | 0x80);
            *p++ = (unsigned char)(len_name >> 16);
            *p++ = (unsigned char)(len_name >> 8);
            *p++ = (unsigned char)len_name;
        }

        if (len_val < 0x80)
            *p++ = (unsigned char)len_val;
        else
        {
            *p++ = (unsigned char)((len_val >> 24) | 0x80);
            *p++ = (unsigned char)(len_val >> 16);
            *p++ = (unsigned char)(len_val >> 8);
            *p++ = (unsigned char)len_val;
        }

        memcpy(p, name, len_name);
        if (len_val > 0)
            memcpy(p + len_name, val, len_val);

        offset_out += len;
    }
    //==================================================================
    int FCGI_client::get_line()
    {
        char *pLF;

        while (1)
        {
            int i = 0, len_line = 0;
            pLF = NULL;
            while ((http_headers_size + i) < offset_in)
            {
                char ch = *(fcgi_buf + http_headers_size + i);
                if (ch == '\n')// found LF
                {
                    pLF = fcgi_buf + http_headers_size + i;
                    break;
                }
                else if (ch == '\r')
                    ;
                else
                    len_line++;
                ++i;
            }

            if (pLF)
            {
                *(fcgi_buf + http_headers_size + len_line) = 0;
                http_headers_size += (i + 1);
                return len_line;
            }
            else
                return -1;
        }

        return -1;
    }
    //==================================================================
    int FCGI_client::fcgi_read_http_header(char **p)
    {
        char padd[256];
        int n;

        *p = fcgi_buf + http_headers_size;
        n = get_line();
        if (n >= 0)
            return n;

        while (1)
        {
            if (header.len > 0)
                n = header.len;
            else
            {
                n = fcgi_read_header(&header);
                if (n <= 0)
                {
                    err = 1;
                    return -1;
                }
            }

            int rd = (header.len <= (FCGI_SIZE_BUF - 1)) ? header.len : (FCGI_SIZE_BUF - 1);
            n = fcgi_read(fcgi_buf + offset_in, rd);
            if (n <= 0)
            {
                fprintf(stderr, "<%s:%d> Error: fcgi_read FCGI_STDOUT\n",  __func__, __LINE__);
                return -1;
            }

            header.len -= n;
            offset_in += n;
            
            n = get_line();

            if ((header.paddingLen > 0) && (header.len == 0))
            {
                fcgi_read(padd, header.paddingLen);
                header.paddingLen = 0;
            }

            if (n >= 0)
                return n;
        }

        return -1;
    }
    //==================================================================
    int FCGI_client::read_from_fcgi_server(char **p) // *** FCGI_STDOUT ***,  fcgi_client IN
    {
        char padd[256];
        if (err)
            return -1;
        *p = NULL;
        if (http_headers_size > 0)
        {
            *p = fcgi_buf + http_headers_size;
            int ret = offset_in - http_headers_size;
            offset_in = http_headers_size = 0;
            return ret;
        }

        while (1)
        {
            if (header.len == 0)
            {
                if (header.paddingLen > 0)
                {
                    fcgi_read(padd, header.paddingLen);
                    header.paddingLen = 0;
                }
                int n = fcgi_read_header(&header);
                if (n <= 0)
                {
                    err = 1;
                    return -1;
                }
            }

            if (header.type == FCGI_STDOUT)
            {
                if (header.len == 0)
                {
                    continue;
                }

                int rd = (header.len <= (FCGI_SIZE_BUF - 1)) ? header.len : (FCGI_SIZE_BUF - 1);
                int n = fcgi_read(fcgi_buf, rd);
                if (n <= 0)
                {
                    fprintf(stderr, "! Error: fcgi_read FCGI_STDOUT\n");
                    return -1;
                }

                header.len -= n;
                fcgi_buf[n] = 0;
                *p = fcgi_buf;
                return n;
            }
            else if (header.type == FCGI_END_REQUEST)
            {
                int n = fcgi_read(padd, header.len);
                if (n > 0)
                    header.len -= n;
                
                if (header.paddingLen > 0)
                {
                    fcgi_read(padd, header.paddingLen);
                    header.paddingLen = 0;
                }
                return 0;
            }
            else if (header.type == FCGI_STDERR)
            {
                int rd = (header.len < 256) ? header.len : 255;
                int n = fcgi_read(padd, rd);
                if (n > 0)
                {
                    padd[n] = 0;
                    fprintf(stderr, "%s\n", padd);
                    header.len -= n;
                }
                else
                {
                    fprintf(stderr, "! Error: fcgi_read FCGI_STDERR\n");
                    return -1;
                }
            }
            else
            {
                fprintf(stderr, "! Error: type=%d\n", header.type);
                return -1;
            }
        }
    }
    //==================================================================
    int FCGI_client::send_to_fcgi_server(const char *p, int len)// *** FCGI_STDIN ***,  fcgi_client out
    {
        if (err)
            return err;

        if (!p)
        {
            fcgi_set_header(fcgi_buf, FCGI_STDIN, 0);
            fcgi_send();
            return err;
        }

        int wr;
        while ((len > 0) && (err == 0))
        {
            if (len > (FCGI_SIZE_BUF - 16))
                wr = FCGI_SIZE_BUF - 16;
            else
                wr = len;

            memcpy(fcgi_buf + SIZE_HEADER, p, wr);
            offset_out += wr;

            fcgi_set_header(fcgi_buf, FCGI_STDIN, wr);
            fcgi_send();

            p += wr;
            len -= wr;
        }

        return err;
    }

#endif
