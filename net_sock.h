#ifndef NET_SOCK_H
#define NET_SOCK_H

#define S_AF_UNKNOWN 0
#define S_AF_IPV4  4
#define S_AF_IPV6  6
#define S_UDP_MAX_PKG_SIZE 1280

typedef int S_Handle;

typedef enum S_UdpState
{
    S_UDP_STATE_INIT =  0,
    S_UDP_STATE_OPEN =  1,
    S_UDP_STATE_ERROR = 2
} S_UdpState;

typedef struct S_Addr
{
    unsigned char data[16];
    unsigned  char af;
} S_Addr;

typedef struct S_Udp
{
    S_Addr addr;
    S_Addr peer_addr;
    unsigned short peer_port;
    S_Handle handle;
    S_UdpState state;
    unsigned short port;
} S_Udp;

int SOCK_Init();
void SOCK_Free();

int SOCK_UdpInit(S_Udp *udp, int af);
int SOCK_UdpBind(S_Udp *udp, unsigned short port);
int SOCK_UdpJoinMulticast(S_Udp *udp, const char *maddr);
int SOCK_UdpRecv(S_Udp *udp, unsigned char *buf, unsigned bufsize);
void SOCK_UdpFree(S_Udp *udp);

#endif /* NET_SOCK_H */
