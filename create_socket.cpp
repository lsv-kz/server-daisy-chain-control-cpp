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

    if (conf->tcp_nodelay == 'y')
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
        const int sock_opt = 1;
        struct sockaddr_in sock_addr;
        memset(&sock_addr, 0, sizeof(sock_addr));

        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd == -1)
        {
            return -errno;
        }

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
            print_err("   Error inet_pton(%s): %s\n", addr, strerror(errno));
            close(sockfd);
            return -errno;
        }

        if (connect(sockfd, (struct sockaddr *)(&sock_addr), sizeof(sock_addr)) != 0)
        {
            close(sockfd);
            return -errno;
        }
    }
    else //==== PF_UNIX ====
    {
        struct sockaddr_un sock_addr;
        sockfd = socket (PF_UNIX, SOCK_STREAM, 0);
        if (sockfd == -1)
            return -errno;

        sock_addr.sun_family = AF_UNIX;
        strcpy (sock_addr.sun_path, host);

        if (connect (sockfd, (struct sockaddr *) &sock_addr, SUN_LEN(&sock_addr)) == -1)
        {
            close(sockfd);
            return -errno;
        }
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

    return sockfd;
}
//======================================================================
int create_server_socket_(const Config *conf)
{
    int sockfd;
    const int sock_opt = 1;
    struct sockaddr_in server_sockaddr;
    
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sockfd == -1)
    {
        fprintf(stderr, "   Error socket(): %s\n", strerror(errno));
        return -1;
    }
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&sock_opt, sizeof(sock_opt)))
    {
        perror("setsockopt (SO_REUSEADDR)");
        close(sockfd);
        return -1;
    }

    if (conf->tcp_nodelay == 'y')
    {
        if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&sock_opt, sizeof(sock_opt)))
        {
            print_err("<%s:%d> setsockopt: unable to set TCP_NODELAY: %s\n", __func__, __LINE__, strerror(errno));
            close(sockfd);
            return -1;
        }
    }
    //------------------------------------------------------------------
    /*socklen_t optlen = sizeof(sndbuf);
    int sndbuf;
    if (!getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (void *)&sndbuf, &optlen))
    {
        printf("<%s:%d> SndBufSize=%d\n", __func__, __LINE__, conf->SndBufSize);
        printf("<%s:%d> SO_SNDBUF=%d\n", __func__, __LINE__, sndbuf);
    }*/
    //------------------------------------------------------------------
    memset(&server_sockaddr, 0, sizeof server_sockaddr);
    server_sockaddr.sin_family = PF_INET;
    server_sockaddr.sin_port = htons(atoi(conf->ServerPort.c_str()));

/*  if (inet_pton(PF_INET, addr, &(server_sockaddr.sin_addr)) < 1)
    {
        print_err("   Error inet_pton(%s): %s\n", addr, strerror(errno));
        close(sockfd);
        return -1;
    }*/
    server_sockaddr.sin_addr.s_addr = inet_addr(conf->ServerAddr.c_str());
    
    if (bind(sockfd, (struct sockaddr *) &server_sockaddr, sizeof (server_sockaddr)) == -1)
    {
        int err = errno;
        printf("<%s:%d> Error bind(): %s\n", __func__, __LINE__, strerror(err));
        print_err("<%s:%d> Error bind(): %s\n", __func__, __LINE__, strerror(err));
        close(sockfd);
        return -1;
    }
    
    if (listen(sockfd, conf->ListenBacklog) == -1)
    {
        perror("listen");
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}
