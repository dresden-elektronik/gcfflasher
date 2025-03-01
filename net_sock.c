
#include "net_sock.h"

int SOCK_Init(void)
{
    return 0;
}

void SOCK_Free(void)
{

}

int SOCK_GetHostAF(const char *host)
{
    if (host)
    {
        for (;*host; host++)
        {
            if (*host == ':')
                return S_AF_IPV6;

            if (*host == '.')
                return S_AF_IPV4;
        }
    }

    return S_AF_UNKNOWN;
}