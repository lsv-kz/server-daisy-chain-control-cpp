#ifndef CLASSES_H_
#define CLASSES_H_

#include "main.h"

//======================================================================
struct Range {
    long long start;
    long long end;
    long long len;
};
//----------------------------------------------------------------------
class ArrayRanges
{
protected:
    Range *range = NULL;
    unsigned int SizeArray = 0;
    unsigned int nRanges = 0;
    long long sizeFile;
    int err = 0;
    void check_ranges();
    void parse_ranges(char *sRange);

    void reserve()
    {
        if (err) return;
        if (SizeArray <= 0)
        {
            err = 1;
            return;
        }

        range = new(std::nothrow) Range [SizeArray];
        if (!range)
        {
            err = 1;
            return;
        }
    }

public:
    ArrayRanges(const ArrayRanges&) = delete;
    ArrayRanges() = delete;
    ArrayRanges(char *s, long long sz);
    ~ArrayRanges() { if (range) { delete [] range; } }

    ArrayRanges & operator << (const Range& val)
    {
        if (err) return *this;
        if (!range || (nRanges >= SizeArray))
        {
            err = 1;
            return *this;
        }

        range[nRanges++] = val;
        return *this;
    }

    Range *get(unsigned int i)
    {
        if (err) return NULL;
        if (i < 0)
        {
            err = 1;
            return NULL;
        }

        if (i < nRanges)
            return range + i;
        else
            return NULL;
    }

    int size() { if (err) return 0; return nRanges; }
    int capacity() { if (err) return 0; return SizeArray; }
    int error() { return -err; }
};
//======================================================================
const int CHUNK_SIZE_BUF = 4096;
const int MAX_LEN_SIZE_CHUNK = 6;
 // NO_SEND - {Request="HEAD"}; SEND_NO_CHUNK - {"Connection: close", HTTP/0.9, HTTP/1.0}; SEND_CHUNK - {all other}
enum mode_chunk {NO_SEND = 0, SEND_NO_CHUNK, SEND_CHUNK};
//----------------------------------------------------------------------
class ClChunked
{
    int i, mode, allSend, lenEntity;
    int err = 0;
    Connect *req;
    char buf[CHUNK_SIZE_BUF + MAX_LEN_SIZE_CHUNK + 10];
    ClChunked() {};
    //------------------------------------------------------------------
    int send_chunk(int size)
    {
        if (err) return -1;
        const char *p;
        int len;
        if (mode == SEND_CHUNK)
        {
            String ss(16);
            ss << Hex << size << "\r\n" << Dec;
            len = ss.size();
            int n = MAX_LEN_SIZE_CHUNK - len;
            if (n < 0)
                return -1;
            memcpy(buf + n, ss.c_str(), len);
            memcpy(buf + MAX_LEN_SIZE_CHUNK + i, "\r\n", 2);
            i += 2;
            p = buf + n;
            len += i;
        }
        else
        {
            p = buf + MAX_LEN_SIZE_CHUNK;
            len = i;
        }

        int ret = write_to_client(req, p, len, conf->Timeout);

        i = 0;
        if (ret > 0)
            allSend += ret;
        return ret;
    }
public://---------------------------------------------------------------
    ClChunked(Connect *rq, int m){req = rq; mode = m; i = allSend = err = lenEntity = 0;}
    //------------------------------------------------------------------
    ClChunked & operator << (const long long ll)
    {
        if (err) return *this;
        String ss(32);
        ss << ll;
        *this << ss;
        return *this;
    }
    //------------------------------------------------------------------
    ClChunked & operator << (const char *s)
    {
        if (err) return *this;
        if (!s)
        {
            err = 1;
            return *this;
        }
        int n = 0, len = strlen(s);
        if (mode == NO_SEND)
        {
            allSend += len;
            return *this;
        }

        lenEntity += len;

        while (CHUNK_SIZE_BUF < (i + len))
        {
            int l = CHUNK_SIZE_BUF - i;
            memcpy(buf + MAX_LEN_SIZE_CHUNK + i, s + n, l);
            i += l;
            len -= l;
            n += l;
            int ret = send_chunk(i);
            if (ret < 0)
            {
                err = 1;
                return *this;
            }
        }

        memcpy(buf + MAX_LEN_SIZE_CHUNK + i, s + n, len);
        i += len;
        return *this;
    }
    //------------------------------------------------------------------
    ClChunked & operator << (const String& s)
    {
        if (err) return *this;
        *this << s.c_str();
        return *this;
    }
    //------------------------------------------------------------------
    ClChunked & operator << (const std::string& s)
    {
        if (err) return *this;
        *this << s.c_str();
        return *this;
    }
    //------------------------------------------------------------------
    int add_arr(const char *s, int len)
    {
        if (err) return -1;
        if (!s) return -1;
        if (mode == NO_SEND)
        {
            allSend += len;
            return 0;
        }

        lenEntity += len;

        int n = 0;
        while (CHUNK_SIZE_BUF < (i + len))
        {
            int l = CHUNK_SIZE_BUF - i;
            memcpy(buf + MAX_LEN_SIZE_CHUNK + i, s + n, l);
            i += l;
            len -= l;
            n += l;
            int ret = send_chunk(i);
            if (ret < 0)
            {
                err = 1;
                return ret;
            }
        }

        memcpy(buf + MAX_LEN_SIZE_CHUNK + i, s + n, len);
        i += len;
        return 0;
    }
    //------------------------------------------------------------------
    int cgi_to_client(int fdPipe)
    {
        if (err) return -1;
        int all_rd = 0;
        while (1)
        {
            if (CHUNK_SIZE_BUF <= i)
            {
                int ret = send_chunk(i);
                if (ret < 0)
                {
                    err = 1;
                    return ret;
                }
            }

            int rd = CHUNK_SIZE_BUF - i;
            int ret = read_timeout(fdPipe, buf + MAX_LEN_SIZE_CHUNK + i, rd, conf->TimeoutCGI);
            if (ret == 0)
            {
                break;
            }
            else if (ret < 0)
            {
                i = 0;
                return ret;
            }

            i += ret;
            all_rd += ret;
        }

        return all_rd;
    }
    //------------------------------------------------------------------
    int fcgi_to_client(int fcgi_sock, int len)
    {
        if (err) return -1;
        if (mode == NO_SEND)
        {
            allSend += len;
            fcgi_to_cosmos(fcgi_sock, len, conf->TimeoutCGI);
            return 0;
        }

        while (len > 0)
        {
            if (CHUNK_SIZE_BUF <= i)
            {
                int ret = send_chunk(i);
                if (ret < 0)
                    return ret;
            }

            int rd = (len < (CHUNK_SIZE_BUF - i)) ? len : (CHUNK_SIZE_BUF - i);
            int ret = read_timeout(fcgi_sock, buf + MAX_LEN_SIZE_CHUNK + i, rd, conf->TimeoutCGI);
            if (ret <= 0)
            {
                print_err("<%s:%d> ret=%d\n", __func__, __LINE__, ret);
                i = 0;
                return -1;
            }
            else if (ret != rd)
            {
                print_err("<%s:%d> ret != rd\n", __func__, __LINE__);
                i = 0;
                return -1;
            }

            i += ret;
            len -= ret;
        }

        return 0;
    }
    //------------------------------------------------------------------
    int end()
    {
        if (err) return -1;
        if (mode == SEND_CHUNK)
        {
            int n = i;
            const char *s = "\r\n0\r\n";
            int len = strlen(s);
            memcpy(buf + MAX_LEN_SIZE_CHUNK + i, s, len);
            i += len;
            return send_chunk(n);
        }
        else if (mode == SEND_NO_CHUNK)
            return send_chunk(0);
        else
            return 0;
    }
    //------------------------------------------------------------------
    int all(){return allSend;}
    int error() { return err; }
    int len_entity() { return lenEntity; }
};

#endif
