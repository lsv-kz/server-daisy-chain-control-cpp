#include "main.h"

using namespace std;

const char *status_resp(int st);
//======================================================================
int create_response_headers(Connect *req)
{
    get_time(req->sTime);
    req->resp_headers.s = "";
    req->resp_headers.s.reserve(512);
    if (req->resp_headers.s.error())
    {
        print_err(req, "<%s:%d> Error create String object\n", __func__, __LINE__);
        return -1;
    }

    req->resp_headers.s << get_str_http_prot(req->httpProt) << " " << status_resp(req->respStatus) << "\r\n"
        << "Date: " << req->sTime << "\r\n"
        << "Server: " << conf->ServerSoftware << "\r\n";

    if (req->reqMethod == M_OPTIONS)
        req->resp_headers.s << "Allow: OPTIONS, GET, HEAD, POST\r\n";

    if (req->numPart == 1)
    {
        if (req->respContentType)
            req->resp_headers.s << "Content-Type: " << req->respContentType << "\r\n";
        req->resp_headers.s << "Content-Length: " << req->respContentLength << "\r\n";

        req->resp_headers.s << "Content-Range: bytes " << req->offset << "-"
                                        << (req->offset + req->respContentLength - 1)
                                        << "/" << req->fileSize << "\r\n";
    }
    else if (req->numPart == 0)
    {
        if (req->respContentType)
            req->resp_headers.s << "Content-Type: " << req->respContentType << "\r\n";
        if (req->respContentLength >= 0)
        {
            req->resp_headers.s << "Content-Length: " << req->respContentLength << "\r\n";
            if (req->respStatus == RS200)
                req->resp_headers.s << "Accept-Ranges: bytes\r\n";
        }

        if (req->respStatus == RS416)
            req->resp_headers.s << "Content-Range: bytes */" << req->fileSize << "\r\n";
    }

    if (req->respStatus == RS101)
    {
        req->resp_headers.s << "Upgrade: HTTP/1.1\r\n"
            << "Connection: Upgrade\r\n";
    }
    else
        req->resp_headers.s << "Connection: " << (req->connKeepAlive == 0 ? "close" : "keep-alive") << "\r\n";

    if (req->mode_send == CHUNK)
        req->resp_headers.s << "Transfer-Encoding: chunked\r\n";

    if (req->hdrs.size())
    {
        req->resp_headers.s << req->hdrs.c_str();
        req->hdrs = "";
    }

    req->resp_headers.s << "\r\n";

    if (req->resp_headers.s.error())
    {
        print_err(req, "<%s:%d> Error create response headers\n", __func__, __LINE__);
        req->req_hd.iReferer = MAX_HEADERS - 1;
        req->reqHdValue[req->req_hd.iReferer] = "Error create response headers";
        return -1;
    }

    return 0;
}
//======================================================================
int send_message(Connect *r, const char *msg)
{
    r->html.s = "";
    r->html.s.reserve(256);

    if (r->httpProt == 0)
        r->httpProt = HTTP11;

    if (r->respStatus != RS204)
    {
        const char *title = status_resp(r->respStatus);
        r->html.s << "<html>\r\n"
                "<head>\r\n"
                "<title>" << title << "</title>\r\n"
                "<meta charset=\"utf-8\">\r\n"
                "</head>\r\n"
                "<body>\r\n"
                "<h3>" << title << "</h3>\r\n";
        if (msg)
            r->html.s << "<p>" << msg <<  "</p>\r\n";
        r->html.s << "<hr>\r\n" << r->sTime << "\r\n"
                "</body>\r\n"
                "</html>";

        r->respContentType = "text/html";
        r->html.len = r->respContentLength = r->html.s.size();
        r->html.p = r->html.s.c_str();
    }
    else // (r->respStatus == RS204)
    {
        r->respContentLength = 0;
        r->respContentType = NULL;
    }

    r->mode_send = NO_CHUNK;
    if ((r->httpProt != HTTP09) && create_response_headers(r))
        return -1;

    r->resp_headers.p = r->resp_headers.s.c_str();
    r->resp_headers.len = r->resp_headers.s.size();
    push_send_html(r);
    return 1;
}
//======================================================================
const char *status_resp(int st)
{
    switch (st)
    {
        case 0:
            return "";
        case RS101:
            return "101 Switching Protocols";
        case RS200:
            return "200 OK";
        case RS204:
            return "204 No Content";
        case RS206:
            return "206 Partial Content";
        case RS301:
            return "301 Moved Permanently";
        case RS302:
            return "302 Moved Temporarily";
        case RS400:
            return "400 Bad Request";
        case RS401:
            return "401 Unauthorized";
        case RS402:
            return "402 Payment Required";
        case RS403:
            return "403 Forbidden";
        case RS404:
            return "404 Not Found";
        case RS405:
            return "405 Method Not Allowed";
        case RS406:
            return "406 Not Acceptable";
        case RS407:
            return "407 Proxy Authentication Required";
        case RS408:
            return "408 Request Timeout";
        case RS411:
            return "411 Length Required";
        case RS413:
            return "413 Request entity too large";
        case RS414:
            return "414 Request-URI Too Large";
        case RS416:
            return "416 Range Not Satisfiable";
        case RS500:
            return "500 Internal Server Error";
        case RS501:
            return "501 Not Implemented";
        case RS502:
            return "502 Bad Gateway";
        case RS503:
            return "503 Service Unavailable";
        case RS504:
            return "504 Gateway Time-out";
        case RS505:
            return "505 HTTP Version not supported";
        default:
            return "500 Internal Server Error";
    }
    return "";
}
