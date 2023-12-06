
int SOCK_UdpInit(S_Udp *udp, int af)
{
    U_bzero(udp, sizeof(*udp));
    udp->state = S_UDP_STATE_INIT;
    udp->addr.af = af;
#if 0
    if      (af == S_AF_IPV4) af = AF_INET;
    else if (af == S_AF_IPV6) af = AF_INET6;
    else return -1;

    udp->handle = socket(af, SOCK_DGRAM, 0);
    udp->handle = -1;
    if (udp->handle == -1)
    {
        return -1;
    }

    udp->state = S_UDP_STATE_OPEN;
#endif

    return 0;
}

int SOCK_UdpBind(S_Udp *udp, u16 port)
{
#if 0
    int ret;
    struct sockaddr_in addr;

    if (udp->state != S_UDP_STATE_OPEN)
        return -1;

    if (udp->addr.af == S_AF_IPV4)
    {
        // allow multiple sockets to use the same PORT number
        int yes = 1;
        if (setsockopt(udp->handle, SOL_SOCKET, SO_REUSEADDR, (char*) &yes, sizeof(yes) ) < 0 )
        {
           goto err;
        }

        U_bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        ret = bind(udp->handle, (struct sockaddr*) &addr, sizeof(addr));

        if (ret == -1)
        {
            goto err;
        }

        udp->port = port;
        return 0;
    }

err:
    udp->state = S_UDP_STATE_ERROR;
#endif
    return -1;
}

int SOCK_UdpJoinMulticast(S_Udp *udp, const char *maddr)
{
    if (udp->state != S_UDP_STATE_OPEN)
        return -1;
#if 0
    if (udp->addr.af == S_AF_IPV4)
    {
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr(maddr);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);

        if (setsockopt(udp->handle, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
        {
            goto err;
        }

        return 0;
    }

err:
    udp->state = S_UDP_STATE_ERROR;
#endif
    return -1;
}

void SOCK_UdpRecv(S_Udp *udp)
{
#if 0
    ssize_t n;
    fd_set readfds;
    struct timeval tv;
    socklen_t addr_len;
    struct sockaddr_in addr;

    if (udp->state != S_UDP_STATE_OPEN)
        return;

    tv.tv_sec = 0;
    tv.tv_usec = 5 * 1000; // 5 msec
    FD_ZERO(&readfds);
    FD_SET(udp->handle, &readfds);
    // don't care about writefds and exceptfds:
    select(udp->handle+1, &readfds, NULL, NULL, &tv);
    if (FD_ISSET(udp->handle, &readfds))
    {
        addr_len = sizeof(addr);
        n = recvfrom(udp->handle, &udp->rx_buf[0], sizeof(udp->rx_buf), 0, (struct sockaddr*)&addr, &addr_len);

        if (n > 0 && n < S_UDP_MAX_PKG_SIZE)
        {
            udp->rx_buf[n] = '\0';
            if (udp->rx)
                udp->rx(udp->user, udp, &udp->rx_buf[0], (unsigned)n);
        }
    }
#endif
}

void SOCK_UdpFree(S_Udp *udp)
{
//    if (udp->handle)
//        close(udp->handle);

    U_bzero(udp, sizeof(*udp));
    udp->state = S_UDP_STATE_INIT;
}
