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
int index_chunked(Connect *req, char **list, int numFiles, string& path)
{
    const int len_path = path.size();
    int n, i;
    long long size;
    struct stat st;
    int chunk;
    if (req->reqMethod == M_HEAD)
        chunk = NO_SEND;
    else
        chunk = ((req->httpProt == HTTP11) && req->connKeepAlive) ? SEND_CHUNK : SEND_NO_CHUNK;

    ClChunked chunk_buf(req, chunk);

    req->respStatus = RS200;
    String hdrs(64);

    if (chunk == SEND_CHUNK)
    {
        hdrs << "Transfer-Encoding: chunked\r\n";
    }

    hdrs << "Content-Type: text/html\r\n";
    req->respContentLength = -1;

    if (chunk)
    {
        if (send_response_headers(req, &hdrs))
        {
            return -1;
        }
    }
    //------------------------------------------------------------------
    chunk_buf << "<!DOCTYPE HTML>\r\n"
            "<html>\r\n"
            " <head>\r\n"
            "  <meta charset=\"UTF-8\">\r\n"
            "  <title>Index of " << req->decodeUri << " (ch)</title>\r\n"
            "  <style>\r\n"
            "    body {\r\n"
            "     margin-left:100px; margin-right:50px;\r\n"
            "    }\r\n"
            "  </style>\r\n"
            "  <link href=\"/styles.css\" type=\"text/css\" rel=\"stylesheet\">\r\n"
            " </head>\r\n"
            " <body id=\"top\">\r\n"
            "  <h3>Index of " << req->decodeUri << "</h3>\r\n"
            "  <table border=\"0\" width=\"100\%\">\r\n"
            "   <tr><td><h3>Directories</h3></td></tr>\r\n";
    if (chunk_buf.error())
    {
        print_err("<%s:%d>   Error chunk\n", __func__, __LINE__);
        return -1;
    }
    //------------------------------------------------------------------
    if (!strcmp(req->decodeUri, "/"))
        chunk_buf << "   <tr><td></td></tr>\r\n";
    else
        chunk_buf << "   <tr><td><a href=\"../\">Parent Directory/</a></td></tr>\r\n";
    if (chunk_buf.error())
    {
        print_err(req, "<%s:%d>   Error chunk\n", __func__, __LINE__);
        return -1;
    }
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
            print_err(req, "<%s:%d> Error: encode()\n", __func__, __LINE__);
            continue;
        }

        chunk_buf << "   <tr><td><a href=\"" << buf << "/\">" << list[i] << "/</a></td></tr>\r\n";
        if (chunk_buf.error())
        {
            print_err(req, "<%s:%d>   Error chunk\n", __func__, __LINE__);
            return -1;
        }
    }
    //------------------------------------------------------------------
    chunk_buf << "  </table>\r\n   <hr>\r\n  <table border=\"0\" width=\"100\%\">\r\n"
                "   <tr><td><h3>Files</h3></td><td></td></tr>\r\n";
    if (chunk_buf.error())
    {
        print_err(req, "<%s:%d>   Error chunk\n", __func__, __LINE__);
        return -1;
    }
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
            print_err(req, "<%s:%d> Error: encode()\n", __func__, __LINE__);
            continue;
        }

        size = (long long)st.st_size;

        if (isimage(list[i]) && (conf->ShowMediaFiles == 'y'))
            chunk_buf << "   <tr><td><a href=\"" << buf << "\"><img src=\"" << buf << "\" width=\"100\"></a>" << list[i] << "</td>"
                      << "<td align=\"right\">" << size << " bytes</td></tr>\r\n";
        else if (isaudiofile(list[i]) && (conf->ShowMediaFiles == 'y'))
            chunk_buf << "   <tr><td><audio preload=\"none\" controls src=\"" << buf << "\"></audio><a href=\""
                      << buf << "\">" << list[i] << "</a></td><td align=\"right\">" << size << " bytes</td></tr>\r\n";
        else
            chunk_buf << "   <tr><td><a href=\"" << buf << "\">" << list[i] << "</a></td><td align=\"right\">"
                      << size << " bytes</td></tr>\r\n";

        if (chunk_buf.error())
        {
            print_err(req, "<%s:%d>   Error chunk\n", __func__, __LINE__);
            return -1;
        }
    }
    //------------------------------------------------------------------
    chunk_buf << "  </table>\r\n"
              "  <hr>\r\n"
              "  " << req->sLogTime << "\r\n"
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
              "</html>\r\n";
    if (chunk_buf.error())
    {
        print_err(req, "<%s:%d>   Error chunk\n", __func__, __LINE__);
        return -1;
    }
    //------------------------------------------------------------------
    n = chunk_buf.end();
    req->respContentLength = chunk_buf.all();
    if (n < 0)
    {
        req->send_bytes = chunk_buf.all();
        print_err(req, "<%s:%d>   Error chunk_buf.end(): %d\n", __func__, __LINE__, n);
        return -1;
    }

    if (chunk == NO_SEND)
    {
        if (send_response_headers(req, &hdrs))
        {
            print_err("<%s:%d> Error send_header_response()\n", __func__, __LINE__);
            return -1;
        }
    }
    else
        req->send_bytes = req->respContentLength;

    return 0;
}
//======================================================================
int index_dir(Connect *req, string& path)
{
    DIR *dir;
    struct dirent *dirbuf;
    int maxNumFiles = 1024, numFiles = 0;
    char *list[maxNumFiles];
    int ret;

    path += '/';

    dir = opendir(path.c_str());
    if (dir == NULL)
    {
        if (errno == EACCES)
            return -RS403;
        else
        {
            print_err(req, "<%s:%d>  Error opendir(\"%s\"): %s\n", __func__, __LINE__, path.c_str(), strerror(errno));
            return -RS500;
        }
    }

    while ((dirbuf = readdir(dir)))
    {
        if (numFiles >= maxNumFiles )
        {
            print_err(req, "<%s:%d> number of files per directory >= %d\n", __func__, __LINE__, numFiles);
            break;
        }

        if (dirbuf->d_name[0] == '.')
            continue;
        list[numFiles] = dirbuf->d_name;
        ++numFiles;
    }

    qsort(list, numFiles, sizeof(char *), cmp);
    ret = index_chunked(req, list, numFiles, path);

    closedir(dir);

    return ret;
}
