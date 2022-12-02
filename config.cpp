#include "main.h"

using namespace std;

static Config c;
const Config* const conf = &c;
//======================================================================
int check_path(string & path)
{
    struct stat st;

    int ret = stat(path.c_str(), &st);
    if (ret == -1)
    {
        fprintf(stderr, "<%s:%d> Error stat(%s): %s\n", __func__, __LINE__, path.c_str(), strerror(errno));
        char buf[2048];
        char *cwd = getcwd(buf, sizeof(buf));
        if (cwd)
            fprintf(stderr, "<%s:%d> cwd: %s\n", __func__, __LINE__, cwd);
        return -1;
    }

    if (!S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "<%s:%d> [%s] is not directory\n", __func__, __LINE__, path.c_str());
        return -1;
    }

    char path_[PATH_MAX] = "";
    if (!realpath(path.c_str(), path_))
    {
        fprintf(stderr, "<%s:%d> Error realpath(): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }

    path = path_;

    return 0;
}
//======================================================================
void create_conf_file(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
    {
        fprintf(stderr, "<%s> Error create conf file (%s): %s\n", __func__, path, strerror(errno));
        exit(1);
    }

    fprintf(f, "ServerSoftware  ?\n");
    fprintf(f, "ServerAddr      0.0.0.0\n");
    fprintf(f, "ServerPort      8080\n\n");

    fprintf(f, "ListenBacklog 128\n");
    fprintf(f, "tcp_cork      n # y/n \n");
    fprintf(f, "tcp_nodelay   y \n\n");

    fprintf(f, "DocumentRoot www/html\n");
    fprintf(f, "ScriptPath   www/cgi-bin\n");
    fprintf(f, "LogPath      www/logs\n\n");
    fprintf(f, "PidFilePath  www/pid\n\n");

    fprintf(f, "SendFile    y\n");
    fprintf(f, "SndBufSize  32768\n\n");

    fprintf(f, "MaxWorkConnections   768\n");
    fprintf(f, "MaxEventConnections  100\n\n");

    fprintf(f, "NumProc 1\n");
    fprintf(f, "MaxNumProc 4\n");
    fprintf(f, "MaxThreads 250\n");
    fprintf(f, "MinThreads 6\n");
    fprintf(f, "MaxCgiProc 15\n\n");

    fprintf(f, "MaxRequestsPerClient 10000\n");
    fprintf(f, "TimeoutKeepAlive 15\n");
    fprintf(f, "Timeout      120\n");
    fprintf(f, "TimeoutCGI    15\n\n");
    fprintf(f, "TimeoutPoll  100\n\n");

    fprintf(f, "MaxRanges  5\n\n");

    fprintf(f, "ClientMaxBodySize 10000000\n\n");

    fprintf(f, " UsePHP     n  # [n, php-fpm, php-cgi]\n");
    fprintf(f, "# PathPHP  127.0.0.1:9000  #  [php-fpm: 127.0.0.1:9000 (/var/run/php-fpm.sock), php-cgi: /usr/bin/php-cgi]\n\n");

    fprintf(f, "AutoIndex   n\n");
    fprintf(f, "index {\n"
                "\t#index.html\n"
                "}\n\n");

    fprintf(f, "fastcgi {\n"
                "\t#/test  127.0.0.1:9009\n"
                "}\n\n");

    fprintf(f, "scgi {\n"
                "\t#/scgi_test  127.0.0.1:9009\n"
                "}\n\n");

    fprintf(f, "ShowMediaFiles  y #  y/n \n\n");

    fprintf(f, "User   root\n");
    fprintf(f, "Group  www-data\n");

    fclose(f);
}
//======================================================================
static int line_ = 1, line_inc = 0;
static char bracket = 0;
//----------------------------------------------------------------------
int getLine(FILE *f, String &ss)
{
    ss.clear();
    if (bracket)
    {
        ss << bracket;
        bracket = 0;
        return 1;
    }

    ss = "";
    int ch, len = 0, numWords = 0, wr = 1, wrSpace = 0;
    
    if (line_inc)
    {
        ++line_;
        line_inc = 0;
    }

    while (((ch = getc(f)) != EOF))
    {
        if (ch == '\n')
        {
            if (len)
            {
                line_inc = 1;
                return ++numWords;
            }
            else
            {
                ++line_;
                wr = 1;
                ss = "";
                wrSpace = 0;
                continue;
            }
        }
        else if (wr == 0)
            continue;
        else if ((ch == ' ') || (ch == '\t'))
        {
            if (len)
                wrSpace = 1;
        }
        else if (ch == '#')
            wr = 0;
        else if ((ch == '{') || (ch == '}'))
        {
            if (len)
                bracket = (char)ch;
            else
            {
                ss << (char)ch;
                ++len;
            }
            return ++numWords;
        }
        else if (ch != '\r')
        {
            if (wrSpace)
            {
                ss << " ";
                ++len;
                ++numWords;
                wrSpace = 0;
            }

            ss << (char)ch;
            ++len;
        }
    }

    if (len)
        return ++numWords;
    return -1;
}
//======================================================================
int is_number(const char *s)
{
    if (!s)
        return 0;
    int n = isdigit((int)*(s++));
    while (*s && n)
        n = isdigit((int)*(s++));
    return n;
}
//======================================================================
int is_bool(const char *s)
{
    if (!s || (strlen(s) != 1))
        return 0;
    return ((tolower(s[0]) == 'y') || (tolower(s[0]) == 'n'));
}
//======================================================================
int find_bracket(FILE *f, char c)
{
    if (bracket)
    {
        if (c != bracket)
        {
            bracket = 0;
            return 0;
        }

        bracket = 0;
        return 1;
    }

    int ch, grid = 0;
    if (line_inc)
    {
        ++line_;
        line_inc = 0;
    }

    while (((ch = getc(f)) != EOF))
    {
        if (ch == '#')
        {
            grid = 1;
        }
        else if (ch == '\n')
        {
            grid = 0;
            ++line_;
        }
        else if ((ch == '{') && (grid == 0))
            return 1;
        else if ((ch != ' ') && (ch != '\t') && (grid == 0))
            return 0;
    }

    return 0;
}
//======================================================================
void create_fcgi_list(fcgi_list_addr **l, const string &s1, const string &s2, int type)
{
    if (l == NULL)
        fprintf(stderr, "<%s:%d> Error pointer = NULL\n", __func__, __LINE__), exit(errno);

    fcgi_list_addr *t;
    try
    {
        t = new fcgi_list_addr;
    }
    catch (...)
    {
        fprintf(stderr, "<%s:%d> Error malloc(): %s\n", __func__, __LINE__, strerror(errno));
        exit(errno);
    }

    t->script_name = s1;
    t->addr = s2;
    t->type = type;
    t->next = *l;
    *l = t;
}
//======================================================================
int read_conf_file(FILE *fconf)
{
    String ss;

    c.index_html = c.index_php = c.index_pl = c.index_fcgi = 'n';
    c.fcgi_list = NULL;

    int n;
    while ((n = getLine(fconf, ss)) > 0)
    {
        if (n == 2)
        {
            String s1, s2;
            ss >> s1;
            ss >> s2;

            if (s1 ==  "ServerAddr")
                s2 >> c.ServerAddr;
            else if (s1 == "ServerPort")
                s2 >> c.ServerPort;
            else if (s1 == "ServerSoftware")
                s2 >> c.ServerSoftware;
            else if ((s1 == "tcp_cork") && is_bool(s2.c_str()))
                c.tcp_cork = (char)tolower(s2[0]);
            else if ((s1 == "tcp_nodelay") && is_bool(s2.c_str()))
                c.tcp_nodelay = (char)tolower(s2[0]);
            else if ((s1 == "ListenBacklog") && is_number(s2.c_str()))
                s2 >> c.ListenBacklog;
            else if ((s1 == "SendFile") && is_bool(s2.c_str()))
                c.SendFile = (char)tolower(s2[0]);
            else if ((s1 == "SndBufSize") && is_number(s2.c_str()))
                s2 >> c.SndBufSize;
            else if ((s1 == "NumCpuCores") && is_number(s2.c_str()))
                s2 >> c.NumCpuCores;
            else if ((s1 == "MaxWorkConnections") && is_number(s2.c_str()))
                s2 >> c.MaxWorkConnections;
            else if ((s1 == "MaxEventConnections") && is_number(s2.c_str()))
                s2 >> c.MaxEventConnections;
            else if ((s1 == "TimeoutPoll") && is_number(s2.c_str()))
                s2 >> c.TimeoutPoll;
            else if (s1 == "DocumentRoot")
                s2 >> c.DocumentRoot;
            else if (s1 == "ScriptPath")
                s2 >> c.ScriptPath;
            else if (s1 == "LogPath")
                s2 >> c.LogPath;
            else if (s1 == "PidFilePath")
                s2 >> c.PidFilePath;
            else if ((s1 == "NumProc") && is_number(s2.c_str()))
                s2 >> c.NumProc;
            else if ((s1 == "MaxNumProc") && is_number(s2.c_str()))
                s2 >> c.MaxNumProc;
            else if ((s1 == "MaxThreads") && is_number(s2.c_str()))
                s2 >> c.MaxThreads;
            else if ((s1 == "MinThreads") && is_number(s2.c_str()))
                s2 >> c.MinThreads;
            else if ((s1 == "MaxCgiProc") && is_number(s2.c_str()))
                s2 >> c.MaxCgiProc;
            else if ((s1 == "MaxRequestsPerClient") && is_number(s2.c_str()))
                s2 >> c.MaxRequestsPerClient;
            else if ((s1 == "TimeoutKeepAlive") && is_number(s2.c_str()))
                s2 >> c.TimeoutKeepAlive;
            else if ((s1 == "Timeout") && is_number(s2.c_str()))
                s2 >> c.Timeout;
            else if ((s1 == "TimeoutCGI") && is_number(s2.c_str()))
                s2 >> c.TimeoutCGI;
            else if ((s1 == "MaxRanges") && is_number(s2.c_str()))
                s2 >> c.MaxRanges;
            else if (s1 == "UsePHP")
                s2 >> c.UsePHP;
            else if (s1 == "PathPHP")
                s2 >> c.PathPHP;
            else if ((s1 == "ShowMediaFiles") && is_bool(s2.c_str()))
                c.ShowMediaFiles = (char)tolower(s2[0]);
            else if ((s1 == "ClientMaxBodySize") && is_number(s2.c_str()))
                s2 >> c.ClientMaxBodySize;
            else if (s1 == "User")
                s2 >> c.user;
            else if (s1 == "Group")
                s2 >> c.group;
            else if ((s1 == "AutoIndex") && is_bool(s2.c_str()))
                c.AutoIndex = (char)tolower(s2[0]);
            else
            {
                fprintf(stderr, "<%s:%d> Error read config file: [%s], line %d\n", __func__, __LINE__, ss.c_str(), line_);
                return -1;
            }
        }
        else if (n == 1)
        {
            if (ss == "index")
            {
                if (find_bracket(fconf, '{') == 0)
                {
                    fprintf(stderr, "<%s:%d> Error not found \"{\", line %d\n", __func__, __LINE__, line_);
                    return -1;
                }

                while (getLine(fconf, ss) == 1)
                {
                    if (ss == "}")
                        break;

                    if (ss == "index.html")
                        c.index_html = 'y';
                    else if (ss == "index.php")
                        c.index_php = 'y';
                    else if (ss == "index.pl")
                        c.index_pl = 'y';
                    else if (ss == "index.fcgi")
                        c.index_fcgi = 'y';
                    else
                    {
                        fprintf(stderr, "<%s:%d> Error read config file: \"index\" [%s], line %d\n", __func__, __LINE__, ss.c_str(), line_);
                        return -1;
                    }
                }

                if (ss.str() != "}")
                {
                    fprintf(stderr, "<%s:%d> Error not found \"}\", line %d\n", __func__, __LINE__, line_);
                    return -1;
                }
            }
            else if (ss == "fastcgi")
            {
                if (find_bracket(fconf, '{') == 0)
                {
                    fprintf(stderr, "<%s:%d> Error not found \"{\", line %d\n", __func__, __LINE__, line_);
                    return -1;
                }

                while (getLine(fconf, ss) == 2)
                {
                    string s1, s2;
                    ss >> s1;
                    ss >> s2;

                    create_fcgi_list(&c.fcgi_list, s1, s2, fast_cgi);
                }

                if (ss.str() != "}")
                {
                    fprintf(stderr, "<%s:%d> Error not found \"}\", line %d\n", __func__, __LINE__, line_);
                    return -1;
                }
            }
            else if (ss == "scgi")
            {
                if (find_bracket(fconf, '{') == 0)
                {
                    fprintf(stderr, "<%s:%d> Error not found \"{\", line %d\n", __func__, __LINE__, line_);
                    return -1;
                }

                while (getLine(fconf, ss) == 2)
                {
                    string s1, s2;
                    ss >> s1;
                    ss >> s2;

                    create_fcgi_list(&c.fcgi_list, s1, s2, s_cgi);
                }

                if (ss.str() != "}")
                {
                    fprintf(stderr, "<%s:%d> Error not found \"}\", line %d\n", __func__, __LINE__, line_);
                    return -1;
                }
            }
            else
            {
                fprintf(stderr, "<%s:%d> Error read config file: [%s] line %d\n", __func__, __LINE__, ss.c_str(), line_);
                return -1;
            }
        }
        else
        {
            fprintf(stderr, "<%s:%d> Error read config file: [%s], line %d\n", __func__, __LINE__, ss.c_str(), line_);
            return -1;
        }
    }

    if (!feof(fconf))
    {
        fprintf(stderr, "<%s:%d> Error read config file\n", __func__, __LINE__);
        return -1;
    }

    fclose(fconf);
    //------------------------------------------------------------------
    if (check_path(c.LogPath) == -1)
    {
        fprintf(stderr, "!!! Error LogPath [%s]\n", c.LogPath.c_str());
        return -1;
    }
    //------------------------------------------------------------------
    if (check_path(c.DocumentRoot) == -1)
    {
        fprintf(stderr, "!!! Error DocumentRoot [%s]\n", c.DocumentRoot.c_str());
        return -1;
    }
    //------------------------------------------------------------------
    if (check_path(c.ScriptPath) == -1)
    {
        c.ScriptPath = "";
        fprintf(stderr, "!!! Error ScriptPath [%s]\n", c.ScriptPath.c_str());
    }

    if (c.NumProc > c.MaxNumProc)
    {
        fprintf(stderr, "<%s:%d> Error: NumProc=%u; MaxNumProc=%u\n", __func__, __LINE__, c.NumProc, c.MaxNumProc);
        return -1;
    }

    if (c.MinThreads < 1)
        c.MinThreads = 1;

    if (c.MinThreads > c.MaxThreads)
    {
        fprintf(stderr, "<%s:%d> Error: NumThreads > MaxThreads\n", __func__, __LINE__);
        return -1;
    }
    //------------------------------------------------------------------
    //c.NumCpuCores = thread::hardware_concurrency();
    //------------------- Setting OPEN_MAX -----------------------------
    if (c.MaxWorkConnections <= 0)
    {
        fprintf(stderr, "<%s:%d> Error config file: MaxWorkConnections=%d\n", __func__, __LINE__, c.MaxWorkConnections);
        return -1;
    }

    const int fd_stdio = 3, fd_logs = 2, fd_serv_sock = 1, fd_pipe = 2; // 8
    long min_open_fd = fd_stdio + fd_logs + fd_serv_sock + fd_pipe;
    int max_fd = min_open_fd + c.MaxWorkConnections * 2;
    n = set_max_fd(max_fd);
    if (n == -1)
        return -1;
    else if (n < max_fd)
    {
        n = (n - min_open_fd)/2;
        c.MaxWorkConnections = n;
    }

    return 0;
}
//======================================================================
int read_conf_file(const char *path_conf)
{
    FILE *fconf = fopen(path_conf, "r");
    if (!fconf)
    {
        if (errno == ENOENT)
        {
            char s[8];
            printf("Create config file? [y/n]: ");
            fflush(stdout);
            fgets(s, sizeof(s), stdout);
            if (s[0] == 'y')
            {
                create_conf_file(path_conf);
                fprintf(stderr, " Correct config file: %s\n", path_conf);
            }
        }
        else
            fprintf(stderr, "<%s:%d> Error fopen(%s): %s\n", __func__, __LINE__, path_conf, strerror(errno));
        return -1;
    }
    
    return read_conf_file(fconf);
}
//======================================================================
int set_uid()
{
    uid_t uid = getuid();
    if (uid == 0)
    {
        char *p;
        c.server_uid = strtol(c.user.c_str(), &p, 0);
        if (*p == '\0')
        {
            struct passwd *passwdbuf = getpwuid(c.server_uid);
            if (!passwdbuf)
            {
                fprintf(stderr, "<%s:%d> Error getpwuid(%d): %s\n", __func__, __LINE__, c.server_uid, strerror(errno));
                return -1;
            }
        }
        else
        {
            struct passwd *passwdbuf = getpwnam(c.user.c_str());
            if (!passwdbuf)
            {
                fprintf(stderr, "<%s:%d> Error getpwnam(%s): %s\n", __func__, __LINE__, c.user.c_str(), strerror(errno));
                return -1;
            }
            c.server_uid = passwdbuf->pw_uid;
        }

        c.server_gid = strtol(c.group.c_str(), &p, 0);
        if (*p == '\0')
        {
            struct group *groupbuf = getgrgid(c.server_gid);
            if (!groupbuf)
            {
                fprintf(stderr, "<%s:%d> Error getgrgid(%d): %s\n", __func__, __LINE__, c.server_gid, strerror(errno));
                return -1;
            }
        }
        else
        {
            struct group *groupbuf = getgrnam(c.group.c_str());
            if (!groupbuf)
            {
                fprintf(stderr, "<%s:%d> Error getgrnam(%s): %s\n", __func__, __LINE__, c.group.c_str(), strerror(errno));
                return -1;
            }
            c.server_gid = groupbuf->gr_gid;
        }
        //--------------------------------------------------------------
        if (c.server_uid != uid)
        {
            if (setuid(c.server_uid) == -1)
            {
                fprintf(stderr, "<%s:%d> Error setuid(%u): %s\n", __func__, __LINE__, c.server_uid, strerror(errno));
                return -1;
            }
        }
    }
    else
    {
        c.server_uid = getuid();
        c.server_gid = getgid();
    }

    return 0;
}
//======================================================================
int set_max_fd(int max_open_fd)
{
    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) == -1)
    {
        fprintf(stderr, "<%s:%d> Error getrlimit(RLIMIT_NOFILE): %s\n", __func__, __LINE__, strerror(errno));
        return -1;
    }
    else
    {
        if (max_open_fd > (long)lim.rlim_cur)
        {
            if (max_open_fd > (long)lim.rlim_max)
                lim.rlim_cur = lim.rlim_max;
            else
                lim.rlim_cur = max_open_fd;

            if (setrlimit(RLIMIT_NOFILE, &lim) == -1)
                fprintf(stderr, "<%s:%d> Error setrlimit(RLIMIT_NOFILE): %s\n", __func__, __LINE__, strerror(errno));
            max_open_fd = sysconf(_SC_OPEN_MAX);
            if (max_open_fd < 0)
            {
                fprintf(stderr, "<%s:%d> Error sysconf(_SC_OPEN_MAX): %s\n", __func__, __LINE__, strerror(errno));
                return -1;
            }
        }
    }
    //fprintf(stderr, "<%s:%d> max_open_fd=%d\n", __func__, __LINE__, max_open_fd);
    return max_open_fd;
}
//======================================================================
void free_fcgi_list()
{
    c.free_fcgi_list();
}
