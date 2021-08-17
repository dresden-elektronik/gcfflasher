/*
 * Copyright (c) 2021 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include <stdint.h>

typedef enum
{
    EV_ACTION,
    EV_RESET_SUCCESS,
    EV_RESET_FAILED,
    EV_UART_RESET_SUCCESS,
    EV_UART_RESET_FAILED,
    EV_FTDI_RESET_SUCCESS,
    EV_FTDI_RESET_FAILED,
    EV_RASPBEE_RESET_SUCCESS,
    EV_RASPBEE_RESET_FAILED,
    EV_PKG_UART_RESET,
    EV_PL_STARTED,
    EV_RX_ASCII,
    EV_RX_BTL_PKG_DATA,
    EV_CONNECTED,
    EV_DISCONNECTED,
    EV_TIMEOUT
} Event;

typedef enum
{
    GCF_SUCCESS,
    GCF_FAILED
} GCF_Status;

typedef struct GCF_t GCF;
typedef struct GCF_File_t GCF_File;

#define Assert(e) assert(e)

GCF *GCF_Init(int argc, char *argv[]);
void GCF_Exit(GCF *gcf);

/*! Called from platform layer when \p data has been received, \p len must be > 0. */
void GCF_Received(GCF *gcf, const uint8_t *data, int len);
void GCF_HandleEvent(GCF *gcf, Event event);

int GCF_ParseFile(GCF_File *file);

/* Platform specific declarations.

   The functions prefixed with PL_ need to be implemented for a target platform.
 */

void *PL_Malloc(unsigned size);
void PL_Free(void *p);

/*! Returns a monotonic time in milliseconds. */
uint64_t PL_Time();

/*! Lets the programm sleep for \p ms milliseconds. */
void PL_MSleep(uint64_t ms);


/*! Sets a timeout \p ms in milliseconds, after which a \c EV_TIMOUT event is generated. */
void PL_SetTimeout(uint64_t ms);

/*! Clears an active timeout. */
void PL_ClearTimeout();

#define MAX_DEV_NAME_LENGTH 16
#define MAX_DEV_SERIALNR_LENGTH 16
#define MAX_DEV_PATH_LENGTH 255
#define MAX_GCF_FILE_SIZE (1024 * 256) // 250K

typedef struct
{
    char name[MAX_DEV_NAME_LENGTH];
    char path[MAX_DEV_PATH_LENGTH];
    char serial[MAX_DEV_SERIALNR_LENGTH];
    char stablepath[MAX_DEV_PATH_LENGTH];
} Device;

/* Fills up to \p max devices in the \p devs array.

   The output is used in list operation (-l).
*/
int PL_GetDevices(Device *devs, size_t max);

/*! Opens the serial port connection for device.

    \param path - The path like /dev/ttyACM0 or COM7.
    \returns GCF_SUCCESS or GCF_FAILED
 */
GCF_Status PL_Connect(const char *path);

/*! Closed the serial port connection. */
void PL_Disconnect();

/*! Shuts down platform layer (ends main loop). */
void PL_ShutDown();

/*! Executes a MCU reset for ConBee I via FTDI CBUS0 reset. */
int PL_ResetFTDI(int num);

/*! Executes a MCU reset for RaspBee I / II via GPIO17 reset pin. */
int PL_ResetRaspBee();

int PL_ReadFile(const char *path, uint8_t *buf, size_t buflen);


/* Terminal printing and logging */

typedef enum
{
    DBG_INFO  = 0x0001,
    DBG_DEBUG = 0x0002,
    DBG_RAW   = 0x0004
} DebugLevel;

#define FMT_GREEN "\x1b[32m"
#define FMT_RESET "\x1b[0m"

void PL_Printf(DebugLevel level, const char *format, ...);
