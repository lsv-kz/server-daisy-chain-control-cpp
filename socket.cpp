#include "main.h"

//======================================================================
int create_server_socket(const Config *conf)
{
    int sockfd, n;
    const int sock_opt = 1;
    struct addrinfo  hints, *result, *rp;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((n = getaddrinfo(conf->ServerAddr.c_str(), conf->ServerPort.c_str(), &hints, &result)) != 0)
    {
        fprintf(stderr, "Error getaddrinfo(%s:%s): %s\n", conf->ServerAddr.c_str(), conf->ServerPort.c_str(), gai_strerror(n));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1)
            continue;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt));

        if (bind(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(sockfd);
    }

    freeaddrinfo(result);

    if (rp == NULL)
    {
        fprintf(stderr, "Error: failed to bind\n");
        return -1;
    }

    if (conf->TcpNoDelay == 'y')
    {
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&sock_opt, sizeof(sock_opt)); // SOL_TCP
    }

    int flags = fcntl(sockfd, F_GETFL);
    if (flags == -1)
    {
        fprintf(stderr, "Error fcntl(, F_GETFL, ): %s\n", strerror(errno));
    }
    else
    {
        flags |= O_NONBLOCK;
        if (fcntl(sockfd, F_SETFL, flags) == -1)
        {
            fprintf(stderr, "Error fcntl(, F_SETFL, ): %s\n", strerror(errno));
        }
    }

    if (listen(sockfd, conf->ListenBacklog) == -1)
    {
        fprintf(stderr, "Error listen(): %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}
//======================================================================
int create_fcgi_socket(const char *host)
{
    int sockfd, n;
    char addr[256];
    char port[16];

    if (!host)
        return -1;
    n = sscanf(host, "%[^:]:%s", addr, port);
    if (n == 2) //==== AF_INET ====
    {
        struct sockaddr_in sock_addr;
        memset(&sock_addr, 0, sizeof(sock_addr));

        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd == -1)
        {
            print_err("<%s:%d> Error socket(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }

        const int sock_opt = 1;
        if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&sock_opt, sizeof(sock_opt)))
        {
            print_err("<%s:%d> Error setsockopt(TCP_NODELAY): %s\n", __func__, __LINE__, strerror(errno));
            close(sockfd);
            return -1;
        }

        sock_addr.sin_port = htons(atoi(port));
        sock_addr.sin_family = AF_INET;
        if (inet_aton(addr, &(sock_addr.sin_addr)) == 0)
//      if (inet_pton(AF_INET, addr, &(sock_addr.sin_addr)) < 1)
        {
            print_err("<%s:%d> Error inet_pton(%s): %s\n", __func__, __LINE__, addr, strerror(errno));
            close(sockfd);
            return -1;
        }

        int flags = fcntl(sockfd, F_GETFL);
        if (flags == -1)
            print_err("<%s:%d> Error fcntl(, F_GETFL, ): %s\n", __func__, __LINE__, strerror(errno));
        else
        {
            flags |= O_NONBLOCK;
            if (fcntl(sockfd, F_SETFL, flags) == -1)
                print_err("<%s:%d> Error fcntl(, F_SETFL, ): %s\n", __func__, __LINE__, strerror(errno));
        }

        if (connect(sockfd, (struct sockaddr *)(&sock_addr), sizeof(sock_addr)) != 0)
        {
            if (errno != EINPROGRESS)
            {
                print_err("<%s:%d> Error connect(%s): %s\n", __func__, __LINE__, host, strerror(errno));
                close(sockfd);
                return -1;
            }
        }
    }
    else //==== PF_UNIX ====
    {
        struct sockaddr_un sock_addr;
        sockfd = socket (PF_UNIX, SOCK_STREAM, 0);
        if (sockfd == -1)
        {
            print_err("<%s:%d> Error socket(): %s\n", __func__, __LINE__, strerror(errno));
            return -1;
        }

        sock_addr.sun_family = AF_UNIX;
        strcpy (sock_addr.sun_path, host);

        int flags = fcntl(sockfd, F_GETFL);
        if (flags == -1)
        print_err("<%s:%d> Error fcntl(, F_GETFL, ): %s\n", __func__, __LINE__, strerror(errno));
        else
        {
            flags |= O_NONBLOCK;
            if (fcntl(sockfd, F_SETFL, flags) == -1)
                print_err("<%s:%d> Error fcntl(, F_SETFL, ): %s\n", __func__, __LINE__, strerror(errno));
        }

        if (connect (sockfd, (struct sockaddr *) &sock_addr, SUN_LEN(&sock_addr)) == -1)
        {
            if (errno != EINPROGRESS)
            {
                print_err("<%s:%d> Error connect(%s): %s\n", __func__, __LINE__, host, strerror(errno));
                close(sockfd);
                return -1;
            }
        }
    }

    return sockfd;
}
//======================================================================
int get_sock_fcgi(Connect *req, const char *script)
{
    int fcgi_sock = -1, len;
    fcgi_list_addr *ps = conf->fcgi_list;

    if (!script)
    {
        print_err(req, "<%s:%d> Not found\n", __func__, __LINE__);
        return -RS404;
    }

    len = strlen(script);
    if (len > 64)
    {
        print_err(req, "<%s:%d> Error len name script\n", __func__, __LINE__);
        return -RS400;
    }

    for (; ps; ps = ps->next)
    {
        if (!strcmp(script, ps->script_name.c_str()))
            break;
    }

    if (ps != NULL)
    {
        fcgi_sock = create_fcgi_socket(ps->addr.c_str());
        if (fcgi_sock < 0)
        {
            fcgi_sock = -RS502;
        }
    }
    else
    {
        print_err(req, "<%s:%d> Not found: %s\n", __func__, __LINE__, script);
        fcgi_sock = -RS404;
    }

    return fcgi_sock;
}
//======================================================================
int get_size_sock_buf(int domain, int optname, int type, int protocol)
{
    int sock = socket(domain, type, protocol);
    if (sock < 0)
    {
        fprintf(stderr, "<%s:%d> Error socketpair(): %s\n", __func__, __LINE__, strerror(errno));
        return -errno;
    }

    int sndbuf;
    socklen_t optlen = sizeof(sndbuf);
    if (getsockopt(sock, SOL_SOCKET, optname, &sndbuf, &optlen) < 0)
    {
        fprintf(stderr, "<%s:%d> Error getsockopt(SO_SNDBUF): %s\n", __func__, __LINE__, strerror(errno));
        close(sock);
        return -errno;
    }

    close(sock);
    return sndbuf;
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
int read_request_headers(Connect *req)
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
