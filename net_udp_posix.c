#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "u_mem.h"
#include "net_sock.h"

int SOCK_UdpInit(S_Udp *udp, int af)
{
    U_bzero(udp, sizeof(*udp));
    udp->state = S_UDP_STATE_INIT;
    udp->addr.af = af;

    if      (af == S_AF_IPV4) af = AF_INET;
    else if (af == S_AF_IPV6) af = AF_INET6;
    else
    {
        udp->addr.af = S_AF_UNKNOWN;
        return 0;
    }

    udp->handle = socket(af, SOCK_DGRAM, 0);

    if (udp->handle == -1)
        return 0;

    udp->state = S_UDP_STATE_OPEN;

    return 1;
}

int SOCK_UdpSetPeer(S_Udp *udp, const char *peer, unsigned short port)
{
    struct in_addr addr;
    struct in6_addr addr6;

    if (inet_pton(AF_INET, peer, &addr) == 1)
    {
        udp->peer_addr.af = S_AF_IPV4;
        udp->peer_port = port;
        U_memcpy(udp->peer_addr.data, &addr.s_addr, 4);
        return 0;
    }
    else if (inet_pton(AF_INET6, peer, &addr6) == 1)
    {
        udp->peer_addr.af = S_AF_IPV6;
        udp->peer_port = port;
        U_memcpy(udp->peer_addr.data, &addr6, 16);
        return 0;
    }

    return -1;
}

int SOCK_UdpBind(S_Udp *udp, unsigned short port)
{
    int ret;
    int yes = 1;
    struct sockaddr_in addr;
    struct sockaddr_in6 addr6;

    if (udp->state != S_UDP_STATE_OPEN)
        return 0;

    errno = 0;

    /* allow multiple sockets to use the same PORT number */
    if (setsockopt(udp->handle, SOL_SOCKET, SO_REUSEADDR, (char*) &yes, sizeof(yes) ) < 0 )
       goto err;

    if (udp->addr.af == S_AF_IPV4)
    {
        U_bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        ret = bind(udp->handle, (struct sockaddr*) &addr, sizeof(addr));

        if (ret == -1)
            goto err;

        udp->port = port;
        return 1;
    }
    else if (udp->addr.af == S_AF_IPV6)
    {
        U_bzero(&addr6, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(port);
        ret = bind(udp->handle, (struct sockaddr*) &addr6, sizeof(addr6));

        if (ret == -1)
            goto err;

        udp->port = port;
        return 1;
    }

err:
    udp->state = S_UDP_STATE_ERROR;
    if (errno)
    {
        fprintf(stderr, "udp bind port: %u failed: %s\n", (unsigned)port, strerror(errno));
    }
    return 0;
}

int SOCK_UdpJoinMulticast(S_Udp *udp, const char *maddr)
{
    struct ip_mreq mreq;
    struct ipv6_mreq mreq6;

    if (udp->state != S_UDP_STATE_OPEN)
        return -1;

    if (udp->addr.af == S_AF_IPV4)
    {
        mreq.imr_multiaddr.s_addr = inet_addr(maddr);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);

        if (setsockopt(udp->handle, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
            goto err;

        return 0;
    }
    else if (udp->addr.af == S_AF_IPV6)
    {
        mreq6.ipv6mr_interface = 0;
        inet_pton(AF_INET6, maddr, &mreq6.ipv6mr_multiaddr);
        if (setsockopt(udp->handle, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6, sizeof(mreq6)) < 0)
            goto err;

        return 0;
    }

err:
    udp->state = S_UDP_STATE_ERROR;
    return -1;
}

int SOCK_UdpSend(S_Udp *udp, unsigned char *buf, unsigned bufsize)
{
    ssize_t n;
    struct sockaddr_in dest_addr;
    struct sockaddr_in6 dest_addr6;

    if (udp->peer_addr.af == S_AF_IPV4)
    {
        dest_addr.sin_family = AF_INET;
        U_memcpy ( (char *) &dest_addr.sin_addr.s_addr, udp->peer_addr.data, 4);
        dest_addr.sin_port = htons (udp->peer_port);

        n = sendto(udp->handle, buf, (size_t)bufsize, 0 /* flags */, &dest_addr, sizeof(dest_addr));
    }
    else if (udp->peer_addr.af == S_AF_IPV6)
    {
        dest_addr6.sin6_family = AF_INET6;
        U_memcpy ( (char *) &dest_addr6.sin6_addr, udp->peer_addr.data, 16);
        dest_addr6.sin6_port = htons (udp->peer_port);

        n = sendto(udp->handle, buf, (size_t)bufsize, 0 /* flags */, &dest_addr6, sizeof(dest_addr6));
    }
    else
    {
        return -1;
    }

    if (n < 0)
    {

        return -1;
    }

    return (int)n;
}

int SOCK_UdpRecv(S_Udp *udp, unsigned char *buf, unsigned bufsize)
{
    ssize_t n;
    fd_set readfds;
    struct timeval tv;
    socklen_t addr_len;
    struct sockaddr_in *sa4;
    struct sockaddr_in6 *sa6;
    struct sockaddr_storage addr;
    char abuf[INET6_ADDRSTRLEN];

    if (udp->state != S_UDP_STATE_OPEN)
        return -1;

    /* TODO non blocking */
    tv.tv_sec = 0;
    tv.tv_usec = 5 * 1000; // 5 msec
    FD_ZERO(&readfds);
    FD_SET(udp->handle, &readfds);
    // don't care about writefds and exceptfds:
    select(udp->handle+1, &readfds, NULL, NULL, &tv);
    if (FD_ISSET(udp->handle, &readfds))
    {
        addr_len = sizeof(addr);
        n = recvfrom(udp->handle, buf, bufsize, 0, (struct sockaddr*)&addr, &addr_len);

        if (addr.ss_family == AF_INET6)
        {
            sa6 = (struct sockaddr_in6*)&addr;
            udp->peer_addr.af = S_AF_IPV6;
            udp->peer_port = sa6->sin6_port;
            U_memcpy(&udp->peer_addr.data[0], &sa6->sin6_addr.s6_addr[0], 16);
        }
        else if (addr.ss_family == AF_INET)
        {
            sa4 = (struct sockaddr_in*)&addr;
            udp->peer_addr.af = S_AF_IPV4;
            udp->peer_port = sa4->sin_port;
            U_memcpy(&udp->peer_addr.data[0], &sa4->sin_addr.s_addr, 4);
        }
        else
        {
            return -1;
        }

        if (inet_ntop(addr.ss_family, (void*)&udp->peer_addr.data[0], &abuf[0], sizeof(abuf)))
        {
            fprintf(stderr, "UDP peer %s port: %u\n", &abuf[0], ntohs(udp->peer_port));
        }

        if (n > 0 && n < (ssize_t)bufsize)
        {
            return (int)n;
        }
        else if (n < 0)
        {
            fprintf(stderr, "UDP error %s\n", strerror(errno));
            return -1;
        }
    }

    return 0;
}

void SOCK_UdpFree(S_Udp *udp)
{
    if (udp->handle)
        close(udp->handle);

    U_bzero(udp, sizeof(*udp));
    udp->state = S_UDP_STATE_INIT;
}
