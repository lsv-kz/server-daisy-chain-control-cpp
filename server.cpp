#include "main.h"

using namespace std;

static int sockServer;
int Connect::serverSocket;

int create_server_socket(const Config *c);
int read_conf_file(const char *path_conf);
void free_fcgi_list();
int set_uid();
pid_t create_child(int, unsigned int, int *, int, char);
static int main_proc();

static string pidFile;
static string conf_path;

static int pfd[2], pfd_in;

static int start = 0, restart = 1;

static pid_t pid_child;
//======================================================================
static void signal_handler(int sig)
{
    if (sig == SIGINT)
    {
        print_err("<main> ####### SIGINT #######\n");
        char status = PROC_CLOSE;
        //    Close first worker process
        if (write(pfd[1], &status, sizeof(status)) < 0)
        {
            shutdown(sockServer, SHUT_RDWR);
            close(sockServer);
            exit(0);
        }
    }
    else if (sig == SIGTERM)
    {
        print_err("<main> ####### SIGTERM #######\n");
        shutdown(sockServer, SHUT_RDWR);
        close(sockServer);
        exit(0);
    }
    else if (sig == SIGSEGV)
    {
        print_err("<main> ####### SIGSEGV #######\n");
        shutdown(sockServer, SHUT_RDWR);
        close(sockServer);
        exit(1);
    }
    else if (sig == SIGUSR1)
    {
        fprintf(stderr, "<%s> ####### SIGUSR1 #######\n", __func__);
        char status = PROC_CLOSE;
        //    Close first worker process
        if (write(pfd[1], &status, sizeof(status)) < 0)
        {
            shutdown(sockServer, SHUT_RDWR);
            close(sockServer);
            exit(0);
        }
        restart = 1;
    }
    else if (sig == SIGUSR2)
    {
        fprintf(stderr, "<%s> ####### SIGUSR2 #######\n", __func__);
        char status = PROC_CLOSE;
        //    Close first worker process
        if (write(pfd[1], &status, sizeof(status)) < 0)
        {
            shutdown(sockServer, SHUT_RDWR);
            close(sockServer);
            exit(0);
        }
    }
    else
        fprintf(stderr, "<%s:%d> ? sig=%d\n", __func__, __LINE__, sig);
}
//======================================================================
void print_help(const char *name)
{
    fprintf(stderr, "Usage: %s [-l] [-c configfile] [-s signal]\n"
                    "Options:\n"
                    "   -h              : help\n"
                    "   -p              : print parameters\n"
                    "   -c configfile   : default: \"./server.conf\"\n"
                    "   -s signal       : restart, close, abort\n", name);
}
//======================================================================
void print_limits()
{
    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) == -1)
        cout << " Error getrlimit(RLIMIT_NOFILE): " << strerror(errno) << "\n";
    else
        cout << " RLIMIT_NOFILE: cur=" << (long)lim.rlim_cur << ", max=" << (long)lim.rlim_max << "\n";
    cout << " hardware_concurrency(): " << thread::hardware_concurrency() << "\n\n";
}
//======================================================================
void print_config()
{
    print_limits();
    
    cout << "   ServerSoftware         : " << conf->ServerSoftware.c_str()
         << "\n\n   ServerAddr             : " << conf->ServerAddr.c_str()
         << "\n   ServerPort             : " << conf->ServerPort.c_str()
         << "\n\n   ListenBacklog          : " << conf->ListenBacklog
         << "\n   tcp_cork               : " << conf->tcp_cork
         << "\n   tcp_nodelay            : " << conf->tcp_nodelay

         << "\n\n   SndBufSize             : " << conf->SndBufSize
         << "\n   SendFile               : " << conf->SendFile

         << "\n\n   NumCpuCores            : " << conf->NumCpuCores
         << "\n   MaxWorkConnections     : " << conf->MaxWorkConnections
         << "\n   MaxEventConnections    : " << conf->MaxEventConnections

         << "\n\n   NumProc                : " << conf->NumProc
         << "\n   MaxThreads             : " << conf->MaxThreads
         << "\n   MimThreads             : " << conf->MinThreads
         << "\n   MaxCgiProc             : " << conf->MaxCgiProc

         << "\n\n   MaxRequestsPerClient   : " << conf->MaxRequestsPerClient
         << "\n   TimeoutKeepAlive       : " << conf->TimeoutKeepAlive
         << "\n   Timeout                : " << conf->Timeout
         << "\n   TimeoutCGI             : " << conf->TimeoutCGI
         << "\n   TimeoutPoll            : " << conf->TimeoutPoll

         << "\n\n   MaxRanges              : " << conf->MaxRanges

         << "\n\n   ClientMaxBodySize      : " << conf->ClientMaxBodySize

         << "\n\n   ShowMediaFiles         : " << conf->ShowMediaFiles

         << "\n\n   index_html             : " << conf->index_html
         << "\n   index_php              : " << conf->index_php
         << "\n   index_pl               : " << conf->index_pl
         << "\n   index_fcgi             : " << conf->index_fcgi
         << "\n\n   DocumentRoot           : " << conf->DocumentRoot.c_str()
         << "\n   ScriptPath             : " << conf->ScriptPath.c_str()
         << "\n   LogPath                : " << conf->LogPath.c_str()
         << "\n\n   UsePHP                 : " << conf->UsePHP.c_str()
         << "\n   PathPHP                : " << conf->PathPHP.c_str()
         << "\n\n   User                   : " << conf->user.c_str()
         << "\n   Group                  : " << conf->group.c_str()
         << "\n";
         
    cout << "   ------------- FastCGI -------------\n";
    fcgi_list_addr *i = conf->fcgi_list;
    for (; i; i = i->next)
    {
        cout << "   [" << i->script_name.c_str() << " : " << i->addr.c_str() << "]\n";
    }
}
//======================================================================
int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    if (argc == 1)
        conf_path = "server.conf";
    else
    {
        int c, arg_print = 0;
        pid_t pid_ = 0;
        char *sig = NULL, *conf_dir_ = NULL;
        while ((c = getopt(argc, argv, "c:s:h:p")) != -1)
        {
            switch (c)
            {
                case 'c':
                    conf_dir_ = optarg;
                    break;
                case 's':
                    sig = optarg;
                    break;
                case 'h':
                    print_help(argv[0]);
                    return 0;
                case 'p':
                    arg_print = 1;
                    break;
                default:
                    print_help(argv[0]);
                    return 0;
            }
        }

        if (conf_dir_)
            conf_path = conf_dir_;
        else
            conf_path = "server.conf";

        if (arg_print)
        {
            if (read_conf_file(conf_path.c_str()))
                return 1;
            print_config();
            return 0;
        }

        if (sig)
        {
            int sig_send;
            if (!strcmp(sig, "restart"))
                sig_send = SIGUSR1;
            else if (!strcmp(sig, "close"))
                sig_send = SIGUSR2;
            else if (!strcmp(sig, "abort"))
                sig_send = SIGTERM;
            else
            {
                fprintf(stderr, "<%d> ? option -s: %s\n", __LINE__, sig);
                print_help(argv[0]);
                return 1;
            }
            
            if (read_conf_file(conf_path.c_str()))
                return 1;
            pidFile = conf->PidFilePath + "/pid.txt";
            FILE *fpid = fopen(pidFile.c_str(), "r");
            if (!fpid)
            {
                fprintf(stderr, "<%s:%d> Error open PidFile(%s): %s\n", __func__, __LINE__, pidFile.c_str(), strerror(errno));
                return 1;
            }

            fscanf(fpid, "%u", &pid_);
            fclose(fpid);

            if (kill(pid_, sig_send))
            {
                fprintf(stderr, "<%d> Error kill(pid=%u, sig=%u): %s\n", __LINE__, pid_, sig_send, strerror(errno));
                return 1;
            }

            return 0;
        }
    }

    while (restart)
    {
        restart = 0;

        if (read_conf_file(conf_path.c_str()))
            break;

        set_uid();

        sockServer = create_server_socket(conf);
        if (sockServer == -1)
        {
            fprintf(stderr, "<%s:%d> Error: create_server_socket(%s:%s)\n", __func__, __LINE__, 
                        conf->ServerAddr.c_str(), conf->ServerPort.c_str());
            break;
        }

        Connect::serverSocket = sockServer;

        if (start == 0)
        {
            start = 1;
            pidFile = conf->PidFilePath + "/pid.txt";
            FILE *fpid = fopen(pidFile.c_str(), "w");
            if (!fpid)
            {
                fprintf(stderr, "<%s:%d> Error open PidFile(%s): %s\n", __func__, __LINE__, pidFile.c_str(), strerror(errno));
                return 1;
            }

            fprintf(fpid, "%u\n", getpid());
            fclose(fpid);
            //----------------------------------------------------------
            if (signal(SIGINT, signal_handler) == SIG_ERR)
            {
                fprintf(stderr, "<%s:%d> Error signal(SIGINT): %s\n", __func__, __LINE__, strerror(errno));
                break;
            }

            if (signal(SIGTERM, signal_handler) == SIG_ERR)
            {
                fprintf(stderr, "<%s:%d> Error signal(SIGTERM): %s\n", __func__, __LINE__, strerror(errno));
                break;
            }

            if (signal(SIGSEGV, signal_handler) == SIG_ERR)
            {
                fprintf(stderr, "<%s:%d> Error signal(SIGSEGV): %s\n", __func__, __LINE__, strerror(errno));
                break;
            }

            if (signal(SIGUSR1, signal_handler) == SIG_ERR)
            {
                fprintf(stderr, "<%s:%d> Error signal(SIGUSR1): %s\n", __func__, __LINE__, strerror(errno));
                break;
            }

            if (signal(SIGUSR2, signal_handler) == SIG_ERR)
            {
                fprintf(stderr, "<%s:%d> Error signal(SIGUSR2): %s\n", __func__, __LINE__, strerror(errno));
                break;
            }
        }
        //--------------------------------------------------------------
        create_logfiles(conf->LogPath);
        //--------------------------------------------------------------
        int ret = main_proc();
        close_logs();
        if (ret)
            break;
    }

    if (start == 1)
        remove(pidFile.c_str());
    return 0;
}
//======================================================================
int main_proc()
{
    pid_t pid = getpid();
    //------------------------------------------------------------------
    cout << "\n[" << get_time().c_str() << "] - server \"" << conf->ServerSoftware.c_str()
         << "\" run port: " << conf->ServerPort.c_str() << "\n";
    cerr << "   pid="  << pid << "; uid=" << getuid() << "; gid=" << getgid() << "\n";
    cout << "   pid="  << pid << "; uid=" << getuid() << "; gid=" << getgid() << "\n";
    cerr << "   MaxWorkConnections: " << conf->MaxWorkConnections << ", NumCpuCores: " << conf->NumCpuCores << "\n";
    cerr << "   SndBufSize: " << conf->SndBufSize << ", MaxEventConnections: " << conf->MaxEventConnections << "\n";
    //------------------------------------------------------------------
    for ( ; environ[0]; )
    {
        char *p, buf[512];
        if ((p = (char*)memccpy(buf, environ[0], '=', strlen(environ[0]))))
        {
            *(p - 1) = 0;
            unsetenv(buf);
        }
    }
    //------------------------------------------------------------------
    if (pipe(pfd) < 0)
    {
        fprintf(stderr, "<%s:%d> Error pipe(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    pfd_in = pfd[0];
    //   Creating first process
    pid_child = create_child(sockServer, 0, &pfd_in, pfd[1], CONNECT_ALLOW);
    if (pid_child < 0)
    {
        fprintf(stderr, "<%s:%d> Error create_child()\n", __func__, __LINE__);
        exit(1);
    }

    //   Creating ather processes
    pid_t pid_;
    unsigned int num_create_proc = 1;
    for ( ; num_create_proc < conf->NumProc; ++num_create_proc)
    {
        pid_ = create_child(sockServer, num_create_proc, &pfd_in, pfd[1], CONNECT_IGN);
        if (pid_ < 0)
        {
            fprintf(stderr, "<%s:%d> Error create_child()\n", __func__, __LINE__);
            exit(1);
        }
    }
    //------------------------------------------------------------------
    //---------- Allow connections first worker process ----------------
    unsigned char status = CONNECT_ALLOW;
    int ret;
    if ((ret = write(pfd[1], &status, sizeof(status))) < 0)
    {
        print_err("<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
        close(pfd[1]);
        shutdown(sockServer, SHUT_RDWR);
        close(sockServer);
        return 1;
    }
    //------------------------------------------------------------------
    while (1)
    {
        if ((ret = read(pfd_in, &status, sizeof(status))) <= 0)
        {
            print_err("<%s:%d> Error read(): %s\n", __func__, __LINE__, strerror(errno));
            break;
        }
        //else
            //print_err("<%s:%d> status: 0x%x, from proc: %u\n", __func__, __LINE__, (int)status, num_create_proc - 1);

        if (status == PROC_CLOSE)
            break;
        else if (status == (CONNECT_ALLOW | 0x80))
        {
            if (num_create_proc < conf->MaxNumProc)
            {
                pid_ = create_child(sockServer, num_create_proc, &pfd_in, pfd[1], CONNECT_ALLOW);
                if (pid_ < 0)
                {
                    fprintf(stderr, "<%s:%d> Error create_child()\n", __func__, __LINE__);
                    exit(1);
                }

                ++num_create_proc;
            }
            else
            {
                status = CONNECT_ALLOW;
                if (write(pfd[1], &status, sizeof(status)) < 0)
                {
                    print_err("<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
                    break;
                }
            }
        }
        else if (status == CONNECT_ALLOW)
        {
            status = 0x80 | CONNECT_ALLOW;
            if (write(pfd[1], &status, sizeof(status)) < 0)
            {
                print_err("<%s:%d> Error write(): %s\n", __func__, __LINE__, strerror(errno));
                break;
            }
        }
        else
            print_err("<%s:%d> !!! status: 0x%x, from proc: %u\n", __func__, __LINE__, (int)status, num_create_proc - 1);
    }

    close(sockServer);
    free_fcgi_list();

    while ((pid = wait(NULL)) != -1)
    {
        print_err("<%s> wait() pid: %d\n", __func__, pid);
    }

    shutdown(sockServer, SHUT_RDWR);
    close(pfd[1]);

    if (restart == 0)
        fprintf(stderr, "<%s> ***** Close *****\n", __func__);
    else
        fprintf(stderr, "<%s> ***** Reload *****\n", __func__);

    return 0;
}
//======================================================================
void manager(int, unsigned int, int, int, char);
//======================================================================
pid_t create_child(int sock, unsigned int num_chld, int *pfd_i, int fd_close, char sig)
{
    pid_t pid;
    int pfd[2];

    if (pipe(pfd) < 0)
    {
        fprintf(stderr, "<%s:%d> Error pipe(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    errno = 0;
    pid = fork();

    if (pid == 0)
    {
        uid_t uid = getuid();
        if (uid == 0)
        {
            if (setgid(conf->server_gid) == -1)
            {
                fprintf(stderr, "<%s:%d> Error setgid(%u): %s\n", __func__, __LINE__, conf->server_gid, strerror(errno));
                exit(1);
            }

            if (setuid(conf->server_gid) == -1)
            {
                fprintf(stderr, "<%s:%d> Error setuid(%u): %s\n", __func__, __LINE__, conf->server_gid, strerror(errno));
                exit(1);
            }
        }

        close(pfd[0]);
        close(fd_close);

        manager(sock, num_chld, *pfd_i, pfd[1], sig);

        close(*pfd_i);
        close_logs();
        exit(0);
    }
    else if (pid < 0)
    {
        fprintf(stderr, "<%s:%d> Error fork(): %s\n", __func__, __LINE__, strerror(errno));
        close(pfd[0]);
        pfd[0] = -1;
    }

    close(*pfd_i);
    close(pfd[1]);
    *pfd_i = pfd[0];

    return pid;
}
