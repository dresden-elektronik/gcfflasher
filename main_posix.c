/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h> /* va_list, ... */
#include <poll.h>
#include <fcntl.h> /* open() */
#include <unistd.h> /* close() */
//#include <sys/types.h>
#include <sys/ioctl.h>
#include <time.h>
#include <string.h> /* memset() */
#include <errno.h>
#include <dlfcn.h>
#include <termios.h> /* POSIX terminal control definitions */

#include "gcf.h"
#include "protocol.h"
#include "u_sstream.h"
#include "u_mem.h"

#define RX_BUF_SIZE 1024
#define TX_BUF_SIZE 2048

typedef struct
{
    PL_time_t timer;
    int fd;
    unsigned char running;
    unsigned char rxbuf[RX_BUF_SIZE];
    unsigned char txbuf[TX_BUF_SIZE];
    unsigned tx_rp;
    unsigned tx_wp;
    GCF *gcf;
} PL_Internal;

static PL_Internal platform;
static struct termios restore_attr;

#ifdef PL_LINUX
int plGetLinuxUSBDevices(Device *dev, Device *end);
int plGetLinuxSerialDevices(Device *dev, Device *end);

#ifdef HAS_LIBGPIOD
int plResetRaspBeeLibGpiod(void);
int plResetFtdiLibGpiod(void);
#endif

#endif

#ifdef PL_MAC
  #include "posix_libftdi_reset.c"
int plGetMacOSUSBDevices(Device *dev, Device *end);
#endif

static int plSetupPort(int fd, int baudrate)
{
    struct termios options;

    fcntl(fd, F_SETFL, O_RDWR | /*O_NONBLOCK |*/ O_NOCTTY);

    tcgetattr(fd, &options);

    cfsetispeed(&options, (speed_t)baudrate);
    cfsetospeed(&options, (speed_t)baudrate);

    cfmakeraw(&options);
    /* Enable the receiver and set local mode... */
    options.c_cflag |= (CLOCAL | CREAD);

    // No parity 8N1
    options.c_cflag &= (tcflag_t)~PARENB;
    options.c_cflag &= (tcflag_t)~CSTOPB;
    options.c_cflag &= (tcflag_t)~CSIZE;
    options.c_cflag |= (tcflag_t)CS8;

    /* disable hardware control flow */
    options.c_cflag &= (tcflag_t)~CRTSCTS;

    tcsetattr(fd, TCSANOW, &options);

    return 0;
}

/* Returns a monotonic timestamps in milliseconds */
PL_time_t PL_Time(void)
{
    PL_time_t res;
    struct timespec ts;

    res = 0;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        res = ts.tv_sec * 1000;
        res += ts.tv_nsec / 1000000;
    }

    return res;
}

void PL_MSleep(unsigned long ms)
{
    while (ms > 0)
    {
        usleep(1000);
        ms--;
    }
}

int PL_ResetFTDI(int num, const char *serialnum)
{
    (void)num;
    (void)serialnum;
#ifdef HAS_LIBGPIOD
    return plResetFtdiLibGpiod();
#endif

#ifdef HAS_LIBFTDI
    return plResetLibFtdi();
#endif

    return -1;
}

int PL_ResetRaspBee(void)
{
#ifdef HAS_LIBGPIOD
    return plResetRaspBeeLibGpiod();
#endif
    return -1;
}

void PL_Print(const char *line)
{
    ssize_t n = write(STDOUT_FILENO, line, strlen(line));
    (void)n;
}

void PL_Printf(DebugLevel level, const char *format, ...)
{
    FILE *fp;

    fp = stdout;
#ifdef NDEBUG
    if (level == DBG_DEBUG)
        return;
#endif
    if (level == DBG_DEBUG)
        fp = stderr;

    va_list args;
    va_start (args, format);
    vfprintf(fp, format, args);
    va_end (args);
}

GCF_Status PL_Connect(const char *path, PL_Baudrate baudrate)
{
    PL_Printf(DBG_DEBUG, "PL_Connect\n");

    int baudrate1 = 0;

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

    if (baudrate == PL_BAUDRATE_38400)
    {
        baudrate1 = B38400;
    }
    else if (baudrate == PL_BAUDRATE_115200)
    {
        baudrate1 = B115200;
    }

    if (baudrate1 == 0)
    {
        baudrate1 = B38400;
#if 0
        if (strstr(path, "ACM")) /* ConBee II Linux */
        {
            baudrate1 = B115200;
        }
        else if (strstr(path, "cu.usbmodemDE")) /* ConBee II macOS */
        {
            baudrate1 = B115200;
        }
#endif
    }

    plSetupPort(platform.fd, baudrate1);

    PL_Printf(DBG_DEBUG, "connected to %s, baudrate: %d\n", path, baudrate);

    return GCF_SUCCESS;
}

void PL_Disconnect(void)
{
    PL_Printf(DBG_DEBUG, "PL_Disconnect\n");
    if (platform.fd != 0)
    {
        close(platform.fd);
        platform.fd = 0;
    }
    platform.tx_rp = 0;
    platform.tx_wp = 0;
    GCF_HandleEvent(platform.gcf, EV_DISCONNECTED);
}

void PL_ShutDown(void)
{
    PL_Printf(DBG_DEBUG, "PL_Shutdown\n");
    platform.running = 0;
}

int PL_ReadFile(const char *path, unsigned char *buf, unsigned long buflen)
{
    int fd;
    int ret;

    Assert(path && buf && buflen >= MAX_GCF_FILE_SIZE);

    ret = -1;
    fd = open(path, O_RDONLY);

    if (fd == -1)
    {
        PL_Printf(DBG_DEBUG, "failed to open %s, err: %s\n", path, strerror(errno));
        return ret;
    }

    ret = (int)read(fd, buf, buflen);
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

void PL_SetTimeout(unsigned long ms)
{
    platform.timer = PL_Time() + ms;
}

void PL_ClearTimeout(void)
{
    platform.timer = 0;
}

int PL_GetDevices(Device *devs, unsigned max)
{
    int result = 0;

    U_bzero(devs, sizeof(*devs) * max);

#ifdef PL_LINUX
    result = plGetLinuxUSBDevices(devs, devs + max);
    if (result == 0) // only include RaspBee if no USB devices where found
    {
        result += plGetLinuxSerialDevices(devs + result, devs + max);
    }
#endif

#ifdef PL_MAC
    result = plGetMacOSUSBDevices(devs, devs + max);
#endif

    return result;
}

int PROT_Write(const unsigned char *data, unsigned len)
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

int PROT_Flush(void)
{
    int n;
    unsigned pos;
    unsigned len;
    unsigned char buf[512];

    if (platform.fd == 0)
    {
        platform.tx_wp = 0;
        platform.tx_rp = 0;
        GCF_HandleEvent(platform.gcf, EV_DISCONNECTED);
        return -1;
    }

    for (len = 0; len < sizeof(buf); len++)
    {
        if ((platform.tx_wp % TX_BUF_SIZE) == ((platform.tx_rp + len) % TX_BUF_SIZE))
            break;
        buf[len] = platform.txbuf[(platform.tx_rp + len) % TX_BUF_SIZE];
    }

    gcfDebugHex(platform.gcf, "send", &buf[0], len);

    for (pos = 0; pos < len;)
    {
        n = (int)write(platform.fd, &buf[pos], len - pos);
        if (n == -1)
        {
            if (errno == EINTR)
                continue;
            PL_Printf(DBG_DEBUG, "write() failed: %s\n", strerror(errno));
            break;
        }
        else if (n > 0 && n <= (int)(len - pos))
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

void UI_GetWinSize(unsigned *w, unsigned *h)
{
    struct winsize size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);

    *w = size.ws_col;
    *h = size.ws_row;
}

/*  Unicode box drawing chars
    https://en.wikipedia.org/wiki/Box-drawing_character
*/
void UI_SetCursor(unsigned x, unsigned y)
{
    U_SStream ss;
    char buf[24];
    U_sstream_init(&ss, buf, sizeof(buf));
    U_sstream_put_str(&ss, FMT_ESC "[");
    U_sstream_put_long(&ss, (long)y);
    U_sstream_put_str(&ss, ";");
    U_sstream_put_long(&ss, (long)x);
    U_sstream_put_str(&ss, "H");
    PL_Print(buf);
}

static void PL_InitKeyboard(void)
{
    struct termios attr;
    tcgetattr(STDIN_FILENO, &attr);
    tcgetattr(STDIN_FILENO, &restore_attr);
    attr.c_lflag &= ~(ICANON | ECHO); /* turn off canonical mode */
    tcsetattr(STDIN_FILENO, TCSANOW, &attr);
}

static void PL_AtExit(void)
{
    restore_attr.c_lflag |= (ICANON | ECHO); /* turn on canonical mode */
    tcsetattr(STDIN_FILENO, TCSANOW, &restore_attr);
}

static int PL_Loop(GCF *gcf)
{
    int nfds;
    int ret;
    int nread;
    struct pollfd fds[2];
    unsigned codepoint;

    PL_InitKeyboard();

    memset(&platform, 0, sizeof(platform));
    platform.gcf = gcf;

    platform.running = 1;

    fds[0].events = POLLIN;
    fds[1].events = POLLIN;

    GCF_HandleEvent(gcf, EV_PL_STARTED);

    while (platform.running)
    {
        GCF_HandleEvent(gcf, EV_PL_LOOP);

        nfds = 0;

        /* always poll STDIN at fds[0], to get poll() timeout and keyboard input */
        fds[nfds++].fd = STDIN_FILENO;

        /* when device is connected fds[1] */
        if (platform.fd != 0)
            fds[nfds++].fd = platform.fd;

        ret = poll(&fds[0], nfds, 5);

        if (ret < 0)
        {
            PL_Printf(DBG_DEBUG, "poll error: %s\n", strerror(errno));
            break;
        }

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

        if (nfds == 2) /* device connected */
        {
            if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL))
            {
                PL_Disconnect();
                continue;
            }

            if (fds[1].revents & POLLIN)
            {
                nread = (int) read(fds[1].fd, platform.rxbuf, sizeof(platform.rxbuf));

                if (nread > 0)
                {
                    GCF_Received(gcf, platform.rxbuf, nread);
                }
            }

            if (platform.tx_rp != platform.tx_wp)
            {
                PROT_Flush();
            }
        }

        /* keyboard input */
        if (fds[0].revents & POLLIN)
        {
            codepoint = 0;
            nread = (int) read(fds[0].fd, platform.rxbuf, sizeof(platform.rxbuf));

            /* simplified input for ASCII and navigation keys */

            if (nread == 1)
            {
                codepoint = platform.rxbuf[0];
                if (codepoint >= 32 && codepoint <= 126)
                { } /* ASCII */
                else if (codepoint == 0x09) { codepoint = PL_KEY_TAB; }
                else if (codepoint == 0x0A) { codepoint = PL_KEY_ENTER; }
                else if (codepoint == 0x1B) { codepoint = PL_KEY_ESC; }
                else if (codepoint == 0x7F) { codepoint = PL_KEY_BACKSPACE; }
                else                        { codepoint = 0; }
            }
            else if (nread >= 3 && platform.rxbuf[0] == 0x1B && platform.rxbuf[1] == 0x5B)
            {
                switch (platform.rxbuf[2])
                {
                    case 0x33: codepoint = PL_KEY_DELETE; break;
                    case 0x41: codepoint = PL_KEY_UP; break;
                    case 0x42: codepoint = PL_KEY_DOWN; break;
                    case 0x43: codepoint = PL_KEY_RIGHT; break;
                    case 0x44: codepoint = PL_KEY_LEFT; break;
                    case 0x48: codepoint = PL_KEY_POS1; break;
                    case 0x46: codepoint = PL_KEY_END; break;
                }
            }

            if (codepoint)
                GCF_KeyboardInput(gcf, codepoint);

#ifndef NDEBUG
            if (codepoint == 0)
            {
                for (int b = 0; b < nread; b++)
                {
                    PL_Printf(DBG_INFO, "IN: [%d] = 0x%02X (%c) \n", b, platform.rxbuf[b] & 0xFF,
                              platform.rxbuf[b] & 0xFF);
                }
            }
#endif
        }
    }

    PL_Disconnect();

    return 1;
}

int main(int argc, char *argv[])
{
    GCF *gcf;

    atexit(PL_AtExit);

    gcf = GCF_Init(argc, argv);
    if (gcf == NULL)
        return 2;

    PL_Loop(gcf);

    GCF_Exit(gcf);

    return 0;
}
