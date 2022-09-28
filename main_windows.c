

#pragma comment(lib, "SetupAPI.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Advapi32.lib")

//#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <initguid.h>
#include <Setupapi.h>
#include <shlwapi.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include "gcf.h"

// https://hero.handmade.network/forums/code-discussion/t/94-guide_-_how_to_avoid_c_c++_runtime_on_windows

typedef struct
{
    uint64_t timer;
    HANDLE fd;
    uint8_t running;
    uint8_t rxbuf[64];

    LARGE_INTEGER frequency;
    BOOL frequencyValid;

    GCF *gcf;
} PL_Internal;

static PL_Internal platform;


static size_t GetComPort(const char *enumerator, Device *devs, size_t max);

void *PL_Malloc(unsigned size)
{
    void *p = VirtualAlloc(0, size, MEM_COMMIT, PAGE_READWRITE);
    return p;
/*    void *p = malloc(size);

    if (p)
    {
        ZeroMemory(p, size);
    }
    return p;
    */
}

void PL_Free(void *p)
{
    if (p)
    {
        VirtualFree(p, 0, MEM_RELEASE);
        //free(p);
    }
}

/*! Returns a monotonic time in milliseconds. */
uint64_t PL_Time()
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
void PL_MSleep(uint64_t ms)
{
    Sleep((DWORD)ms);
}


/*! Sets a timeout \p ms in milliseconds, after which a \c EV_TIMOUT event is generated. */
void PL_SetTimeout(uint64_t ms)
{
    platform.timer = PL_Time() + ms;
}

/*! Clears an active timeout. */
void PL_ClearTimeout()
{
    platform.timer = 0;
}

/* Fills up to \p max devices in the \p devs array.

   The output is used in list operation (-l).
*/
int PL_GetDevices(Device *devs, size_t max)
{
    // http://www.naughter.com/enumser.html

    size_t result = 0;

    result = GetComPort("USB", devs, max);

    if (result < max)
    {
        size_t res2 = GetComPort("FTDIBUS", devs + result, max - result);
        if (res2 + result <= max)
        {
            result += res2;
        }
    }

    return (int)result;
}

static size_t GetComPort(const char *enumerator, Device *devs, size_t max)
{
    size_t devcount = 0;

    if (max == 0)
    {
        return devcount;
    }

    HDEVINFO DeviceInfoSet;
    DWORD DeviceIndex =0;
    SP_DEVINFO_DATA DeviceInfoData;
    //const char *DevEnum = "USB";
    //const char *DevEnum = "FTDIBUS";
    //char ExpectedDeviceId[80]; //Store hardware id
    BYTE szBuffer[1024];
    DEVPROPTYPE ulPropertyType;
    DWORD dwSize = 0;
    DWORD Error = 0;
    
    //create device hardware id
    /*
    strcpy_s(ExpectedDeviceId, sizeof(ExpectedDeviceId), "vid_");
    strcat_s(ExpectedDeviceId, sizeof(ExpectedDeviceId), vid);
    strcat_s(ExpectedDeviceId, sizeof(ExpectedDeviceId), "&pid_");
    strcat_s(ExpectedDeviceId, sizeof(ExpectedDeviceId), pid);
    */
    
    //SetupDiGetClassDevs returns a handle to a device information set
    DeviceInfoSet = SetupDiGetClassDevs(
                        NULL,
                        enumerator,
                        NULL,
                        DIGCF_ALLCLASSES | DIGCF_PRESENT);
    
    if (DeviceInfoSet == INVALID_HANDLE_VALUE)
        return devcount;

    //Fills a block of memory with zeros
    ZeroMemory(&DeviceInfoData, sizeof(SP_DEVINFO_DATA));
    DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    //Receive information about an enumerated device
    while (SetupDiEnumDeviceInfo(
                DeviceInfoSet,
                DeviceIndex,
                &DeviceInfoData) && devcount < max)
    {
        DeviceIndex++;

        //Retrieves a specified Plug and Play device property
        if (SetupDiGetDeviceRegistryProperty (DeviceInfoSet, &DeviceInfoData, SPDRP_HARDWAREID,
                                              &ulPropertyType, (BYTE*)szBuffer,
                                              sizeof(szBuffer),   // The size, in bytes
                                              &dwSize))
        {
            HKEY hDeviceRegistryKey;
            //Get the key
            // HKEY_LOCAL_MACHINE\SYSTEM\ControlSet001\Control\DeviceMigration\Devices\USB

            hDeviceRegistryKey = SetupDiOpenDevRegKey(DeviceInfoSet, &DeviceInfoData,DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
            if (hDeviceRegistryKey == INVALID_HANDLE_VALUE)
            {
                Error = GetLastError();
                break; //Not able to open registry
            }
            else
            {
                // Read in the name of the port
                char pszPortName[20];
                DWORD dwSize = sizeof(pszPortName);
                DWORD dwType = 0;
                if ((RegQueryValueEx(hDeviceRegistryKey, "PortName", NULL, &dwType,
                    (LPBYTE) pszPortName, &dwSize) == ERROR_SUCCESS) && (dwType == REG_SZ))
                {
                    // USB\VID_1CF1&PID_0030&REV_0100 (COM3)
                    

                    // Check if it really is a com port
                    if (_tcsnicmp(pszPortName, "COM", 3) == 0)
                    {
                        int nPortNr = atoi( pszPortName + 3);
                        if (nPortNr != 0)
                        {

                            if (StrStrIA(szBuffer, "VID_1CF1&PID_0030"))
                            {
                                strcpy_s(devs->name, sizeof(devs->name), "ConBee II");                                
                            }
                            else if (StrStrIA(szBuffer, "VID_0403&PID_6015"))
                            {
                                strcpy_s(devs->name, sizeof(devs->name), "ConBee I");                                
                            }
                            else
                            {
                                strcpy_s(devs->name, sizeof(devs->name), enumerator);
                            }

                            dwSize = sizeof(szBuffer);
                            if (RegQueryValueEx(hDeviceRegistryKey, "", NULL, &dwType,
                    (LPBYTE) &szBuffer[0], &dwSize) == ERROR_SUCCESS)
                            {
                                PL_Printf(DBG_DEBUG, "--> %s (%s)\n", szBuffer, pszPortName);
                                strcpy_s(devs->serial, sizeof(devs->serial), szBuffer);
                            }

                            strcpy_s(devs->path, sizeof(devs->path), pszPortName);
                            strcpy_s(devs->stablepath, sizeof(devs->path), pszPortName);

                            devcount++;
                            devs++;
                        }
                    }
                }
                // Close the key now that we are finished with it
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
GCF_Status PL_Connect(const char *path)
{
    char buf[32];

    if (strlen(path) > 7)
    {
        return GCF_FAILED;
    }

    if (*path == 'C')
    {
        snprintf(buf, sizeof(buf), "\\\\.\\%s", path);
    }
    else if (*path == '\\')
    {
        memcpy(buf, path, strlen(path) + 1);
    }
    else
    {
        return GCF_FAILED;
    }

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
        printf("\nError to Get the Com state\n\n");
        goto Exit1;
    }
    dcbSerialParams.BaudRate = CBR_115200;   //BaudRate = 115200
    dcbSerialParams.ByteSize = 8;            //ByteSize = 8
    dcbSerialParams.StopBits = ONESTOPBIT;    //StopBits = 1
    dcbSerialParams.Parity = NOPARITY;      //Parity = None
    dcbSerialParams.fBinary = TRUE;

    Status = SetCommState(platform.fd, &dcbSerialParams);
    if (Status == FALSE)
    {
        printf("\nError to Setting DCB Structure\n\n");
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
        printf("\nError to Setting Time outs");
        goto Exit1;
    }
    
    Status = SetCommMask(platform.fd, EV_RXCHAR);
    if (Status == FALSE)
    {
        printf("\nError to in Setting CommMask\n\n");
        goto Exit1;
    }

    PL_Printf(DBG_DEBUG, "opened com port %s\n", buf);

    return GCF_SUCCESS;

Exit1:
    PL_Disconnect();
    return GCF_FAILED;
}

/*! Closed the serial port connection. */
void PL_Disconnect()
{
    if (platform.fd != INVALID_HANDLE_VALUE)
    {
        CloseHandle(platform.fd);
        platform.fd = INVALID_HANDLE_VALUE;
        GCF_HandleEvent(platform.gcf, EV_DISCONNECTED);
    }
}

/*! Shuts down platform layer (ends main loop). */
void PL_ShutDown()
{
    platform.running = 0;
}

/*! Executes a MCU reset for ConBee I via FTDI CBUS0 reset. */
int PL_ResetFTDI(int num)
{
    return -1;
}

/*! Executes a MCU reset for RaspBee I / II via GPIO17 reset pin. */
int PL_ResetRaspBee()
{
    return -1;
}

int PL_ReadFile(const char *path, uint8_t *buf, size_t buflen)
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
    printf("%s", line);
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

void UI_GetWinSize(uint16_t *w, uint16_t *h)
{
    // TODO
    *w = 80;
    *h = 60;
}

void UI_SetCursor(uint16_t x, uint16_t y)
{

}


int PROT_Write(const uint8_t *data, uint16_t len)
{
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
        return 0;
    }

    return BytesWritten;
}

int PROT_Putc(uint8_t ch)
{
    return PROT_Write(&ch, 1);
}

int PROT_Flush()
{
    return 0;
}

static void PL_Loop(GCF *gcf)
{
    ZeroMemory(&platform, sizeof(platform));
    platform.gcf = gcf;
    platform.fd = INVALID_HANDLE_VALUE;

    platform.running = 1;
    platform.frequencyValid = QueryPerformanceFrequency(&platform.frequency);

    GCF_HandleEvent(gcf, EV_PL_STARTED);

    BOOL Status;
    while (platform.running)
    {
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
            GCF_Received(gcf, platform.rxbuf, NoBytesRead);
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

int main(int argc, char **argv)
{
    GCF *gcf = GCF_Init(argc, argv);
    if (gcf == NULL)
        return 2;

    PL_Loop(gcf);

    GCF_Exit(gcf);

    return 0;
}
