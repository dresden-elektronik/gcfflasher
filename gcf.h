/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef GCF_H
#define GCF_H

typedef enum
{
    EV_ACTION = 0,
    EV_RESET_SUCCESS = 10,
    EV_RESET_FAILED = 20,
    EV_UART_RESET_SUCCESS = 11,
    EV_UART_RESET_FAILED  = 21,
    EV_FTDI_RESET_SUCCESS = 12,
    EV_FTDI_RESET_FAILED  = 22,
    EV_RASPBEE_RESET_SUCCESS = 13,
    EV_RASPBEE_RESET_FAILED = 23,
    EV_PKG_UART_RESET = 41,
    EV_PL_STARTED = 100,
    EV_RX_ASCII = 50,
    EV_RX_BTL_PKG_DATA = 40,
    EV_CONNECTED = 200,
    EV_DISCONNECTED = 203,
    EV_TIMEOUT = 333
} Event;

typedef enum
{
    GCF_SUCCESS,
    GCF_FAILED
} GCF_Status;

typedef enum
{
    PL_BAUDRATE_UNKNOWN = 0,
    PL_BAUDRATE_38400 = 38400,
    PL_BAUDRATE_115200 = 115200
} PL_Baudrate;

typedef struct GCF_t GCF;
typedef struct GCF_File_t GCF_File;

/* TODO detect old 32-bit only compilers */
typedef unsigned long long PL_time_t;


#ifdef NDEBUG
  #define Assert(c) ((void)0)
#else
  #if _MSC_VER
    #define Assert(c) if (!(c)) __debugbreak()
  #elif __GNUC__
    #define Assert(c) if (!(c)) __builtin_trap()
  #else
    #define Assert(c) ((void)0)
  #endif
#endif /* NDEBUG */

GCF *GCF_Init(int argc, char *argv[]);
void GCF_Exit(GCF *gcf);

/*! Called from platform layer when \p data has been received, \p len must be > 0. */
void GCF_Received(GCF *gcf, const unsigned char *data, int len);
void GCF_HandleEvent(GCF *gcf, Event event);

int GCF_ParseFile(GCF_File *file);
void gcfDebugHex(GCF *gcf, const char *msg, const unsigned char *data, unsigned size);
void put_hex(unsigned char ch, char *buf);

/* Platform specific declarations.

   The functions prefixed with PL_ need to be implemented for a target platform.
 */

/*! Returns a monotonic time in milliseconds. */
PL_time_t PL_Time();

/*! Lets the programm sleep for \p ms milliseconds. */
void PL_MSleep(unsigned long ms);


/*! Sets a timeout \p ms in milliseconds, after which a \c EV_TIMOUT event is generated. */
void PL_SetTimeout(unsigned long ms);

/*! Clears an active timeout. */
void PL_ClearTimeout(void);

#define MAX_DEV_NAME_LENGTH 32
#define MAX_DEV_SERIALNR_LENGTH 18
#define MAX_DEV_PATH_LENGTH 255
#define MAX_GCF_FILE_SIZE (1024 * 800) // 800K

typedef struct
{
    PL_Baudrate baudrate;
    char name[MAX_DEV_NAME_LENGTH];
    char path[MAX_DEV_PATH_LENGTH];
    char serial[MAX_DEV_SERIALNR_LENGTH];
    char stablepath[MAX_DEV_PATH_LENGTH];
} Device;

/* Fills up to \p max devices in the \p devs array.

   The output is used in list operation (-l).
*/
int PL_GetDevices(Device *devs, unsigned max);

/*! Opens the serial port connection for device.

    \param path - The path like /dev/ttyACM0 or COM7.
    \returns GCF_SUCCESS or GCF_FAILED
 */
GCF_Status PL_Connect(const char *path, PL_Baudrate baudrate);

/*! Closed the serial port connection. */
void PL_Disconnect();

/*! Shuts down platform layer (ends main loop). */
void PL_ShutDown();

/*! Executes a MCU reset for ConBee I via FTDI CBUS0 reset. */
int PL_ResetFTDI(int num, const char *serialnum);

/*! Executes a MCU reset for RaspBee I / II via GPIO17 reset pin. */
int PL_ResetRaspBee();

int PL_ReadFile(const char *path, unsigned char *buf, unsigned long buflen);


/* Terminal printing and logging */

typedef enum
{
    DBG_INFO  = 0x0001,
    DBG_DEBUG = 0x0002,
    DBG_RAW   = 0x0004
} DebugLevel;

/* ASCII escape codes

   https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
*/
#ifdef PL_NO_ESCASCII
  #define FMT_ESC ""
  #define FMT_GREEN FMT_ESC ""
  #define FMT_RESET FMT_ESC ""
#else
  #define FMT_ESC "\x1b"
  #define FMT_GREEN FMT_ESC "[32m"
  #define FMT_RESET FMT_ESC "[0m"
#endif

void PL_Print(const char *line);

void PL_Printf(DebugLevel level, const char *format, ...);

void UI_GetWinSize(unsigned *w, unsigned *h);
void UI_SetCursor(unsigned x, unsigned y);

#endif /* GCF_H */
