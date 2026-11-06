/*
 * Copyright (c) 2021-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

/*
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <shlwapi.h>
#ifndef __WATCOMC__
#include <devpkey.h>
#endif
#include <tchar.h>
#include <stdarg.h>

#include "gcf.h"
#include "u_sstream.h"
#include "u_strlen.h"
#include "u_mem.h"

/* in gcf.c for now */
extern void U_sstream_put_u32hex(U_SStream *ss, unsigned long val);

// https://hero.handmade.network/forums/code-discussion/t/94-guide_-_how_to_avoid_c_c++_runtime_on_windows

#define FTDIBUS_IOCTL_SET_BITMODE 0x222178
#define FTDI_BITMODE_RESET 0x00
#define FTDI_BITMODE_CBUS  0x20

typedef struct
{
    PL_time_t timer;
    HANDLE fd;
    char fdPath[8]; /* to reopen the port after the FTDI reset */
    PL_Baudrate fdBaudrate;
    HANDLE hOut;
    int running;
    unsigned char rxbuf[1024];
    unsigned char txbuf[2048];
    unsigned long txpos;

    LARGE_INTEGER frequency;
    BOOL frequencyValid;

    GCF *gcf;
} PL_Internal;

static PL_Internal platform;


static int GetComPort(const char *enumerator, Device *devs, size_t max);

/* for compiling without CRT */
int _fltused=0;

#ifdef _MSC_VER
#pragma function(memcpy)
void *memcpy(void *dst, const void *src, SIZE_T n)
#else
void *memcpy(void *dst, const void *src, unsigned long n)
#endif
{
    return U_memcpy(dst, src, (unsigned long)n);
}

#ifdef _MSC_VER
#pragma function(memset)
void *memset(void *dst, int c, SIZE_T count)
#else
void *memset(void *dst, int c, size_t count)
#endif
{
    char *p = dst;
    for (;count; count--)
        *p++ = (char)c;

    return dst;
}

#ifdef _X86_
/* mingw32 on i686 doesn't have long devision */
__attribute((externally_visible))
long __divdi3(long n, long d)
{
    long q = 0;
    if (n == 0 || d == 0)
        return 0;

      while (n >= d)
      {
        ++q;
        n -= d;
      }
      return q;
}
#endif

/*! Returns a monotonic time in milliseconds. */
PL_time_t PL_Time(void)
{
    if (platform.frequencyValid)
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (1000LL * now.QuadPart) / platform.frequency.QuadPart;
    }

    return GetTickCount();
}

/*! Lets the programm sleep for \p ms milliseconds. */
void PL_MSleep(unsigned long ms)
{
    Sleep((DWORD)ms);
}


/*! Sets a timeout \p ms in milliseconds, after which a \c EV_TIMOUT event is generated. */
void PL_SetTimeout(unsigned long ms)
{
    platform.timer = PL_Time() + ms;
}

/*! Clears an active timeout. */
void PL_ClearTimeout(void)
{
    platform.timer = 0;
}

/* Fills up to \p max devices in the \p devs array.

   The output is used in list operation (-l).
*/
int PL_GetDevices(Device *devs, unsigned max)
{
    // http://www.naughter.com/enumser.html

    int result = 0;
    unsigned i = 0;

    ZeroMemory(devs, sizeof(*devs) * max);

    GetComPort("USB", devs, max);
    GetComPort("FTDIBUS", devs, max);

    for (i = 0; i < max; i++)
    {
        if (devs[i].serial[0] != '\0' && devs[i].path[0] != '\0')
            result++;
    }

    return result;
}

static int GetComPort(const char *enumerator, Device *devs, size_t max)
{
    int devcount = 0;

    if (max == 0)
    {
        return devcount;
    }

    int i;
    char ch;
    U_SStream ss;
    Device *dev = 0;
    HDEVINFO DeviceInfoSet;
    DWORD DeviceIndex =0;
    SP_DEVINFO_DATA DeviceInfoData;
    BYTE szBuffer[256];
    wchar_t wcbuf[128];
    DEVPROPTYPE ulPropertyType;
    DWORD dwSize = 0;
    DWORD dwType = 0;
    DEVPROPKEY PropertyKey;
    DEVPROPTYPE PropertyType = 0;

    unsigned vid;
    unsigned pid;
    
    // setupDiGetClassDevs returns a handle to a device information set
    DeviceInfoSet = SetupDiGetClassDevs(
                        NULL,
                        enumerator,
                        NULL,
                        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    
    if (DeviceInfoSet == INVALID_HANDLE_VALUE)
        return devcount;

    // fills a block of memory with zeros
    ZeroMemory(&DeviceInfoData, sizeof(SP_DEVINFO_DATA));
    DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    // receive information about an enumerated device
    while (SetupDiEnumDeviceInfo(
                DeviceInfoSet,
                DeviceIndex,
                &DeviceInfoData) && devcount < max)
    {
        DeviceIndex++;

        szBuffer[0] = '\0';

        if (SetupDiGetDeviceInstanceId(DeviceInfoSet, &DeviceInfoData, (_TCHAR *)&szBuffer[0], sizeof(szBuffer), NULL))
        {
        }

        // USB\VID_1CF1&PID_0030\DE1995634 @ DE1995634
        // FTDIBUS\VID_0403+PID_6015+DJ00QBWEA\0000 @ 0000
        if (szBuffer[0] == '\0')
            continue;

        U_sstream_init(&ss, &szBuffer[0], U_strlen((char*)&szBuffer[0]));

        // filter vendor and product ids
        if (U_sstream_find(&ss, "VID_1CF1") && U_sstream_find(&ss, "PID_0030")) // ConBee II
        {
            vid = 0x1cf1;
            pid = 0x0030;
        }
        else if (U_sstream_find(&ss, "VID_0403") && U_sstream_find(&ss, "PID_6015")) // ConBee I and III
        {
            vid = 0x0403;
            pid = 0x6015;
        }
        else if (U_sstream_find(&ss, "VID_1A86") && U_sstream_find(&ss, "PID_7523")) // CH340 ~ Hive
        {
            vid = 0x1a86;
            pid = 0x7523;
        }
        else if (U_sstream_find(&ss, "VID_303A") && U_sstream_find(&ss, "PID_1001")) // Espressif ~ FLS-M
        {
            vid = 0x303a;
            pid = 0x1001;
        }
        else
        {

            continue;
        }

        ss.pos += 8; // move behind PID_XXXX

        char serial[MAX_DEV_SERIALNR_LENGTH];
        serial[0] = '\0';

        // extract serial number
        // important: look for '+' first as the FTDI serial also contains a '\' !
        if (U_sstream_peek_char(&ss) == '+' || U_sstream_peek_char(&ss) == '\\')
        {
            ss.pos++;
            for (i = 0; ss.pos < ss.len; ss.pos++, i++)
            {
                if (i + 2 >= (int)sizeof(serial))
                    break;

                ch = ss.str[ss.pos];

                if ((ch >= 'A' && ch <= 'Z') ||
                    (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9'))
                {
                    serial[i] = ch;
                }
                else
                {
                    // for some reason FTDIBUS\VID_0403+PID_6015+DJ00QBWEA\0000
                    // has A\0000 appended, remove here
                    if (ch == '\\' && i != 0 && serial[i - 1] == 'A')
                    {
                        serial[i - 1] = '\0';
                    }

                    break;
                }

            }

            serial[i] = '\0';
        }
        else if (vid == 0x303a && pid == 0x1001)
        {
            serial[0] = 'u';
            serial[1] = 'n';
            serial[2] = 'k';
            serial[3] = 'n';
            serial[4] = 'o';
            serial[5] = 'w';
            serial[6] = 'n';
            serial[7] = '\0';
        }
        else
        {
            continue; /* no serial number? */
        }

        if (serial[0] == '\0')
            continue;

        dev = 0;

        for (i = 0; i < max; i++)
        {
            U_sstream_init(&ss, devs[i].serial, U_strlen(devs[i].serial));

            if (U_sstream_starts_with(&ss, serial))
            {
                dev = &devs[i]; // already known
                break;
            }
        }

        // no device with this serial yet, take an empty one
        if (!dev)
        {
            for (i = 0; i < max; i++)
            {
                if (devs[i].serial[0] == '\0')
                {
                    dev = &devs[i];
                    devcount++;
                    U_sstream_init(&ss, dev->serial, sizeof(dev->serial));
                    U_sstream_put_str(&ss, serial);
                    break;
                }
            }
        }

        if (!dev) // all slots full
        {
            PL_Printf(DBG_DEBUG, "ALL SLOTS FULL\n");
            continue;
        }

        /*** check device name (only ConBee II and ConBee III) ********************/
#ifndef __WATCOMC__
        /* for ConBee III this happens when enumerator == "USB" */
        PropertyKey = DEVPKEY_Device_BusReportedDeviceDesc;

        if (SetupDiGetDevicePropertyW(DeviceInfoSet, &DeviceInfoData, &PropertyKey,
            &PropertyType, (BYTE*)wcbuf, sizeof(wcbuf), NULL, 0))
        {
            if (PropertyType == DEVPROP_TYPE_STRING)
            {
                for (i = 0; wcbuf[i]; i++)
                {
                    szBuffer[i] = (BYTE)wcbuf[i]; // wchar to ASCII
                }
                szBuffer[i] = '\0';

                // we may get here multiple times for ConBee III
                // the generic FTDI name will be overwritten with ConBee III if not already set
                if (dev->name[0] != 'C')
                {
                    for (i = 0; i < sizeof(dev->name) && szBuffer[i]; i++)
                        dev->name[i] = (char)szBuffer[i];

                    dev->name[i] = '\0';

                    U_sstream_init(&ss, dev->name, U_strlen(dev->name));
                    if (U_sstream_starts_with(&ss, "ConBee")) // ConBee II and III
                    {
                        dev->baudrate = PL_BAUDRATE_115200;
                    }
                    else if (vid == 0x0403) // FTDI ConBee I
                    {
                        U_sstream_init(&ss, dev->name, sizeof(dev->name));
                        U_sstream_put_str(&ss, "Serial FTDI");
                        dev->baudrate = PL_BAUDRATE_38400;

                    }
                    else if (vid == 0x1a86) // CH340 Hive
                    {
                        U_sstream_init(&ss, dev->name, sizeof(dev->name));
                        U_sstream_put_str(&ss, "Serial CH340");
                        dev->baudrate = PL_BAUDRATE_115200;
                    }
                    else if (vid == 0x303a) // Espressif ~ FLS-M
                    {
                        U_sstream_init(&ss, dev->name, sizeof(dev->name));
                        U_sstream_put_str(&ss, "Espressif");
                        dev->baudrate = PL_BAUDRATE_115200;
                    }
                }
            }
        }
#endif
        /**************************************************************************/

        if (devs->name[0] == '\0')
            continue;

        // retrieves a specified Plug and Play device property
        if (SetupDiGetDeviceRegistryProperty (DeviceInfoSet, &DeviceInfoData, SPDRP_HARDWAREID,
                                              &ulPropertyType, (BYTE*)szBuffer,
                                              sizeof(szBuffer),
                                              &dwSize))
        {
            HKEY hDeviceRegistryKey;
            // get registry the key
            // HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Control\DeviceMigration\Devices\USB

            hDeviceRegistryKey = SetupDiOpenDevRegKey(DeviceInfoSet, &DeviceInfoData,DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);

            if (hDeviceRegistryKey != INVALID_HANDLE_VALUE)
            {
                // read name of the port
                char pszPortName[20];
                dwType = 0;
                dwSize = sizeof(pszPortName);

                if ((RegQueryValueEx(hDeviceRegistryKey, "PortName", NULL, &dwType,
                    (LPBYTE) pszPortName, &dwSize) == ERROR_SUCCESS) && (dwType == REG_SZ))
                {
                    // is a com port?
                    if (pszPortName[0] == 'C' && pszPortName[1] == 'O' && pszPortName[2] == 'M')
                    {
                        if (pszPortName[3] >= '0' && pszPortName[3] <= '9')
                        {
                            unsigned sz = 0;
                            for (; pszPortName[sz] && sz + 1 < sizeof(dev->path); sz++)
                            {
                                dev->path[sz] = pszPortName[sz];
                                dev->stablepath[sz] = pszPortName[sz];
                            }
                            dev->path[sz] = '\0';
                            dev->stablepath[sz] = '\0';
                        }
                    }
                }

                RegCloseKey(hDeviceRegistryKey);
            }
        }
    }

    if (DeviceInfoSet)
    {
        SetupDiDestroyDeviceInfoList(DeviceInfoSet);
    }

    return devcount;
}

/*! Opens the serial port connection for device.

    \param path - The path like /dev/ttyACM0 or COM7.
    \returns GCF_SUCCESS or GCF_FAILED
 */
GCF_Status PL_Connect(const char *path, PL_Baudrate baudrate)
{
    char buf[32];
    U_SStream ss;

    if (platform.fd != INVALID_HANDLE_VALUE)
    {
        PL_Printf(DBG_DEBUG, "device already connected %s\n", path);
        return GCF_SUCCESS;
    }

    if (U_strlen(path) > 7)
    {
        return GCF_FAILED;
    }

    U_sstream_init(&ss, platform.fdPath, sizeof(platform.fdPath));
    U_sstream_put_str(&ss, path);
    platform.fdBaudrate = baudrate;

    U_sstream_init(&ss, buf, sizeof(buf));
    if (*path == 'C')
    {
        U_sstream_put_str(&ss, "\\\\.\\");
        U_sstream_put_str(&ss, path);
    }
    else if (*path == '\\')
    {
        U_sstream_put_str(&ss, path);
    }
    else
    {
        return GCF_FAILED;
    }

    PL_Printf(DBG_INFO, "connect %s, baudrate %d\n", buf, (int)baudrate);

    platform.txpos = 0;

    platform.fd = CreateFile(
                  buf,
                  GENERIC_READ | GENERIC_WRITE,
                  0,
                  NULL,
                  OPEN_EXISTING,
                  0,
                  NULL);

    if (platform.fd == INVALID_HANDLE_VALUE)
    {
        PL_Printf(DBG_DEBUG, "failed to open %s\n", buf);
        return GCF_FAILED;
    }

    static DCB dcbSerialParams;  // Initializing DCB structure
    static COMMTIMEOUTS timeouts;  //Initializing timeouts structure

    ZeroMemory(&dcbSerialParams, sizeof(dcbSerialParams));
    ZeroMemory(&timeouts, sizeof(timeouts));

    //Setting the Parameters for the SerialPort
    BOOL Status;
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    Status = GetCommState(platform.fd, &dcbSerialParams); //retreives  the current settings
    if (Status == FALSE)
    {
        PL_Printf(DBG_DEBUG, "\nError to Get the Com state\n\n");
        goto Exit1;
    }

    if (baudrate == PL_BAUDRATE_38400)
    {
        dcbSerialParams.BaudRate = CBR_38400;
    }
    else if (baudrate == PL_BAUDRATE_115200)
    {
        dcbSerialParams.BaudRate = CBR_115200;
    }
    else
    {
        dcbSerialParams.BaudRate = CBR_115200;
    }

    dcbSerialParams.ByteSize = 8;            //ByteSize = 8
    dcbSerialParams.StopBits = ONESTOPBIT;    //StopBits = 1
    dcbSerialParams.Parity = NOPARITY;      //Parity = None
    dcbSerialParams.fBinary = TRUE;

    Status = SetCommState(platform.fd, &dcbSerialParams);
    if (Status == FALSE)
    {
        PL_Printf(DBG_DEBUG, "\nError to Setting DCB Structure\n\n");
        goto Exit1;
    }
    //Setting Timeouts
    timeouts.ReadIntervalTimeout = 1;
    timeouts.ReadTotalTimeoutConstant = 20;
    timeouts.ReadTotalTimeoutMultiplier = 1;
    timeouts.WriteTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    if (SetCommTimeouts(platform.fd, &timeouts) == 0)
    {
        PL_Printf(DBG_DEBUG, "\nError to Setting Time outs");
        goto Exit1;
    }

    Status = SetCommMask(platform.fd, EV_RXCHAR);
    if (Status == FALSE)
    {
        PL_Printf(DBG_DEBUG, "\nError to in Setting CommMask\n\n");
        goto Exit1;
    }

    PL_Printf(DBG_DEBUG, "connected com port %s, %lu\n", buf, (unsigned long)baudrate);

    return GCF_SUCCESS;

Exit1:
    PL_Disconnect();
    return GCF_FAILED;
}

/*! Closed the serial port connection. */
void PL_Disconnect(void)
{
    PL_Printf(DBG_DEBUG, "PL_Disconnect\n");
    if (platform.fd != INVALID_HANDLE_VALUE)
    {
        platform.txpos = 0;
        CloseHandle(platform.fd);
        platform.fd = INVALID_HANDLE_VALUE;
    }
    GCF_HandleEvent(platform.gcf, EV_DISCONNECTED);
}

/*! Shuts down platform layer (ends main loop). */
void PL_ShutDown(void)
{
    platform.running = 0;
}

/*! Tells if \p serial belongs to a ConBee I. The ConBee III has the same USB IDs and must never be pulsed. */
static int plIsConBee1(const char *serial)
{
    static Device devs[16];
    U_SStream ss;
    unsigned len;
    unsigned i;

    len = U_strlen(serial);
    if (len == 0)
        return 0;

    PL_GetDevices(&devs[0], sizeof(devs) / sizeof(devs[0]));

    for (i = 0; i < sizeof(devs) / sizeof(devs[0]); i++)
    {
        if (U_strlen(devs[i].serial) != len)
            continue;

        U_sstream_init(&ss, devs[i].serial, len);
        if (U_sstream_starts_with(&ss, serial) == 0)
            continue;

        U_sstream_init(&ss, devs[i].name, U_strlen(devs[i].name));
        return U_sstream_starts_with(&ss, "Serial FTDI") != 0;
    }

    return 0;
}

static int plSetBitMode(HANDLE dev, unsigned char mask, unsigned char mode)
{
    unsigned char request[2];
    DWORD returned;

    request[0] = mask;
    request[1] = mode;
    returned = 0;

    return DeviceIoControl(dev, FTDIBUS_IOCTL_SET_BITMODE, &request[0], sizeof(request), NULL, 0, &returned, NULL) != 0;
}

/*! Executes a MCU reset for ConBee I via FTDI CBUS0, which is wired to the MCU reset pin.

    This talks to FTDI's bus driver, which Windows has bound to the device, so no
    vendor library is needed. Its handle is exclusive, so the COM port is closed
    around the pulse. The driver's interface is undocumented and this only has been
    tested with driver version 2.12.36.20; any other outcome is a failed reset.

    \param serial USB serial number of the device, no other device is touched.

    \returns PL_RESET_PORT_REOPENED or 0 on success, or -1 on failure.
*/
int PL_ResetFTDI(int num, const char *serialnum)
{
    static const unsigned char masks[3] = { 0xf1, 0xf0, 0xf1 }; /* high nibble: direction, low nibble: level */
    char path[128];
    HANDLE dev;
    U_SStream ss;
    unsigned i;
    int connected;
    int result;

    (void)num;

    if (plIsConBee1(serialnum) == 0)
        return -1;

    U_sstream_init(&ss, path, sizeof(path));
    U_sstream_put_str(&ss, "\\\\?\\usb#vid_0403&pid_6015#");
    U_sstream_put_str(&ss, serialnum);
    U_sstream_put_str(&ss, "#{219d0508-57a8-4ff5-97a1-bd86587c6c7e}");
    if (ss.status != U_SSTREAM_OK)
        return -1;

    /* not PL_Disconnect(), this is no disconnect for the state machine */
    connected = platform.fd != INVALID_HANDLE_VALUE;
    if (connected)
    {
        platform.txpos = 0;
        CloseHandle(platform.fd);
        platform.fd = INVALID_HANDLE_VALUE;
    }

    result = -1;
    dev = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (dev != INVALID_HANDLE_VALUE)
    {
        result = 0;

        for (i = 0; i < sizeof(masks); i++)
        {
            if (plSetBitMode(dev, masks[i], FTDI_BITMODE_CBUS) == 0)
            {
                result = -1;
                break;
            }
            if (i + 1 < sizeof(masks))
                PL_MSleep(10);
        }

        /* always give the CBUS pins back, also after a failed request */
        if (plSetBitMode(dev, 0, FTDI_BITMODE_RESET) == 0)
            result = -1;

        CloseHandle(dev);
    }
    else
    {
        PL_Printf(DBG_DEBUG, "failed to open %s, error %lu\n", path, (unsigned long)GetLastError());
    }

    if (connected)
    {
        if (PL_Connect(platform.fdPath, platform.fdBaudrate) != GCF_SUCCESS)
            return -1;

        if (result == 0)
            return PL_RESET_PORT_REOPENED;
    }

    return result;
}

int PL_ResetRaspBee(void)
{
    return -1;
}

int PL_ReadFile(const char *path, unsigned char *buf, unsigned long buflen)
{
    HANDLE hFile;
    int result = -1;
    DWORD nread = 0;

    hFile = CreateFile(path,
                       GENERIC_READ,
                       FILE_SHARE_READ,
                       NULL,                  // default security
                       OPEN_EXISTING,         // existing file only
                       FILE_ATTRIBUTE_NORMAL, // normal file
                       NULL);                 // no attr. template
 
    if (hFile == INVALID_HANDLE_VALUE) 
    { 
        return result; 
    }

    if (ReadFile(hFile, buf, (DWORD)buflen, &nread, NULL))
    {
        if (nread > 0)
        {
            result = (int)nread;
        }
    }

    CloseHandle(hFile);

    return result;
}


void PL_Print(const char *line)
{
    DWORD nchars;

    if (platform.hOut == INVALID_HANDLE_VALUE)
        return;

    for (nchars = 0; line[nchars]; nchars++)
    { }

    if (nchars)
    {
        WriteConsoleA(platform.hOut, line, nchars, NULL, NULL);
    }
}

/* TODO(mpi) This function doesn't depend on platform anymore, and
 *           should be moved into gcf.c so that platform layers only
 *           need to provide PL_Print()
 */
void PL_Printf(DebugLevel level, const char *format, ...)
{
#ifdef NDEBUG
    if (level == DBG_DEBUG)
    {
        return;
    }
#else
    (void)level;
#endif

    static char buf[1024];
    U_SStream ss;
    const char *fmt;
    const char *xx;
    long vlong;
    unsigned long vulong;
    va_list args;

    va_start (args, format);

    xx = 0;
    fmt = format;
    U_sstream_init(&ss, buf, sizeof(buf));

    for (; *fmt && ss.pos + 1 < ss.len; fmt++)
    {
        if (xx)
        {
            if (*fmt == 's')
            {
                xx = (const char*)va_arg(args, const char*);
                U_sstream_put_str(&ss, xx);
                xx = 0;
            }
            else if (*fmt == 'x' || *fmt == 'X')
            {
                vulong = (unsigned long)va_arg(args, unsigned int);
                U_sstream_put_u32hex(&ss, vulong);
                xx = 0;
            }
            else if (*fmt == 'd')
            {
                if (fmt[-1] == 'l') vlong = (long)va_arg(args, long);
                else                vlong = (long)va_arg(args, int);
                U_sstream_put_long(&ss, vlong);
                xx = 0;
            }
            else if (*fmt == 'u')
            {
                if (fmt[-1] == 'l') vulong = (unsigned long)va_arg(args, unsigned long);
                else                vulong = (unsigned long)va_arg(args, unsigned int);
                U_sstream_put_long(&ss, (long)vulong);
                xx = 0;
            }
            else if (*fmt >= '0' && *fmt <= '9')
            {
            }
            else if (*fmt == 'l')
            {
            }
            else if (*fmt == '%')
            {
                U_sstream_put_str(&ss, "%");
                xx = 0;
            }
            else
            {
                /* unknown format specifier */
                U_sstream_put_str(&ss, "??");
                xx = 0;
            }
        }
        else if (*fmt == '%')
        {
            xx = fmt + 1;
        }
        else
        {
            ss.str[ss.pos++] = *fmt;
        }
    }

    ss.str[ss.pos] = '\0';

    va_end (args);

    if (ss.pos)
    {
        PL_Print(buf);
    }
}

void UI_GetWinSize(unsigned *w, unsigned *h)
{
    // TODO
    *w = 80;
    *h = 60;
}

void UI_SetCursor(unsigned x, unsigned y)
{
    (void)x;
    (void)y;
}


int PROT_Write(const unsigned char *data, unsigned len)
{
    if (len == 0)
        return 0;

    Assert(platform.fd != INVALID_HANDLE_VALUE);

    BOOL Status;
    DWORD BytesWritten = 0;          // No of bytes written to the port

    //Writing data to Serial Port
    Status = WriteFile(platform.fd,// Handle to the Serialport
                       data,            // Data to be written to the port
                       len,   // No of bytes to write into the port
                       &BytesWritten,  // No of bytes written to the port
                       NULL);
    if (Status == FALSE)
    {
        DWORD dw = GetLastError();
        PL_Printf(DBG_DEBUG, "failed write com port, error: 0%08X\n", dw);
        return 0;
    }

    if (BytesWritten != (int)len)
    {
        PL_Printf(DBG_DEBUG, "failed write of %u bytes (%d written)\n", len, (int)BytesWritten);
    }
    else
    {
        gcfDebugHex(platform.gcf, "send", data, len);
    }

    return (int)BytesWritten;
}

int PROT_Putc(unsigned char ch)
{
    Assert(platform.txpos + 1 < sizeof(platform.txbuf));
    if (platform.txpos + 1 < sizeof(platform.txbuf))
    {
        platform.txbuf[platform.txpos] = ch;
        platform.txpos += 1;
        return 1;
    }
    return 0;
}

int PROT_Flush(void)
{
    int result = 0;

    if (platform.txpos != 0 && platform.txpos < sizeof(platform.txbuf))
    {
        result = PROT_Write(&platform.txbuf[0], (unsigned)platform.txpos);
        Assert(result == (int)platform.txpos); /* support/handle partial writes? */
        platform.txpos = 0;
    }

    return result;
}

static void plInitOutput(void)
{
    platform.hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    if (platform.hOut == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD dwMode = 0;
    if (!GetConsoleMode(platform.hOut, &dwMode))
    {
        return;
    }

    /*
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(platform.hOut, dwMode))
    {
        return;
    }
    */
}

static volatile int gcfStopRequested = 0;

static BOOL WINAPI plConsoleCtrlHandler(DWORD ctrlType)
{
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT)
    {
        gcfStopRequested = 1; /* handled in PL_Loop: clean sniffer stop */
        return TRUE;
    }

    return FALSE;
}

static void PL_Loop(GCF *gcf)
{
    ZeroMemory(&platform, sizeof(platform));
    platform.gcf = gcf;
    platform.fd = INVALID_HANDLE_VALUE;

    plInitOutput();

    platform.running = 1;
    platform.frequencyValid = QueryPerformanceFrequency(&platform.frequency);

    SetConsoleCtrlHandler(plConsoleCtrlHandler, TRUE);

    GCF_HandleEvent(gcf, EV_PL_STARTED);

    BOOL Status;
    while (platform.running)
    {
        if (gcfStopRequested)
        {
            gcfStopRequested = 0;
#ifdef USE_SNIFF
            if (GCF_SniffActive(gcf))
            {
                GCF_SnifferStop(gcf);
                Sleep(100); /* let the device process stop + online */
            }
#endif /* USE_SNIFF */
            break;
        }

        GCF_HandleEvent(gcf, EV_PL_LOOP);

        if (platform.fd == INVALID_HANDLE_VALUE)
        {
            Sleep(20);

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

        DWORD NoBytesRead;
        Status = ReadFile(platform.fd, &platform.rxbuf, sizeof(platform.rxbuf), &NoBytesRead, NULL);

        if (Status == FALSE)
        {
            PL_Disconnect();
            continue;
        }
        else if (NoBytesRead > 0)
        {
            GCF_Received(gcf, platform.rxbuf, (int)NoBytesRead);
        }
        else if (NoBytesRead == 0)
        {
            if (platform.timer != 0)
            {
                if (platform.timer < PL_Time())
                {
                    platform.timer = 0;
                    GCF_HandleEvent(gcf, EV_TIMEOUT);
                }
            }
            else
            {
                Sleep(4);
            }
        }
    }
}

static int PL_Main(int argc, char **argv)
{
    GCF *gcf = GCF_Init(argc, argv);
    if (gcf == NULL)
        return 2;

    PL_Loop(gcf);

    GCF_Exit(gcf);

    return 0;
}

#define MAX_CMDLINE_ARGS 16
#define MAX_CMDLINE_LEN 512

void mainCRTStartup(void)
{
    int i;
    int argc;

    unsigned cmdllen = 0;
    static char *argv[MAX_CMDLINE_ARGS];
    static char cmdlbuf[MAX_CMDLINE_LEN];

    {
        LPSTR pcmd = GetCommandLineA();
        cmdllen = 0;

        if (pcmd)
        {
            for (i = 0; pcmd[i] && (i + 1) < MAX_CMDLINE_LEN; i++)
            {
                cmdlbuf[i] = pcmd[i];
            }
            cmdlbuf[i] = '\0';
            cmdllen = i;
        }
    }

    argc = 0;

    if (cmdllen)
    {
        int quote;
        U_SStream ss;
        U_sstream_init(&ss, cmdlbuf, cmdllen);

        quote = 0;
        for (;argc < MAX_CMDLINE_ARGS && ss.pos < ss.len; argc++)
        {
            argv[argc] = &ss.str[ss.pos];
            for (;ss.pos < ss.len; ss.pos++)
            {
                if (ss.str[ss.pos] == '"')
                {
                    quote = quote ? 0 : 1;
                }
                if (ss.str[ss.pos] == ' ' && quote == 0)
                {
                    ss.str[ss.pos] = '\0';
                    ss.pos++;
                    break;
                }
            }
        }
    }

    int ret = PL_Main(argc, argv);
    ExitProcess(ret);
}
