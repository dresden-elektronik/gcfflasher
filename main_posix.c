/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#define __STDC_FORMAT_MACROS
#include <inttypes.h> /* printf types */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h> /* va_list, ... */
#include <assert.h>
#include <limits.h>
#include <poll.h>
#include <fcntl.h> /* open() */
#include <unistd.h> /* close() */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <string.h> /* memset() */
#include <errno.h>
#include <dlfcn.h>
#include <termios.h> /* POSIX terminal control definitions */

#include <ctype.h>

#include "gcf.h"
#include "protocol.h"

#define RX_BUF_SIZE 1024
#define TX_BUF_SIZE 2048

typedef struct
{
    uint64_t timer;
    int fd;
    uint8_t running;
    uint8_t rxbuf[RX_BUF_SIZE];
    uint8_t txbuf[TX_BUF_SIZE];
    uint32_t tx_rp;
    uint32_t tx_wp;
    GCF *gcf;
} PL_Internal;

static PL_Internal platform;

#ifdef PL_LINUX
  #include "linux_get_usb_devices.c"
  #include "linux_libgpiod_reset.c"
#endif

#ifdef PL_MAC
  #include "posix_libftdi_reset.c"
#endif

static int plSetupPort(int fd, int baudrate)
{
    struct termios options;


    fcntl(fd, F_SETFL, O_RDWR | /*O_NONBLOCK |*/ O_NOCTTY);

    tcgetattr(fd, &options);

    cfsetispeed(&options, baudrate);
    cfsetospeed(&options, baudrate);

    cfmakeraw(&options);
    /* Enable the receiver and set local mode... */
    options.c_cflag |= (CLOCAL | CREAD);

    // No parity 8N1
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    /* disable hardware control flow */
    options.c_cflag &= ~CRTSCTS;

    tcsetattr(fd, TCSANOW, &options);

    return 0;
}

/* Returns a monotonic timestamps in milliseconds */
uint64_t PL_Time()
{
    struct timespec ts;
    uint64_t res = 0;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        res = ts.tv_sec * 1000;
        res += ts.tv_nsec / 1000000;
    }

    return res;
}

void PL_MSleep(uint64_t ms)
{
    while (ms > 0)
    {
        usleep(1000);
        ms--;
    }
}

int PL_ResetFTDI(int num)
{
#ifdef HAS_LIBGPIOD
    return plResetFtdiLibGpiod();
#endif

#ifdef HAS_LIBFTDI
    return plResetLibFtdi();
#endif

    return -1;
}

int PL_ResetRaspBee()
{
#ifdef HAS_LIBGPIOD
    return plResetRaspBeeLibGpiod();
#endif
    return -1;
}

void *PL_Malloc(unsigned size)
{
    void *p = malloc(size);

    if (p)
    {
        memset(p, 0, size);
    }
    return p;
}

void PL_Free(void *p)
{
    if (p)
    {
        free(p);
    }
}

void PL_Print(const char *line)
{
    int n = write(STDOUT_FILENO, line, strlen(line));
    (void)n;
}

void PL_Printf(DebugLevel level, const char *format, ...)
{
#ifdef NDEBUG
    if (level == DBG_DEBUG)
    {
        return;
    }
#endif
    va_list args;
    va_start (args, format);
    vprintf(format, args);
    va_end (args);
}

GCF_Status PL_Connect(const char *path)
{
    if (platform.fd != 0)
    {
        PL_Printf(DBG_DEBUG, "device already connected %s\n", path);
        return GCF_SUCCESS;
    }

    platform.fd = open(path, O_CLOEXEC | O_RDWR /*| O_NONBLOCK*/);
    platform.tx_rp = 0;
    platform.tx_wp = 0;

    if (platform.fd < 0)
    {
        PL_Printf(DBG_DEBUG, "failed to open device %s\n", path);
        platform.fd = 0;
        return GCF_FAILED;
    }

    PL_Printf(DBG_DEBUG, "connected to %s\n", path);


    int baudrate = 0;

    if (baudrate == 0)
    {
        baudrate = B38400;

        if (strstr(path, "ACM")) /* ConBee II Linux */
        {
            baudrate = B115200;
        }
        else if (strstr(path, "cu.usbmodemDE")) /* ConBee II macOS */
        {
            baudrate = B115200;
        }
    }

    plSetupPort(platform.fd, baudrate);

    return GCF_SUCCESS;
}

void PL_Disconnect()
{
    if (platform.fd != 0)
    {
        close(platform.fd);
        platform.fd = 0;
    }
    platform.tx_rp = 0;
    platform.tx_wp = 0;
    GCF_HandleEvent(platform.gcf, EV_DISCONNECTED);
}

void PL_ShutDown()
{
    PL_Printf(DBG_DEBUG, "shutdown\n");
    platform.running = 0;
}

int PL_ReadFile(const char *path, uint8_t *buf, size_t buflen)
{
    Assert(path && buf && buflen >= MAX_GCF_FILE_SIZE);

    int fd;
    int ret = -1;

    fd = open(path, O_RDONLY);

    if (fd == -1)
    {
        PL_Printf(DBG_DEBUG, "failed to open %s, err: %s\n", path, strerror(errno));
        return ret;
    }

    ret = read(fd, buf, buflen);
    if (ret == -1)
    {
        PL_Printf(DBG_DEBUG, "failed to read %s, err: %s\n", path, strerror(errno));
    }

    if (close(fd) == -1)
    {
        PL_Printf(DBG_DEBUG, "failed to close %s, err: %s\n", path, strerror(errno));
    }

    return ret;
}

void PL_SetTimeout(uint64_t ms)
{
    platform.timer = PL_Time() + ms;
}

void PL_ClearTimeout()
{
    platform.timer = 0;
}

int PL_GetDevices(Device *devs, size_t max)
{
    int result = 0;

#ifdef PL_LINUX
    result = plGetLinuxUSBDevices(devs, devs + max);
#endif

    return result;
}

int PROT_Write(const unsigned char *data, unsigned short len)
{
    int result;
    unsigned i;

    result = 0;
    for (i = 0; i < len; i++)
        result += PROT_Putc(data[i]);

    PROT_Flush();

    return result;
}

int PROT_Putc(unsigned char ch)
{
    if (platform.fd == 0)
        return 0;

    platform.txbuf[platform.tx_wp % TX_BUF_SIZE] = ch;
    platform.tx_wp++;

    if ((platform.tx_wp % TX_BUF_SIZE) == (platform.tx_rp % TX_BUF_SIZE))
        platform.tx_rp++; /* overwrite oldest */

    return 1;
}

int PROT_Flush()
{
    int n;
    unsigned pos;
    unsigned len;
    uint8_t buf[512];

    if (platform.fd == 0)
    {
        platform.tx_wp = 0;
        platform.tx_rp = 0;
        GCF_HandleEvent(platform.gcf, EV_DISCONNECTED);
        return -1;
    }

    len = 0;
    for (len = 0; len < sizeof(buf); len++)
    {
        if ((platform.tx_wp % TX_BUF_SIZE) == ((platform.tx_rp + len) % TX_BUF_SIZE))
            break;
        buf[len] = platform.txbuf[(platform.tx_rp + len) % TX_BUF_SIZE];
    }

    pos = 0;

    for (;pos < len;)
    {
        n = (int)write(platform.fd, &buf[pos], len - pos);
        if (n == -1)
        {
            if (errno == EINTR)
                continue;
            PL_Printf(DBG_DEBUG, "write() failed: %s\n", strerror(errno));
            break;
        }
        else if (n > 0 && n <= (len - pos))
        {
            pos += (unsigned)n;
        }
        else
        {
            break; /* should never happen */
        }
    }

    platform.tx_rp += pos;

    return (int)pos;
}

void UI_GetWinSize(uint16_t *w, uint16_t *h)
{
    struct winsize size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);

    *w = size.ws_col;
    *h = size.ws_row;
}

/*  Unicode box drawing chars
    https://en.wikipedia.org/wiki/Box-drawing_character
*/
void UI_SetCursor(uint16_t x, uint16_t y)
{
    // ESC[{line};{column}H
    char buf[24];
    sprintf(buf, FMT_ESC "[%u;%uH", (unsigned)y, (unsigned)x);
    PL_Print(buf);
}

static int PL_Loop(GCF *gcf)
{
    memset(&platform, 0, sizeof(platform));
    platform.gcf = gcf;

    platform.running = 1;

    struct pollfd fds;
    fds.events = POLLIN;

    GCF_HandleEvent(gcf, EV_PL_STARTED);

    while (platform.running)
    {
        /* when no device is connected, poll STDIN, to get poll() timeout */
        fds.fd = platform.fd != 0 ? platform.fd : STDIN_FILENO;

        int ret = poll(&fds, 1, 5);

        if (ret < 0)
            break;

        if (ret == 0)
        {
            if (platform.timer != 0)
            {
                if (platform.timer < PL_Time())
                {
                    platform.timer = 0;
                    GCF_HandleEvent(gcf, EV_TIMEOUT);
                }
            }

            continue;
        }

        if (fds.revents & (POLLHUP | POLLERR | POLLNVAL) && platform.fd != 0)
        {
            PL_Disconnect();
            continue;
        }

        if (fds.revents & POLLIN)
        {
            int nread = read(fds.fd, platform.rxbuf, sizeof(platform.rxbuf));

            if (nread > 0)
            {
                GCF_Received(gcf, platform.rxbuf, nread);
            }
        }

        if (platform.fd && platform.tx_rp != platform.tx_wp)
        {
            PROT_Flush();
        }
    }

    PL_Disconnect();

    return 1;
}

int main(int argc, char *argv[])
{
    GCF *gcf = GCF_Init(argc, argv);
    if (gcf == NULL)
        return 2;

    PL_Loop(gcf);

    GCF_Exit(gcf);

    return 0;
}
