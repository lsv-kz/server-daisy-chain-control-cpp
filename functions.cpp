#include "main.h"

using namespace std;

//======================================================================
string get_time()
{
    struct tm t;
    char s[40];
    time_t now = time(NULL);

    gmtime_r(&now, &t);

    strftime(s, sizeof(s), "%a, %d %b %Y %H:%M:%S GMT", &t);
    return s;
}
//======================================================================
void get_time(string& str)
{
    struct tm t;
    char s[40];
    time_t now = time(NULL);

    gmtime_r(&now, &t);

    strftime(s, sizeof(s), "%a, %d %b %Y %H:%M:%S GMT", &t);
    str = s;
}
//======================================================================
string log_time()
{
    struct tm t;
    char s[40];
    time_t now = time(NULL);

    gmtime_r(&now, &t);

    strftime(s, sizeof(s), "%d/%b/%Y:%H:%M:%S GMT", &t);
    return s;
}
//======================================================================
const char *strstr_case(const char *s1, const char *s2)
{
    const char *p1, *p2;
    char c1, c2;

    if (!s1 || !s2) return NULL;
    if (*s2 == 0) return s1;

    int diff = ('a' - 'A');

    for (; ; ++s1)
    {
        c1 = *s1;
        if (!c1) break;
        c2 = *s2;
        c1 += (c1 >= 'A') && (c1 <= 'Z') ? diff : 0;
        c2 += (c2 >= 'A') && (c2 <= 'Z') ? diff : 0;
        if (c1 == c2)
        {
            p1 = s1;
            p2 = s2;
            ++s1;
            ++p2;

            for (; ; ++s1, ++p2)
            {
                c2 = *p2;
                if (!c2) return p1;

                c1 = *s1;
                if (!c1) return NULL;

                c1 += (c1 >= 'A') && (c1 <= 'Z') ? diff : 0;
                c2 += (c2 >= 'A') && (c2 <= 'Z') ? diff : 0;
                if (c1 != c2)
                    break;
            }
        }
    }

    return NULL;
}
//======================================================================
int strlcmp_case(const char *s1, const char *s2, int len)
{
    char c1, c2;

    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    int diff = ('a' - 'A');

    for (; len > 0; --len, ++s1, ++s2)
    {
        c1 = *s1;
        c2 = *s2;
        if (!c1 && !c2) return 0;

        c1 += (c1 >= 'A') && (c1 <= 'Z') ? diff : 0;
        c2 += (c2 >= 'A') && (c2 <= 'Z') ? diff : 0;

        if (c1 != c2) return (c1 - c2);
    }

    return 0;
}
//======================================================================
int get_int_method(const char *s)
{
    if (!memcmp(s, "GET", 3))
        return M_GET;
    else if (!memcmp(s, "POST", 4))
        return M_POST;
    else if (!memcmp(s, "HEAD", 4))
        return M_HEAD;
    else if (!memcmp(s, "OPTIONS", 7))
        return M_OPTIONS;
    else if (!memcmp(s, "CONNECT", 7))
        return M_CONNECT;
    else
        return 0;
}
//======================================================================
const char *get_str_method(int i)
{
    switch (i)
    {
        case M_GET:
            return "GET";
        case M_POST:
            return "POST";
        case M_HEAD:
            return "HEAD";
        case M_OPTIONS:
            return "OPTIONS";
        case M_CONNECT:
            return "CONNECT";
    }

    return "";
}
//======================================================================
int get_int_http_prot(const char *s)
{
    if (!memcmp(s, "HTTP/1.1", 8))
        return HTTP11;
    else if (!memcmp(s, "HTTP/1.0", 8))
        return HTTP10;
    else if (!memcmp(s, "HTTP/0.9", 8))
        return HTTP09;
    else if (!memcmp(s, "HTTP/2", 6))
        return HTTP2;
    else
        return 0;
}
//======================================================================
const char *get_str_http_prot(int i)
{
    switch (i)
    {
        case HTTP11:
            return "HTTP/1.1";
        case HTTP10:
            return "HTTP/1.0";
        case HTTP09:
            return "HTTP/0.9";
    }

    return "";
}
//======================================================================
const char *get_str_operation(OPERATION_TYPE n)
{
    switch (n)
    {
        case READ_REQUEST:
            return "READ_REQUEST";
        case SEND_RESP_HEADERS:
            return "SEND_RESP_HEADERS";
        case SEND_ENTITY:
            return "SEND_ENTITY";
        case DYN_PAGE:
            return "DYN_PAGE";
    }

    return "?";
}
//======================================================================
const char *get_cgi_operation(CGI_OPERATION n)
{
    switch (n)
    {
        case CGI_CREATE_PROC:
            return "CGI_CREATE_PROC";
        case CGI_STDIN:
            return "CGI_STDIN";
        case CGI_READ_HTTP_HEADERS:
            return "CGI_READ_HTTP_HEADERS";
        case CGI_SEND_HTTP_HEADERS:
            return "CGI_SEND_HTTP_HEADERS";
        case CGI_SEND_ENTITY:
            return "CGI_SEND_ENTITY";
    }

    return "?";
}
//======================================================================
const char *get_fcgi_operation(FCGI_OPERATION n)
{
    switch (n)
    {
        case FASTCGI_CONNECT:
            return "FASTCGI_CONNECT";
        case FASTCGI_BEGIN:
            return "FASTCGI_BEGIN";
        case FASTCGI_PARAMS:
            return "FASTCGI_PARAMS";
        case FASTCGI_STDIN:
            return "FASTCGI_STDIN";
        case FASTCGI_READ_HTTP_HEADERS:
            return "FASTCGI_READ_HTTP_HEADERS";
        case FASTCGI_SEND_HTTP_HEADERS:
            return "FASTCGI_SEND_HTTP_HEADERS";
        case FASTCGI_SEND_ENTITY:
            return "FASTCGI_SEND_ENTITY";
        case FASTCGI_READ_ERROR:
            return "FASTCGI_READ_ERROR";
        case FASTCGI_CLOSE:
            return "FASTCGI_CLOSE";
    }

    return "?";
}
//======================================================================
const char *get_fcgi_status(FCGI_STATUS n)
{
    switch (n)
    {
        case FCGI_READ_DATA:
            return "FCGI_READ_DATA";
        case FCGI_READ_HEADER:
            return "FCGI_READ_HEADER";
        case FCGI_READ_PADDING:
            return "FCGI_READ_PADDING";
    }

    return "?";
}
//======================================================================
const char *get_scgi_operation(SCGI_OPERATION n)
{
    switch (n)
    {
        case SCGI_CONNECT:
            return "SCGI_CONNECT";
        case SCGI_PARAMS:
            return "SCGI_PARAMS";
        case SCGI_STDIN:
            return "SCGI_STDIN";
        case SCGI_READ_HTTP_HEADERS:
            return "SCGI_READ_HTTP_HEADERS";
        case SCGI_SEND_HTTP_HEADERS:
            return "SCGI_SEND_HTTP_HEADERS";
        case SCGI_SEND_ENTITY:
            return "SCGI_SEND_ENTITY";
    }

    return "?";
}
//======================================================================
const char *get_cgi_type(CGI_TYPE n)
{
    switch (n)
    {
        case NO_CGI:
            return "NO_CGI";
        case CGI:
            return "CGI";
        case PHPCGI:
            return "PHPCGI";
        case PHPFPM:
            return "PHPFPM";
        case FASTCGI:
            return "FASTCGI";
        case SCGI:
            return "SCGI";
    }

    return "?";
}
//======================================================================
const char *get_cgi_dir(DIRECT n)
{
    switch (n)
    {
        case FROM_CGI:
            return "FROM_CGI";
        case TO_CGI:
            return "TO_CGI";
        case FROM_CLIENT:
            return "FROM_CLIENT";
        case TO_CLIENT:
            return "TO_CLIENT";
    }

    return "?";
}
//======================================================================
const char *istextfile_(FILE *f)
{
    int cnt, i;
    int c;
    char s[128] = "";
    char chr_txt[] = "`~!@#$%^&*()-_=+\\|[]{};:'\",<.>/?"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "abcdefghijklmnopqrstuvwxyz"
                    "0123456789"
                    "\x09\x20\x0a\x0d";

    for (cnt = 0; ((c = fgetc(f)) >= 0) && (cnt < 128); cnt++)
    {
        if ((c < ' ') && (c != '\t') && (c != '\r') && (c != '\n'))
            return "";
    }

    if (cnt == 0)
        return "";

    fseek(f, 0, SEEK_SET);
    fgets(s, sizeof(s), f);
    if (strstr(s, "html>") || strstr(s, "HTML>") || strstr(s, "<html") || strstr(s, "<HTML"))
        return "text/html";

    fseek(f, 0, SEEK_SET);
    for (cnt = 0; ((c = fgetc(f)) >= 0) && (cnt < 32); cnt++)
    {
        if ((c < ' ') && (c != '\t') && (c != '\r') && (c != '\n'))
            return "";

        if (c < 0x7f)
        {
            if (!strchr(chr_txt, c))
                return "";
            continue;
        }
        else if ((c >= 0xc0) && (c <= 0xdf))
        {
            for (i = 1; i < 2; i++)
            {
                c = fgetc(f);
                if (!((c >= 0x80) && (c <= 0xbf)))
                    return "";
            }
            continue;
        }
        else if ((c >= 0xe0) && (c <= 0xef))
        {
            for (i = 1; i < 3; i++)
            {
                c = fgetc(f);
                if (!((c >= 0x80) && (c <= 0xbf)))
                    return "";
            }
            continue;
        }
        else if ((c >= 0xf0) && (c <= 0xf7))
        {
            for (i = 1; i < 4; i++)
            {
                c = fgetc(f);
                if (!((c >= 0x80) && (c <= 0xbf)))
                    return "";
            }
            continue;
        }
        else if ((c >= 0xf8) && (c <= 0xfb))
        {
            for (i = 1; i < 5; i++)
            {
                c = fgetc(f);
                if (!((c >= 0x80) && (c <= 0xbf)))
                    return "";
            }
            continue;
        }
        else if ((c >= 0xfc) && (c <= 0xfd))
        {
            for (i = 1; i < 6; i++)
            {
                c = fgetc(f);
                if (!((c >= 0x80) && (c <= 0xbf)))
                    return "";
            }
            continue;
        }
    }

    return "text/plain; charset=UTF-8";
}
//======================================================================
const char *istextfile(const char *path)
{
    FILE *f;

    f = fopen(path, "r");
    if (f == NULL)
    {
        print_err("<%s:%d> Error fopen(%s): %s\n", __func__, __LINE__, path, strerror(errno));
        return "";
    }

    const char *s = istextfile_(f);
    fclose(f);

    return s;
}
//======================================================================
const char *ismediafile_(FILE *f)
{
    int size = 0;
    char s[64];

    size = fread(s, 1, 63, f);
    if (size <= 0)
        return "";

    if (!memcmp(s, "\x30\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA\x00\x62\xCE\x6C", 16))
    {
        return "video/x-ms-wmv";
    }

    if (s[0] == 'C' || s[0] == 'F')
    {
        if (!memcmp(s + 1, "WS", 2))
        {
            if (s[3] >= 0x02 && s[3] <= 0x15)
                return "application/x-shockwave-flash";
        }
    }

    if (!memcmp(s, "RIFF", 4))                              // avi, wav
    {
        if (!memcmp(s + 8, "AVI LIST", 8))
            return "video/x-msvideo";
        else if (!memcmp(s + 8, "WAVE", 4))
            return "audio/x-wav";
        else
            return "";
    }

    if ((!memcmp(s, "\xff\xf1", 2)) || (!memcmp(s, "\xff\xf9", 2)))
        return "audio/aac";
    if (!memcmp(s + 8, "AIFF", 4))
        return "audio/aiff";
    if (!memcmp(s, "fLaC", 4))
        return "audio/flac";
    if (!memcmp(s, "#!AMR", 4))
        return "audio/amr";
    if (!memcmp(s, "ID3", 3))
        return "audio/mpeg";          // mp3
    if (!memcmp(s, "MThd", 4))
        return "audio/midi";
    if (!memcmp(s, "OggS", 4)) //return "audio/ogg";
    {
        if (!memcmp(s + 28, "\x01""vorbis", 7) || !memcmp(s + 28, "\x7f""FLAC", 5))
            return "audio/ogg";
    }

    if (*s == '\xff')
    {
        if (memchr("\xE2\xE3\xF2\xF3\xFA\xFB", s[1], 6))
        {
            if (((s[2] & 0xF0) != 0xF0) && ((s[2] & 0xF0) != 0x00) && ((s[2] & 0x0F) < 0x0C))
                return "audio/mpeg";
        }
    }
    //------------------------------------------------------------------
    if (!memcmp(s, "FLV", 3))
        return "video/x-flv";            // flv
    if (!memcmp(s + 4, "ftyp3gp", 6))
        return "video/3gpp"; // 3gp
    if (!memcmp(s + 4, "ftypqt", 6))
        return "video/quicktime"; // mov
    if (!memcmp(s + 4, "ftyp", 4))
        return "video/mp4";         // mp4
    if (!memcmp(s, "\x1A\x45\xDF\xA3", 4))    // \x93\x42\x82\x88
        return "video/x-matroska";                            // mkv
    if (!memcmp(s, "OggS", 4))
        return "video/ogg";
    if (!memcmp(s + 4, "moov", 4))
        return "video/quicktime";
    if (!memcmp(s, "\x00\x00\x01\xBA", 4))
        return "video/mpeg";
    return "";
}
//======================================================================
const char *ismediafile(const char *path)
{
    FILE *f;

    f = fopen(path, "r");
    if (f == NULL)
    {
        print_err("<%s:%d> Error fopen(%s): %s\n", __func__, __LINE__, path, strerror(errno));
        return "";
    }

    const char *s = ismediafile_(f);
    fclose(f);

    return s;
}
//======================================================================
const char *content_type(const char *s)
{
    const char *p = strrchr(s, '.');

    if (!p)
        goto end;

    //       video
    if (!strlcmp_case(p, ".ogv", 4))
        return "video/ogg";
    else if (!strlcmp_case(p, ".mp4", 4))
        return "video/mp4";
    else if (!strlcmp_case(p, ".avi", 4))
        return "video/x-msvideo";
    else if (!strlcmp_case(p, ".mov", 4))
        return "video/quicktime";
    else if (!strlcmp_case(p, ".mkv", 4))
        return "video/x-matroska";
    else if (!strlcmp_case(p, ".flv", 4))
        return "video/x-flv";
    else if (!strlcmp_case(p, ".mpeg", 5) || !strlcmp_case(p, ".mpg", 4))
        return "video/mpeg";
    else if (!strlcmp_case(p, ".asf", 4))
        return "video/x-ms-asf";
    else if (!strlcmp_case(p, ".wmv", 4))
        return "video/x-ms-wmv";
    else if (!strlcmp_case(p, ".swf", 4))
        return "application/x-shockwave-flash";
    else if (!strlcmp_case(p, ".3gp", 4))
        return "video/video/3gpp";

    //       sound
    else if (!strlcmp_case(p, ".mp3", 4))
        return "audio/mpeg";
    else if (!strlcmp_case(p, ".wav", 4))
        return "audio/x-wav";
    else if (!strlcmp_case(p, ".ogg", 4))
        return "audio/ogg";
    else if (!strlcmp_case(p, ".pls", 4))
        return "audio/x-scpls";
    else if (!strlcmp_case(p, ".aac", 4))
        return "audio/aac";
    else if (!strlcmp_case(p, ".aif", 4))
        return "audio/x-aiff";
    else if (!strlcmp_case(p, ".ac3", 4))
        return "audio/ac3";
    else if (!strlcmp_case(p, ".voc", 4))
        return "audio/x-voc";
    else if (!strlcmp_case(p, ".flac", 5))
        return "audio/flac";
    else if (!strlcmp_case(p, ".amr", 4))
        return "audio/amr";
    else if (!strlcmp_case(p, ".au", 3))
        return "audio/basic";

    //       image
    else if (!strlcmp_case(p, ".gif", 4))
        return "image/gif";
    else if (!strlcmp_case(p, ".svg", 4) || !strlcmp_case(p, ".svgz", 5))
        return "image/svg+xml";
    else if (!strlcmp_case(p, ".png", 4))
        return "image/png";
    else if (!strlcmp_case(p, ".ico", 4))
        return "image/vnd.microsoft.icon";
    else if (!strlcmp_case(p, ".jpeg", 5) || !strlcmp_case(p, ".jpg", 4))
        return "image/jpeg";
    else if (!strlcmp_case(p, ".djvu", 5) || !strlcmp_case(p, ".djv", 4))
        return "image/vnd.djvu";
    else if (!strlcmp_case(p, ".tiff", 5))
        return "image/tiff";
    //       text
    else if (!strlcmp_case(p, ".txt", 4))
        return istextfile(s);
    else if (!strlcmp_case(p, ".html", 5) || !strlcmp_case(p, ".htm", 4) || !strlcmp_case(p, ".shtml", 6))
        return "text/html";
    else if (!strlcmp_case(p, ".css", 4))
        return "text/css";

    //       application
    else if (!strlcmp_case(p, ".pdf", 4))
        return "application/pdf";
    else if (!strlcmp_case(p, ".gz", 3))
        return "application/gzip";
end:
    p = ismediafile(s);
    if (p)
        if (strlen(p))
            return p;

    p = istextfile(s);
    if (p)
        if (strlen(p))
            return p;

    return "";
}
//======================================================================
int clean_path(char *path)
{
    int i = 0, o = 0;
    char ch;

    while ((ch = *(path + o)))
    {
        if (!memcmp(path+o, "/../", 4))
        {
            if (i != 0)
            {
                for (--i; i > 0; --i)
                {
                    if (*(path + i) == '/')
                        break;
                }
            }
            o += 3;
        }
        else if (!memcmp(path+o, "//", 2))
            o += 1;
        else if (!memcmp(path+o, "/./", 3))
            o += 2;
        else
        {
            if (!memcmp(path+o, "/.", 2))
                return -RS404;
            if (o != i)
                *(path + i) = ch;
            ++i;
            ++o;
        }
    }

    *(path + i) = 0;

    return i;
}
//======================================================================
const char *base_name(const char *path)
{
    const char *p;

    if (!path)
        return NULL;

    p = strrchr(path, '/');
    if (p)
        return p + 1;

    return path;
}
//======================================================================
int parse_startline_request(Connect *req, char *s)
{
    if (s == NULL)
    {
        print_err(req, "<%s:%d> Error: start line is empty\n",  __func__, __LINE__);
        return -1;
    }
    //----------------------------- method -----------------------------
    if (s[0] == ' ')
        return -1;
    char *p = s, *p_val;
    p_val = p;
    while (*p)
    {
        if (*p == ' ')
        {
            *p = 0;
            p++;
            break;
        }
        p++;
    }

    req->reqMethod = get_int_method(p_val);
    if (!req->reqMethod)
        return -RS400;
    //------------------------------- uri ------------------------------
    if (*p == ' ')
        return -RS400;
    req->uriLen = 0;
    p_val = p;
    while (*p)
    {
        if (*p == ' ')
        {
            *p = 0;
            p++;
            break;
        }
        p++;
        req->uriLen++;
    }

    req->uri = p_val;
    //------------------------------ version ---------------------------
    p_val = p;
    /*while (*p)
    {
        if (*p == ' ')
        {
            *p = 0;
            p++;
            break;
        }
        p++;
    }*/

    if (!(req->httpProt = get_int_http_prot(p_val)))
    {
        print_err(req, "<%s:%d> Error version protocol\n", __func__, __LINE__);
        req->httpProt = HTTP11;
        return -RS400;
    }
    return 0;
}
//======================================================================
int parse_headers(Connect *req, char *pName, int i)
{
    if (pName == NULL)
    {
        print_err(req, "<%s:%d> Error: header is empty\n",  __func__, __LINE__);
        return -1;
    }

    if (req->httpProt == HTTP09)
    {
        print_err(req, "<%s:%d> Error version protocol\n", __func__, __LINE__);
        return -1;
    }

    char *pVal, *p = pName, ch;
    int colon = 0;
    while ((ch = *p))
    {
        if (ch == ':')
            colon = 1;
        else if ((ch == ' ') || (ch == '\t') || (ch == '\n') || (ch == '\r'))
        {
            if (colon == 0)
                return -RS400;
            *(p++) = 0;
            break;
        }
        else
            *p = tolower(ch);
        p++;
    }

    if (*p == ' ')
        return -RS400;
    pVal = p;

    if (!strcmp(pName, "accept-encoding:"))
    {
        req->req_hd.iAcceptEncoding = i;
    }
    else if (!strcmp(pName, "connection:"))
    {
        req->req_hd.iConnection = i;
        if (strstr_case(pVal, "keep-alive"))
            req->connKeepAlive = 1;
        else
            req->connKeepAlive = 0;
    }
    else if (!strcmp(pName, "content-length:"))
    {
        req->req_hd.reqContentLength = atoll(pVal);
        req->req_hd.iReqContentLength = i;
        if (req->req_hd.iReqContentLength < 0)
            return -RS400;
    }
    else if (!strcmp(pName, "content-type:"))
    {
        req->req_hd.iReqContentType = i;
    }
    else if (!strcmp(pName, "host:"))
    {
        req->req_hd.iHost = i;
    }
    else if (!strcmp(pName, "if-range:"))
    {
        req->req_hd.iIfRange = i;
    }
    else if (!strcmp(pName, "range:"))
    {
        char *p = strchr(pVal, '=');
        if (p)
            req->sRange = p + 1;
        else
            req->sRange = NULL;

        req->req_hd.iRange = i;
    }
    else if (!strcmp(pName, "referer:"))
    {
        req->req_hd.iReferer = i;
    }
    else if (!strcmp(pName, "upgrade:"))
    {
        req->req_hd.iUpgrade = i;
    }
    else if (!strcmp(pName, "user-agent:"))
    {
        req->req_hd.iUserAgent = i;
    }

    req->reqHdValue[i] = pVal;

    return 0;
}
//======================================================================
int write_to_client(Connect *req, const char *buf, int len)
{
    int ret = send(req->clientSocket, buf, len, 0);
    if (ret == -1)
    {
        print_err(req, "<%s:%d> Error send(): %s\n", __func__, __LINE__, strerror(errno));
        if (errno == EAGAIN)
            return ERR_TRY_AGAIN;
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
            return ERR_TRY_AGAIN;
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
        if (n == ERR_TRY_AGAIN)
            return ERR_TRY_AGAIN;
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
