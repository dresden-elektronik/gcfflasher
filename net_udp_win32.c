#include <winsock2.h>
#include <ws2tcpip.h>
#include "u_mem.h"
#include "net_sock.h"

static int wsa_init = 0;
static WSADATA wsa_data;

int SOCK_UdpInit(S_Udp *udp, int af)
{
    if (wsa_init == 0)
    {
        if (WSAStartup(MAKEWORD(2,2), &wsa_data) != 0)
            return -1;

        wsa_init = 1;
    }

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

    if (udp->handle == INVALID_SOCKET)
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
    (void)maddr;
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

int SOCK_UdpRecv(S_Udp *udp, unsigned char *buf, unsigned bufsize)
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
    return -1;
}

int SOCK_UdpSend(S_Udp *udp, unsigned char *buf, unsigned bufsize)
{
    int n;
    struct sockaddr_in dest_addr;
    struct sockaddr_in6 dest_addr6;

    if (udp->peer_addr.af == S_AF_IPV4)
    {
        dest_addr.sin_family = AF_INET;
        U_memcpy ( (char *) &dest_addr.sin_addr.s_addr, udp->peer_addr.data, 4);
        dest_addr.sin_port = htons(udp->peer_port);

        n = sendto(udp->handle, (char*)buf, (int)bufsize, 0 /* flags */, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    }
    else if (udp->peer_addr.af == S_AF_IPV6)
    {
        dest_addr6.sin6_family = AF_INET6;
        U_memcpy ( (char *) &dest_addr6.sin6_addr, udp->peer_addr.data, 16);
        dest_addr6.sin6_port = htons (udp->peer_port);

        n = sendto(udp->handle, (char*)buf, (int)bufsize, 0 /* flags */, (struct sockaddr*)&dest_addr6, sizeof(dest_addr6));
    }
    else
    {
        return -1;
    }

    if (n < 0)
    {
        return -1;
    }

    return n;
}

void SOCK_UdpFree(S_Udp *udp)
{
    if (udp->handle)
        closesocket(udp->handle);

    U_bzero(udp, sizeof(*udp));
    udp->state = S_UDP_STATE_INIT;
}
