#include "main.h"

using namespace std;
//======================================================================
void Connect::init()
{
    sRange = NULL;
    //------------------------------------
    decodeUri[0] = 0;
    uri = NULL;
    p_newline = req.buf;
    tail = NULL;
    //------------------------------------
    err = 0;
    lenTail = 0;
    req.len = 0;
    countReqHeaders = 0;
    reqMethod = 0;
    httpProt = 0;
    connKeepAlive = 0;

    req_hd = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1LL};

    respStatus = 0;
    mode_send = NO_CHUNK;

    cgi = NULL;
    cgi_type = NO_CGI;
    scriptName = "";

    fcgi.fd = -1;

    hdrs = "";
    numPart = 0;
    respContentLength = -1LL;
    respContentType = NULL;
    fileSize = 0;
    fd = -1;
    offset = 0;
    send_bytes = 0LL;
}
//======================================================================
/*void Ranges::check_ranges()
{
    if (err) return;
    Range *r = range;

    for ( int n = nRanges - 1; n > 0; n--)
    {
        for (int i = n - 1; i >= 0; i--)
        {
            if (((r[n].end + 1) >= r[i].start) && ((r[i].end + 1) >= r[n].start))
            {
                nRanges--;
                if (r[n].start < r[i].start)
                    r[i].start = r[n].start;

                if (r[n].end > r[i].end)
                    r[i].end = r[n].end;

                r[i].len = r[i].end - r[i].start + 1;
                r[n].len = 0;

                if (((nRanges) > n) && (r[nRanges].len > 0))
                {
                    r[n].start = r[nRanges].start;
                    r[n].end = r[nRanges].end;
                    r[n].len = r[nRanges].len;
                    r[nRanges].len = 0;
                }

                n--;
            }
        }
    }
}*/
//----------------------------------------------------------------------
void Ranges::check_ranges()
{
    if (err) return;
    int num = nRanges;
    Range *r = range;

    for ( int n = num - 1; n > 0; n--)
    {
        for (int i = n - 1; i >= 0; i--)
        {
            if (((r[n].end + 1) >= r[i].start) && ((r[i].end + 1) >= r[n].start))
            {
                if (r[n].start < r[i].start)
                    r[i].start = r[n].start;

                if (r[n].end > r[i].end)
                    r[i].end = r[n].end;

                r[i].len = r[i].end - r[i].start + 1;
                r[n].len = 0;

                n--;
                nRanges--;
            }
        }
    }

    for (int i = 0, j = 0; j < num; j++)
    {
        if (r[j].len)
        {
            if (i < j)
            {
                r[i].start = r[j].start;
                r[i].end = r[j].end;
                r[i].len = r[j].len;
                r[j].len = 0;
            }

            i++;
        }
    }
}
//----------------------------------------------------------------------
void Ranges::parse_ranges(char *sRange)
{
    if (err) return;
    long long start = 0, end = 0, size = sizeFile, ll;
    int i = 0;
    const char *p1;
    char *p2;

    p1 = p2 = sRange;

    for ( ; nRanges < SizeArray; )
    {
        if (err) return;
        if ((*p1 >= '0') && (*p1 <= '9'))
        {
            ll = strtoll(p1, &p2, 10);
            if (p1 < p2)
            {
                if (i == 0)
                    start = ll;
                else if (i == 2)
                    end = ll;
                else
                {
                    err = RS416;
                    return;
                }

                i++;
                p1 = p2;
            }
        }
        else if (*p1 == ' ')
            p1++;
        else if (*p1 == '-')
        {
            if (i == 0)
            {
                ll = strtoll(p1, &p2, 10);
                if (ll < 0)
                {
                    start = size + ll;
                    end = size - 1;
                    i = 3;
                    p1 = p2;
                }
                else
                {
                    err = RS416;
                    return;
                }
            }
            else if (i == 2)
            {
                err = RS416;
                return;
            }
            else
            {
                p1++;
                i++;
            }
        }
        else if ((*p1 == ',') || (*p1 == 0))
        {
            if (i == 2)
                end = size - 1;
            else if (i != 3)
            {
                err = RS416;
                return;
            }

            if (start < 0)
            {
                start = 0;
                end = size - 1;
            }
            else if (end >= size)
                end = size - 1;

            if (start <= end)
            {
                (*this) << Range{start, end, end - start + 1};
                if (*p1 == 0)
                    break;
                start = end = 0;
                p1++;
                i = 0;
            }
            else
            {
                err = RS416;
                return;
            }
        }
        else
        {
            err = RS416;
            return;
        }
    }
}
//----------------------------------------------------------------------
void Ranges::init(char *s, long long sz)
{
    err = 0;
    if (!s)
    {
        err = RS500;
        return;
    }

    if (conf->MaxRanges == 0)
    {
        err = RS403;
        return;
    }

    nRanges = index = 0;

    unsigned int n = 0;
    for ( char *p = s; *p; ++p)
    {
        if (*p == ',')
            n++;
    }

    n++;

    if (n > conf->MaxRanges)
        n = conf->MaxRanges;
    reserve(n);
    sizeFile = sz;
    parse_ranges(s);
    if ((nRanges == 0) && (err == 0))
        err = RS416;
    else if (nRanges > 1)
        check_ranges();
}
