#include "classes.h"

using namespace std;
//======================================================================
int isimage(const char *name)
{
    const char *p;

    if (!(p = strrchr(name, '.')))
        return 0;

    if (!strlcmp_case(p, ".gif", 4))
        return 1;
    else if (!strlcmp_case(p, ".png", 4))
        return 1;
    else if (!strlcmp_case(p, ".svg", 4))
        return 1;
    else if (!strlcmp_case(p, ".jpeg", 5) || !strlcmp_case(p, ".jpg", 4))
        return 1;
    return 0;
}
//======================================================================
int isaudiofile(const char *name)
{
    const char *p;

    if (!(p = strrchr(name, '.')))
        return 0;

    if (!strlcmp_case(p, ".wav", 4))
        return 1;
    else if (!strlcmp_case(p, ".mp3", 4))
        return 1;
    else if (!strlcmp_case(p, ".ogg", 4))
        return 1;
    return 0;
}
//======================================================================
int cmp(const void *a, const void *b)
{
    unsigned int n1, n2;
    int i;

    if ((n1 = atoi(*(char **)a)) > 0)
    {
        if ((n2 = atoi(*(char **)b)) > 0)
        {
            if (n1 < n2)
                i = -1;
            else if (n1 == n2)
                i = strcmp(*(char **)a, *(char **)b);
            else
                i = 1;
        }
        else
            i = strcmp(*(char **)a, *(char **)b);
    }
    else
        i = strcmp(*(char **)a, *(char **)b);

    return i;
}
//======================================================================
int create_index_html(Connect *r, char **list, int numFiles, string& path)
{
    const int len_path = path.size();
    int n, i;
    long long size;
    struct stat st;

    if (r->reqMethod == M_HEAD)
        return 0;
    //------------------------------------------------------------------
    r->resp_headers.s << "<!DOCTYPE HTML>\r\n"
            "<html>\r\n"
            " <head>\r\n"
            "  <meta charset=\"UTF-8\">\r\n"
            "  <title>Index of " << r->decodeUri << " (ch)</title>\r\n"
            "  <style>\r\n"
            "    body {\r\n"
            "     margin-left:100px; margin-right:50px;\r\n"
            "    }\r\n"
            "  </style>\r\n"
            "  <link href=\"/styles.css\" type=\"text/css\" rel=\"stylesheet\">\r\n"
            " </head>\r\n"
            " <body id=\"top\">\r\n"
            "  <h3>Index of " << r->decodeUri << "</h3>\r\n"
            "  <table border=\"0\" width=\"100\%\">\r\n"
            "   <tr><td><h3>Directories</h3></td></tr>\r\n";
    //------------------------------------------------------------------
    if (!strcmp(r->decodeUri, "/"))
        r->resp_headers.s << "   <tr><td></td></tr>\r\n";
    else
        r->resp_headers.s << "   <tr><td><a href=\"../\">Parent Directory/</a></td></tr>\r\n";
    //-------------------------- Directories ---------------------------
    for (i = 0; (i < numFiles); i++)
    {
        char buf[1024];
        path += list[i];
        n = lstat(path.c_str(), &st);
        path.resize(len_path);
        if ((n == -1) || !S_ISDIR (st.st_mode))
            continue;

        if (!encode(list[i], buf, sizeof(buf)))
        {
            print_err(r, "<%s:%d> Error: encode()\n", __func__, __LINE__);
            continue;
        }

        r->resp_headers.s << "   <tr><td><a href=\"" << buf << "/\">" << list[i] << "/</a></td></tr>\r\n";
    }
    //------------------------------------------------------------------
    r->resp_headers.s << "  </table>\r\n   <hr>\r\n  <table border=\"0\" width=\"100\%\">\r\n"
                "   <tr><td><h3>Files</h3></td><td></td></tr>\r\n";
    //---------------------------- Files -------------------------------
    for (i = 0; i < numFiles; i++)
    {
        char buf[1024];
        path += list[i];
        n = lstat(path.c_str(), &st);
        path.resize(len_path);
        if ((n == -1) || !S_ISREG (st.st_mode))
            continue;
        else if (!strcmp(list[i], "favicon.ico"))
            continue;

        if (!encode(list[i], buf, sizeof(buf)))
        {
            print_err(r, "<%s:%d> Error: encode()\n", __func__, __LINE__);
            continue;
        }

        size = (long long)st.st_size;

        if (isimage(list[i]) && (conf->ShowMediaFiles == 'y'))
            r->resp_headers.s << "   <tr><td><a href=\"" << buf << "\"><img src=\"" << buf << "\" width=\"100\"></a>" << list[i] << "</td>"
                      << "<td align=\"right\">" << size << " bytes</td></tr>\r\n";
        else if (isaudiofile(list[i]) && (conf->ShowMediaFiles == 'y'))
            r->resp_headers.s << "   <tr><td><audio preload=\"none\" controls src=\"" << buf << "\"></audio><a href=\""
                      << buf << "\">" << list[i] << "</a></td><td align=\"right\">" << size << " bytes</td></tr>\r\n";
        else
            r->resp_headers.s << "   <tr><td><a href=\"" << buf << "\">" << list[i] << "</a></td><td align=\"right\">"
                      << size << " bytes</td></tr>\r\n";
    }
    //------------------------------------------------------------------
    r->resp_headers.s << "  </table>\r\n"
              "  <hr>\r\n"
              "  " << r->sTime << "\r\n"
              "  <a href=\"#top\" style=\"display:block;\r\n"
              "         position:fixed;\r\n"
              "         bottom:30px;\r\n"
              "         left:10px;\r\n"
              "         width:50px;\r\n"
              "         height:40px;\r\n"
              "         font-size:60px;\r\n"
              "         background:gray;\r\n"
              "         border-radius:10px;\r\n"
              "         color:black;\r\n"
              "         opacity: 0.7\">^</a>\r\n"
              " </body>\r\n"
              "</html>";
    //------------------------------------------------------------------
    r->respContentLength = r->resp_headers.s.size();

    return r->respContentLength;
}
//======================================================================
int index_dir(Connect *r)
{
    DIR *dir;
    struct dirent *dirbuf;
    int maxNumFiles = 1024, numFiles = 0;
    char *list[maxNumFiles];
    int ret;

    string path;
    path.reserve(1 + r->lenDecodeUri + 16);
    path += '.';
    path += r->decodeUri;
    if (path[path.size()-1] != '/')
        path += '/';

    dir = opendir(path.c_str());
    if (dir == NULL)
    {
        if (errno == EACCES)
            return -RS403;
        else
        {
            print_err(r, "<%s:%d>  Error opendir(\"%s\"): %s\n", __func__, __LINE__, path.c_str(), strerror(errno));
            return -RS500;
        }
    }

    while ((dirbuf = readdir(dir)))
    {
        if (numFiles >= maxNumFiles )
        {
            print_err(r, "<%s:%d> number of files per directory >= %d\n", __func__, __LINE__, numFiles);
            break;
        }

        if (dirbuf->d_name[0] == '.')
            continue;
        list[numFiles] = dirbuf->d_name;
        ++numFiles;
    }

    qsort(list, numFiles, sizeof(char *), cmp);
    ret = create_index_html(r, list, numFiles, path);
    closedir(dir);
    if (ret >= 0)
    {
        r->resp_headers.p = r->resp_headers.s.c_str();
        r->resp_headers.len = r->resp_headers.s.size();
    }

    return ret;
}
//======================================================================
extern struct pollfd *cgi_poll_fd;

int cgi_set_size_chunk(Connect *r);
void cgi_del_from_list(Connect *r);
//======================================================================
void index_set_poll_list(Connect *r, int *i)
{
    if (r->operation == INDEX)
    {
        r->hdrs.reserve(64);
        r->hdrs << "Content-Type: text/html\r\n";
        r->respContentLength = -1;
        r->mode_send = ((r->httpProt == HTTP11) && r->connKeepAlive) ? CHUNK : NO_CHUNK;
        if (create_response_headers(r))
        {
            print_err(r, "<%s:%d> Error create_response_headers()\n", __func__, __LINE__);
            r->err = -1;
            cgi_del_from_list(r);
            end_response(r);
            return;
        }
        else
        {
            r->resp_headers.p = r->resp_headers.s.c_str();
            r->resp_headers.len = r->resp_headers.s.size();
            r->operation = SEND_RESP_HEADERS;
            r->cgi->dir = IN;
        }
    }
    else if (r->operation == SEND_RESP_HEADERS)
    {
        if (r->resp_headers.len == 0)
        {
            r->resp_headers.s = "";
            int ret = index_dir(r);
            if (ret == 0)
            {
                cgi_del_from_list(r);
                end_response(r);
                return;
            }
            else if (ret < 0)
            {
                r->err = -1;
                cgi_del_from_list(r);
                end_response(r);
                return;
            }

            r->operation = SEND_ENTITY;
            r->cgi->dir = IN;
        }
    }
    else if (r->operation == SEND_ENTITY)
    {
        if (r->resp_headers.len == 0)
        {
            if (r->mode_send == CHUNK)
            {
                r->cgi->len_buf = 0;
                cgi_set_size_chunk(r);
                r->cgi->dir = OUT;
                r->mode_send = CHUNK_END;
            }
            else
            {
                cgi_del_from_list(r);
                end_response(r);
                return;
            }
        }
    }

    cgi_poll_fd[*i].fd = r->clientSocket;
    cgi_poll_fd[*i].events = POLLOUT;
    (*i)++;
}
//----------------------------------------------------------------------
int index_(Connect *r)
{
    if (r->cgi->dir == IN)
    {
        int len = (r->resp_headers.len > r->cgi->size_buf) ? r->cgi->size_buf : r->resp_headers.len;
        memcpy(r->cgi->buf + 8, r->resp_headers.p, len);
        r->resp_headers.p += len;
        r->resp_headers.len -= len;
        
        r->cgi->len_buf = len;
        if ((r->mode_send == CHUNK) && (r->operation == SEND_ENTITY))
        {
            if (cgi_set_size_chunk(r))
            {
                r->err = -1;
                cgi_del_from_list(r);
                end_response(r);
                return -1;
            }
        }
        else
            r->cgi->p = r->cgi->buf + 8;
        r->cgi->dir = OUT;
    }

    int ret = write(r->clientSocket, r->cgi->p, r->cgi->len_buf);
    if (ret == -1)
    {
        print_err(r, "<%s:%d> Error send to client: %s\n", __func__, __LINE__, strerror(errno));
        if (errno == EAGAIN)
            return -EAGAIN;
        return -1;
    }

    r->cgi->p += ret;
    r->cgi->len_buf -= ret;
    r->send_bytes += ret;

    if (r->cgi->len_buf == 0)
        r->cgi->dir = IN;
    else
        r->cgi->dir = OUT;
        
    return ret;
}

