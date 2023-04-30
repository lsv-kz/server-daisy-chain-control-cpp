#ifndef SERVER_H_
#define SERVER_H_
#define _FILE_OFFSET_BITS 64

#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <climits>
#include <iomanip>
#include <vector>

#include <mutex>
#include <thread>
#include <condition_variable>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include <sys/resource.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/un.h>

#include "string__.h"
#include "ranges.h"

#define    LINUX_
//#define    FREEBSD_
#define    SEND_FILE_
#define    TCP_CORK_

#define     MAX_PATH          2048
#define     MAX_NAME           256
#define     SIZE_BUF_REQUEST  8192
#define     MAX_HEADERS         25

const char boundary[] = "---------a9b5r7a4c0a2d5a1b8r3a";

enum {
    RS101 = 101,
    RS200 = 200,RS204 = 204,RS206 = 206,
    RS301 = 301, RS302,
    RS400 = 400,RS401,RS402,RS403,RS404,RS405,RS406,RS407,
    RS408,RS411 = 411,RS413 = 413,RS414,RS415,RS416,RS417,RS418,
    RS500 = 500,RS501,RS502,RS503,RS504,RS505
};
enum {
    M_GET = 1, M_HEAD, M_POST, M_OPTIONS, M_PUT,
    M_PATCH, M_DELETE, M_TRACE, M_CONNECT
};
enum { HTTP09 = 1, HTTP10, HTTP11, HTTP2 };
enum { EXIT_THR = 1 };

enum MODE_SEND { NO_CHUNK, CHUNK, CHUNK_END };
enum SOURCE_ENTITY { ENTITY_NONE, FROM_FILE, FROM_DATA_BUFFER, MULTIPART_ENTITY, };
enum OPERATION_TYPE { READ_REQUEST = 1, SEND_RESP_HEADERS, SEND_ENTITY, DYN_PAGE, };
enum MULTIPART { SEND_HEADERS = 1, SEND_PART, SEND_END };
enum IO_STATUS { POLL = 1, WORK };

enum CGI_TYPE { CGI_TYPE_NONE, CGI, PHPCGI, PHPFPM, FASTCGI, SCGI, };
enum DIRECT { FROM_CGI = 1, TO_CGI, FROM_CLIENT, TO_CLIENT };

enum FCGI_OPERATION { FASTCGI_CONNECT, FASTCGI_BEGIN, FASTCGI_PARAMS, FASTCGI_STDIN, 
                      FASTCGI_READ_HTTP_HEADERS, FASTCGI_SEND_HTTP_HEADERS, 
                      FASTCGI_READ_ENTITY, FASTCGI_READ_ERROR, FASTCGI_CLOSE };

enum FCGI_STATUS {FCGI_READ_DATA,  FCGI_READ_HEADER, FCGI_READ_PADDING }; 

enum CGI_OPERATION { CGI_CREATE_PROC = 1, CGI_STDIN, CGI_READ_HTTP_HEADERS, CGI_SEND_HTTP_HEADERS, CGI_READ_ENTITY };

enum SCGI_OPERATION { SCGI_CONNECT = 1, SCGI_PARAMS, SCGI_STDIN, SCGI_READ_HTTP_HEADERS, SCGI_SEND_HTTP_HEADERS, SCGI_READ_ENTITY };

const int PROC_LIMIT = 8;

void print_err(const char *format, ...);

union OPERATION { CGI_OPERATION cgi; FCGI_OPERATION fcgi; SCGI_OPERATION scgi;};
struct Cgi
{
    OPERATION op;
    DIRECT dir;
    char buf[8 + 4096 + 8];
    int  size_buf = 4096;
    long len_buf;
    long len_post;
    char *p;

    pid_t pid;
    int  to_script;
    int  from_script;
};

typedef struct fcgi_list_addr {
    std::string script_name;
    std::string addr;
    CGI_TYPE type;
    struct fcgi_list_addr *next;
} fcgi_list_addr;

struct Param
{
    std::string name;
    std::string val;
};
/* ---------------------------------------------------------------------
 *                  Commands send to next process
 * CONNECT_IGN    : The next process MUST NOT receive requests from the client
 * CONNECT_ALLOW  : The next process MAY receive requests from client
 * PROC_CLOSE     : Close next process
 */
enum { CONNECT_IGN, CONNECT_ALLOW, PROC_CLOSE };
//----------------------------------------------------------------------
struct Config
{
    std::string ServerSoftware;

    std::string ServerAddr;
    std::string ServerPort;

    std::string DocumentRoot;
    std::string ScriptPath;
    std::string LogPath;
    std::string PidFilePath;

    std::string UsePHP;
    std::string PathPHP;

    int ListenBacklog;
    char TcpCork;
    char TcpNoDelay;

    char SendFile;
    int SndBufSize;

    unsigned int NumCpuCores;

    int MaxWorkConnections;

    unsigned int NumProc;
    unsigned int MaxNumProc;
    unsigned int MaxThreads;
    unsigned int MinThreads;
    unsigned int MaxCgiProc;

    unsigned int MaxRanges;
    long int ClientMaxBodySize;

    int MaxRequestsPerClient;
    int TimeoutKeepAlive;
    int Timeout;
    int TimeoutCGI;
    int TimeoutPoll;

    char AutoIndex;
    char index_html;
    char index_php;
    char index_pl;
    char index_fcgi;

    char ShowMediaFiles;

    std::string user;
    std::string group;

    uid_t server_uid;
    gid_t server_gid;

    fcgi_list_addr *fcgi_list;
    //------------------------------------------------------------------
    Config()
    {
        fcgi_list = NULL;
    }

    ~Config()
    {
        free_fcgi_list();
        //std::cout << __func__ << " ******* " << getpid() << " *******\n";
    }

    void free_fcgi_list()
    {
        fcgi_list_addr *t;
        while (fcgi_list)
        {
            t = fcgi_list;
            fcgi_list = fcgi_list->next;
            if (t)
                delete t;
        }
    }
};
//----------------------------------------------------------------------
extern const Config* const conf;
//======================================================================
class Connect
{
public:
    Connect *prev;
    Connect *next;

    static int serverSocket;

    unsigned int numProc, numConn, numReq;
    int       clientSocket;
    int       err;
    time_t    sock_timer;
    int       timeout;
    int       event;
    OPERATION_TYPE operation;
    IO_STATUS    io_status;

    char      remoteAddr[NI_MAXHOST];
    char      remotePort[NI_MAXSERV];

    struct
    {
        char      buf[SIZE_BUF_REQUEST];
        int       len;
    } req;

    char      *p_newline;
    char      *tail;
    int       lenTail;

    char      decodeUri[SIZE_BUF_REQUEST];
    unsigned int lenDecodeUri;

    char      *uri;
    unsigned int uriLen;
    //------------------------------------------------------------------
    const char *sReqParam;
    char      *sRange;

    int       reqMethod;
    int       httpProt;
    int       connKeepAlive;

    struct
    {
        int  iConnection;
        int  iHost;
        int  iUserAgent;
        int  iReferer;
        int  iUpgrade;
        int  iReqContentType;
        int  iReqContentLength;
        int  iAcceptEncoding;
        int  iRange;
        int  iIfRange;
        long long reqContentLength;
    } req_hd;

    int  countReqHeaders;
    char  *reqHdName[MAX_HEADERS + 1];
    const char  *reqHdValue[MAX_HEADERS + 1];
    //--------------------------------------
    struct
    {
        String s;
        const char *p;
        int len;
    } resp_headers;

    String hdrs;

    struct
    {
        String s;
        const char *p;
        int len;
    } html;

    String scriptName;
    CGI_TYPE cgi_type;
    Cgi *cgi;

    struct
    {
        bool http_headers_received;
        FCGI_STATUS status;
        int fd;

        int i_param;
        int size_par;
        std::vector <Param> vPar;

        unsigned char fcgi_type;
        int dataLen;
        int paddingLen;
        char *ptr_wr;
        char *ptr_rd;
        char buf[2048];
        int size_buf = 2047;
        int len_buf;
    } fcgi;

    Ranges rg;
    struct
    {
        MULTIPART status;
        Range *rg;
        String hdr;
    } mp;

    SOURCE_ENTITY source_entity;
    MODE_SEND mode_send;

    std::string sTime;
    int respStatus;
    int numPart;
    long long respContentLength;
    const char *respContentType;
    long long fileSize;
    int fd;
    off_t offset;
    long long send_bytes;

    void init();
};
//----------------------------------------------------------------------
class RequestManager
{
private:
    Connect *list_start;
    Connect *list_end;

    std::mutex mtx_thr;

    std::condition_variable cond_list;
    std::condition_variable cond_new_thr, cond_exit_thr;

    unsigned int num_wait_thr, size_list, all_thr;
    unsigned int count_thr, stop_manager;

    unsigned int NumProc;

    RequestManager() {}
public:
    RequestManager(const RequestManager&) = delete;
    RequestManager(unsigned int);
    ~RequestManager();
    //-------------------------------
    int get_num_chld(void);
    int get_num_thr(void);
    int get_all_thr(void);
    int start_thr(void);
    void wait_exit_thr(unsigned int n);
    friend void push_resp_list(Connect *req, RequestManager *);
    Connect *pop_resp_list();
    int end_thr(int);
    int wait_create_thr(int*);
    void close_manager();
};
//----------------------------------------------------------------------
extern char **environ;
//----------------------------------------------------------------------
void response1(RequestManager *ReqMan);
int response2(Connect *req);
int options(Connect *req);
int index_dir(Connect *req, std::string& path);
//----------------------------------------------------------------------
int create_fcgi_socket(const char *host);
//----------------------------------------------------------------------
int encode(const char *s_in, char *s_out, int len_out);
int decode(const char *s_in, int len_in, char *s_out, int len);
//----------------------------------------------------------------------
int read_from_pipe(int fd, char *buf, int len, int timeout);
int hd_read(Connect* req);
int write_to_client(Connect *req, const char *buf, int len);
int read_from_client(Connect *req, char *buf, int len);
//----------------------------------------------------------------------
int send_message(Connect *req, const char *msg);
int create_response_headers(Connect *req);
//----------------------------------------------------------------------
std::string get_time();
void get_time(std::string& s);
std::string log_time();

const char *strstr_case(const char * s1, const char *s2);
int strlcmp_case(const char *s1, const char *s2, int len);

int get_int_method(const char *s);
const char *get_str_method(int i);

int get_int_http_prot(const char *s);
const char *get_str_http_prot(int i);

const char *get_str_operation(int i);

int clean_path(char *path);
const char *content_type(const char *s);

const char *base_name(const char *path);
int parse_startline_request(Connect *req, char *s);
int parse_headers(Connect *req, char *s, int n);

const char *get_str_operation(OPERATION_TYPE n);
const char *get_cgi_operation(CGI_OPERATION n);
const char *get_fcgi_operation(FCGI_OPERATION n);
const char *get_fcgi_status(FCGI_STATUS n);
const char *get_scgi_operation(SCGI_OPERATION n);
const char *get_cgi_type(CGI_TYPE n);
const char *get_cgi_dir(DIRECT n);
//----------------------------------------------------------------------
void create_logfiles(const std::string &);
void close_logs(void);
void print_err(Connect *req, const char *format, ...);
void print_log(Connect *req);
//----------------------------------------------------------------------
int timedwait_close_cgi();
void cgi_dec();
//----------------------------------------------------------------------
void end_response(Connect *req);
void close_connect(Connect *req);
//----------------------------------------------------------------------
void event_handler(RequestManager *ReqMan);
void push_pollin_list(Connect *req);
void push_send_file(Connect *req);
void push_send_multipart(Connect *req);
void push_send_html(Connect *req);
void close_event_handler();
//----------------------------------------------------------------------
int set_max_fd(int max_open_fd);
//----------------------------------------------------------------------
void cgi_handler(RequestManager *ReqMan);
void push_cgi(Connect *req);
void close_cgi_handler(void);

#endif
