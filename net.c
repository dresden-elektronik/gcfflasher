/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#define MAX_NET_CLIENTS 4

#include <stdio.h>

#include "u_mem.h"
#include "net.h"

#ifdef USE_NET
#include "net_sock.h"

typedef struct NET_Client
{
    S_Addr addr;
    unsigned short port;
} NET_Client;

typedef struct NET_State
{
    S_Udp udp_main;
    unsigned char rx_buf[1280 + 1];

    unsigned n_clients;
    NET_Client clients[MAX_NET_CLIENTS];

} NET_State;

static NET_State net_state;

int NET_Init(const char *interface, unsigned short port)
{
    S_Udp *sock;
    SOCK_Init();

    net_state.n_clients = 0;

    (void)interface; /* TODO */
    sock = &net_state.udp_main;

    if (SOCK_UdpInit(sock, S_AF_IPV4) != 1)
        goto err1;

    if (SOCK_UdpBind(sock, port) != 1)
        goto err1;

    return 1;

err1:
    return 0;
}

static int netCheckNewClient(void)
{
    unsigned i;
    unsigned j;
    unsigned addr_len;
    S_Udp *sock;
    NET_Client *client;

    sock = &net_state.udp_main;
    client = &net_state.clients[0];

    for (i = 0; i < net_state.n_clients; i++, client++)
    {
        if (sock->peer_port == client->port && sock->peer_addr.af == client->addr.af)
        {
            addr_len = (client->addr.af == S_AF_IPV4) ? 4 : 16;
            for (j = 0; j < addr_len; j++)
            {
                if (sock->peer_addr.data[j] != client->addr.data[j])
                    break;
            }

            if (j == addr_len)
            {
                // already known TODO refresh ttl
                return (int)i;
            }
        }
    }

    if (net_state.n_clients < MAX_NET_CLIENTS)
    {
        client = &net_state.clients[net_state.n_clients];
        client->port = sock->peer_port;
        U_memcpy(&client->addr, &sock->peer_addr, sizeof(sock->peer_addr));
        net_state.n_clients++;
        return (int)net_state.n_clients - 1;
    }
    else
    {
        /* clients exhausted (send error to last client?) */
    }

    return -1;
}

int NET_Step(void)
{
    /*
        echo -n "hello" >/dev/udp/127.0.0.1/19817
     */

    int n;
    int client_id;

    n = SOCK_UdpRecv(&net_state.udp_main, net_state.rx_buf, sizeof(net_state.rx_buf) - 1);
    if (n > 0)
    {
        client_id = netCheckNewClient();
        net_state.rx_buf[n] = '\0';
        NET_Received(client_id, net_state.rx_buf, (unsigned)n);
    }

    return 1;
}

void NET_Exit(void)
{
    net_state.n_clients = 0;
    SOCK_UdpFree(&net_state.udp_main);
}

#else
int NET_Init(const char *interface, unsigned short port)
{
    (void)interface;
    (void)port;
    return 0;
}

int NET_Step(void)
{
    return 0;
}

void NET_Exit(void)
{
}
#endif
