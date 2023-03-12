#include "main.h"

using namespace std;

int flog = STDOUT_FILENO, flog_err = STDERR_FILENO;
mutex mtxLog;
//======================================================================
void create_logfiles(const string& log_dir)
{
    char buf[256];
    struct tm tm1;
    time_t t1;

    time(&t1);
    tm1 = *localtime(&t1);
    strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M", &tm1);

    String fileName;
    fileName << log_dir << '/' << buf << '-' << conf->ServerSoftware << ".log";

    flog = open(fileName.c_str(), O_CREAT | O_APPEND | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (flog == -1)
    {
        cerr << "  Error create log: " << fileName.c_str() << "\n";
        exit(1);
    }
    //------------------------------------------------------------------
    fileName.clear();
    fileName << log_dir << "/error_" << buf << '_' << conf->ServerSoftware << ".log";

    flog_err = open(fileName.c_str(), O_CREAT | O_APPEND | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (flog_err == -1)
    {
        cerr << "  Error create log_err: " << fileName.c_str() << "\n";
        exit(1);
    }

    dup2(flog_err, STDERR_FILENO);
}
//======================================================================
void close_logs(void)
{
    close(flog);
    close(flog_err);
}
//======================================================================
void print_err(const char *format, ...)
{
    va_list ap;
    char buf[300];

    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    String ss(256);
    ss << "[" << log_time() << "] - " << buf;
mtxLog.lock();
    write(flog_err, ss.c_str(), ss.size());
mtxLog.unlock();
}
//======================================================================
void print_err(Connect *req, const char *format, ...)
{
    va_list ap;
    char buf[300];

    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    String ss(256);
    ss << "[" << log_time() << "]-[" << req->numProc << "/" << req->numConn << "/" << req->numReq << "] " << buf;

mtxLog.lock();
    write(flog_err, ss.c_str(), ss.size());
mtxLog.unlock();
}
//======================================================================
void print_log(Connect *req)
{
    String ss(320);
    if (req->reqMethod <= 0)
    {
        ss  << req->numProc << "/" << req->numConn << "/" << req->numReq << " - " << req->remoteAddr << ":" << req->remotePort
            << " - [" << log_time() << "] - \"-\" "
            << req->respStatus << " " << req->send_bytes
            << " \"" << ((req->req_hd.iReferer >= 0) ? req->reqHdValue[req->req_hd.iReferer] : "-") << "\" \"-\"\n";
    }
    else
    {
        ss  << req->numProc << "/" << req->numConn << "/" << req->numReq << " - " << req->remoteAddr << ":" << req->remotePort
            << " - [" << log_time() << "] - \"" << get_str_method(req->reqMethod) << " " << req->decodeUri
            << ((req->sReqParam) ? "?" : "") << ((req->sReqParam) ? req->sReqParam : "") << " "
            << get_str_http_prot(req->httpProt) << "\" "
            << req->respStatus << " " << req->send_bytes << " "
            << "\"" << ((req->req_hd.iReferer >= 0) ? req->reqHdValue[req->req_hd.iReferer] : "-") << "\" "
            << "\"" << ((req->req_hd.iUserAgent >= 0) ? req->reqHdValue[req->req_hd.iUserAgent] : "-") << "\"\n";
    }
//mtxLog.lock();
    write(flog, ss.c_str(), ss.size());
//mtxLog.unlock();
}
