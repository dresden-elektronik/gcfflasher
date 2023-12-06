/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

/* This file implements the platform independend part of GCFFlasher.
 */

#ifndef APP_VERSION /* provided by CMakeLists.txt */
#define APP_VERSION "v0.0.0-beta"
#endif

#include <stdio.h>
#include <stdarg.h> /* va_list, ... */
#include "u_sstream.h"
#include "u_strlen.h"
#include "u_mem.h"
#include "buffer_helper.h"
#include "gcf.h"
#include "protocol.h"

#define UI_MAX_LINE_LENGTH 255
#define UI_MAX_LINES 32

#define MAX_DEVICES 4

#define GCF_HEADER_SIZE 14
#define GCF_MAGIC 0xCAFEFEED

#define FW_VERSION_PLATFORM_MASK 0x0000FF00
#define FW_VERSION_PLATFORM_R21  0x00000700 /* 0x26120700*/
#define FW_VERSION_PLATFORM_AVR  0x00000500 /* 0x26390500*/

/* Bootloader V3.x serial protocol */
#define BTL_MAGIC              0x81
#define BTL_ID_REQUEST         0x02
#define BTL_ID_RESPONSE        0x82
#define BTL_FW_UPDATE_REQUEST  0x03
#define BTL_FW_UPDATE_RESPONSE 0x83
#define BTL_FW_DATA_REQUEST    0x04
#define BTL_FW_DATA_RESPONSE   0x84

/* Bootloader V1 */
#define V1_PAGESIZE 256

typedef void (*state_handler_t)(GCF*, Event);

typedef enum
{
    T_NONE,
    T_RESET,
    T_PROGRAM,
    T_LIST,
    T_CONNECT,
    T_HELP
} Task;

typedef enum
{
    DEV_UNKNOWN,
    DEV_RASPBEE_1,
    DEV_RASPBEE_2,
    DEV_CONBEE_1,
    DEV_CONBEE_2,
    DEV_HIVE
} DeviceType;

typedef struct GCF_File_t
{
    char fname[MAX_DEV_PATH_LENGTH];
    unsigned long fsize;

    unsigned long fwVersion; /* taken from file name */

    /* parsed GCF file header */
    unsigned char gcfFileType;
    unsigned long gcfTargetAddress;
    unsigned long gcfFileSize;
    unsigned char gcfCrc;

    unsigned char fcontent[MAX_GCF_FILE_SIZE];
} GCF_File;

typedef struct UI_Line
{
    unsigned length;
    char buf[UI_MAX_LINE_LENGTH];
} UI_Line;

/* The GCF struct holds the complete state as well as GCF file data. */
typedef struct GCF_t
{
    int argc;
    char **argv;
    unsigned wp;     /* ascii[] write pointer */
    char ascii[512]; /* buffer for raw data */
    state_handler_t state;
    state_handler_t substate;

    /* UI line buffering */
    unsigned uiCurrentLine;
    UI_Line uiLines[UI_MAX_LINES];

    int retry;

    unsigned remaining; /* remaining bytes during upload */

    Task task;

    PROT_RxState rxstate;

    PL_time_t startTime;
    PL_time_t maxTime;

    unsigned devCount;
    Device devices[MAX_DEVICES];

    DeviceType devType;

    PL_Baudrate devBaudrate;
    char devpath[MAX_DEV_PATH_LENGTH];
    char devSerialNum[MAX_DEV_SERIALNR_LENGTH];
    GCF_File file;
} GCF;


static DeviceType gcfGetDeviceType(GCF *gcf);
static void gcfRetry(GCF *gcf);
static void gcfPrintHelp();
static GCF_Status gcfProcessCommandline(GCF *gcf);
static void gcfGetDevices(GCF *gcf);
static void gcfCommandResetUart();
static void gcfCommandQueryStatus();
static void gcfCommandQueryFirmwareVersion();
static void ST_Void(GCF *gcf, Event event);
static void ST_Init(GCF *gcf, Event event);

static void ST_Program(GCF *gcf, Event event);
static void ST_V1ProgramSync(GCF *gcf, Event event);
static void ST_V1ProgramWriteHeader(GCF *gcf, Event event);
static void ST_V1ProgramUpload(GCF *gcf, Event event);
static void ST_V1ProgramValidate(GCF *gcf, Event event);

static void ST_V3ProgramSync(GCF *gcf, Event event);
static void ST_V3ProgramUpload(GCF *gcf, Event event);

static void ST_BootloaderConnect(GCF *gcf, Event event);
static void ST_BootloaderQuery(GCF *gcf, Event event);

static void ST_Connect(GCF *gcf, Event event);
static void ST_Connected(GCF *gcf, Event event);

static void ST_Reset(GCF *gcf, Event event);
static void ST_ResetUart(GCF *gcf, Event event);
static void ST_ResetFtdi(GCF *gcf, Event event);
static void ST_ResetRaspBee(GCF *gcf, Event event);

static void ST_ListDevices(GCF *gcf, Event event);

static UI_Line *UI_NextLine(GCF *gcf);
void UI_Printf(GCF *gcf, const char *format, ...);

static GCF gcfLocal;


static const char hex_lookup[16] =
{
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'A', 'B', 'C', 'D', 'E', 'F',
};

void put_hex(unsigned char ch, char *buf)
{
    buf[0] = hex_lookup[(ch >> 4) & 0xF];
    buf[1] = hex_lookup[(ch & 0x0F)];
}

static UI_Line *UI_NextLine(GCF *gcf)
{
    UI_Line *line;

    gcf->uiCurrentLine = (gcf->uiCurrentLine + 1) % UI_MAX_LINES;
    line = &gcf->uiLines[gcf->uiCurrentLine];

    line->buf[0] = '\0';
    line->length = 0;

    return line;
}

void UI_Printf(GCF *gcf, const char *format, ...)
{
    int sz;
    UI_Line *line;
    va_list args;

    line = UI_NextLine(gcf);
    va_start (args, format);
    sz = vsnprintf(&line->buf[0], sizeof(line->buf), format, args);
    if (sz < 0 || sz > (int)sizeof(line->buf))
    {
        line->buf[0] = '\0';
        line->length = 0;
    }
    else
    {
        line->length = (unsigned)sz;
    }
    va_end (args);

    if (line->length > 0)
    {
        //PL_Printf(DBG_INFO, "UI [%2u]: %s", gcf->uiCurrentLine, line->buf);
        PL_Print(line->buf);
    }
}

#ifdef PL_NO_UTF8
  #define FMT_BLOCK_OPEN "."
  #define FMT_BLOCK_DONE "#"
#else
  #define FMT_BLOCK_OPEN "\xE2\x96\x91" /* Light Shade U+2591 */
  #define FMT_BLOCK_DONE "\xE2\x96\x93" /* Dark Shade U+2591 */
#endif

static void UI_UpdateProgress(GCF *gcf)
{
    long percent;
    int ndone;
    unsigned i;
    unsigned w;
    unsigned h;
    unsigned wmax;
    unsigned long total;
    char buf[256];

    U_SStream ss;

    U_sstream_init(&ss, &buf[0], sizeof(buf));

    total = gcf->file.gcfFileSize;

    UI_GetWinSize(&w, &h);

    wmax = w - 2 <= 80 ? w : 80; // cap line length
    percent = (total - gcf->remaining) * 100 / total;

    if (percent > 95)
        percent = 100;

    U_sstream_put_str(&ss, "\r ");

    /* ' 100 % '   right align percent number */
    if      (percent < 10) { U_sstream_put_str(&ss, "  "); }
    else if (percent < 100) {U_sstream_put_str(&ss, " "); }

    U_sstream_put_long(&ss, percent);
    U_sstream_put_str(&ss, "% uploading ");

    w = wmax - ss.pos - 2;
    ndone = (total - gcf->remaining) * w / total;

    for (i = 0; i < w; i++)
    {
        if ((int)i <= ndone)
            U_sstream_put_str(&ss, FMT_BLOCK_DONE);
        else
            U_sstream_put_str(&ss, FMT_BLOCK_OPEN);
    }

    for (; ss.pos < wmax;)
        U_sstream_put_str(&ss, " ");

    UI_SetCursor(0, h - 1);
    PL_Print(&buf[0]);
}

static void ST_Void(GCF *gcf, Event event)
{
    (void)gcf;
    (void)event;
}

static void ST_Init(GCF *gcf, Event event)
{
    if (event == EV_PL_STARTED || event == EV_TIMEOUT)
    {
        if (gcfProcessCommandline(gcf) == GCF_FAILED)
        {
            PL_ShutDown();
        }
        else
        {
            GCF_HandleEvent(gcf, EV_ACTION);
        }
    }
}

static void ST_Reset(GCF *gcf, Event event)
{
    if (event == EV_ACTION)
    {
        gcf->wp = 0;
        gcf->substate = ST_ResetUart;
        gcf->substate(gcf, EV_ACTION);
    }
    else if (event == EV_UART_RESET_SUCCESS || event == EV_FTDI_RESET_SUCCESS || event == EV_RASPBEE_RESET_SUCCESS)
    {
        gcf->substate = ST_Void;

        if (gcf->task == T_RESET)
        {
            PL_ShutDown();
        }
        else if (gcf->task == T_PROGRAM)
        {
            gcf->state = ST_Program;
            GCF_HandleEvent(gcf, EV_RESET_SUCCESS);
        }
    }
    else if (event == EV_UART_RESET_FAILED)
    {
        if (gcf->devType == DEV_CONBEE_1)
        {
            if (PL_Connect(gcf->devpath, gcf->devBaudrate) == GCF_SUCCESS)
            {
                gcf->substate = ST_ResetFtdi;
                gcf->substate(gcf, EV_ACTION);
                return;
            }
        }
        else if (gcf->devType == DEV_RASPBEE_1 || gcf->devType == DEV_RASPBEE_2)
        {
            if (PL_Connect(gcf->devpath, gcf->devBaudrate) == GCF_SUCCESS)
            {
                gcf->substate = ST_ResetRaspBee;
                gcf->substate(gcf, EV_ACTION);
                return;
            }
        }

        /* pretent it worked and jump to bootloader detection */
        PL_SetTimeout(500); /* for connect bootloader */
        GCF_HandleEvent(gcf, EV_UART_RESET_SUCCESS);
    }
    else if (event == EV_FTDI_RESET_FAILED)
    {
        /* pretent it worked and jump to bootloader detection */
        PL_SetTimeout(1); /* for connect bootloader */
        GCF_HandleEvent(gcf, EV_FTDI_RESET_SUCCESS);
    }
    else if (event == EV_RASPBEE_RESET_FAILED)
    {
        /* pretent it worked and jump to bootloader detection */
        PL_SetTimeout(1); /* for connect bootloader */
        GCF_HandleEvent(gcf, EV_RASPBEE_RESET_SUCCESS);
    }
    else
    {
        gcf->substate(gcf, event);
    }
}

static void ST_ResetUart(GCF *gcf, Event event)
{
    if (event == EV_ACTION)
    {
        PL_SetTimeout(3000);

        if (PL_Connect(gcf->devpath, gcf->devBaudrate) == GCF_SUCCESS)
        {
            if (gcf->task == T_RESET)
                gcfCommandQueryFirmwareVersion();
            gcfCommandResetUart();
        }
    }
    else if (event == EV_RX_BTL_PKG_DATA)
    {
        if ((unsigned char)gcf->ascii[1] == BTL_ID_RESPONSE)
        {
            PL_ClearTimeout();
            PL_SetTimeout(100); /* for connect bootloader */
            GCF_HandleEvent(gcf, EV_UART_RESET_SUCCESS);
        }
    }
    else if (event == EV_DISCONNECTED)
    {
        PL_ClearTimeout();
        PL_SetTimeout(500); /* for connect bootloader */
        GCF_HandleEvent(gcf, EV_UART_RESET_SUCCESS);
    }
    else if (event == EV_PKG_UART_RESET)
    {
        UI_Printf(gcf, "command UART reset done\n");
        if (gcf->devType == DEV_RASPBEE_1 || gcf->devType == DEV_CONBEE_1)
        {
            /* due FTDI don't wait for disconnect */
            PL_ClearTimeout();
            GCF_HandleEvent(gcf, EV_UART_RESET_SUCCESS);
        }
    }
    else if (event == EV_TIMEOUT)
    {
        UI_Printf(gcf, "command reset timeout\n");
        gcf->substate = ST_Void;
        PL_Disconnect();
        GCF_HandleEvent(gcf, EV_UART_RESET_FAILED);
    }
}

/*! FTDI reset applies only to ConBee I */
static void ST_ResetFtdi(GCF *gcf, Event event)
{
    if (event == EV_ACTION)
    {
        if (PL_ResetFTDI(0, &gcf->devSerialNum[0]) == 0)
        {
            UI_Printf(gcf, "FTDI reset done\n");
            GCF_HandleEvent(gcf, EV_FTDI_RESET_SUCCESS);
        }
        else
        {
            UI_Printf(gcf,  "FTDI reset failed\n");
            GCF_HandleEvent(gcf, EV_FTDI_RESET_FAILED);
        }
    }
}

/*! RaspBee reset applies only to RaspBee I & II */
static void ST_ResetRaspBee(GCF *gcf, Event event)
{
    if (event == EV_ACTION)
    {
        if (PL_ResetRaspBee() == 0)
        {
            UI_Printf(gcf, "RaspBee reset done\n");
            GCF_HandleEvent(gcf, EV_RASPBEE_RESET_SUCCESS);
        }
        else
        {
            UI_Printf(gcf, "RaspBee reset failed\n");
            GCF_HandleEvent(gcf, EV_RASPBEE_RESET_FAILED);
        }
    }
}

static void gcfGetDevices(GCF *gcf)
{
    int i;
    int n;
    U_SStream ss;
    n = PL_GetDevices(&gcf->devices[0], MAX_DEVICES);
    gcf->devCount = n > 0 ? (unsigned)n : 0;

    if (gcf->devpath[0] != '\0' && gcf->devSerialNum[0] == '\0')
    {
        U_sstream_init(&ss, &gcf->devpath[0], U_strlen(&gcf->devpath[0]));

        for (i = 0; i < n; i++)
        {
            if (gcf->devices[i].serial[0] == '\0')
                continue;

            if (U_sstream_find(&ss, &gcf->devices[i].path[0]) || U_sstream_find(&ss, &gcf->devices[i].stablepath[0]))
            {
                U_memcpy(&gcf->devSerialNum[0], &gcf->devices[i].serial[0], MAX_DEV_SERIALNR_LENGTH);

                if (gcf->devBaudrate == PL_BAUDRATE_UNKNOWN)
                    gcf->devBaudrate = gcf->devices[i].baudrate;

                break;
            }
        }
    }
}

static void ST_ListDevices(GCF *gcf, Event event)
{
    Device *dev;
    unsigned i;

    if (event == EV_ACTION)
    {
        gcfGetDevices(gcf);

        if (gcf->devCount == 0)
        {
            UI_Printf(gcf, "no devices found\n");
        }

        UI_Printf(gcf, "Path              | Serial      | Type\n");
        UI_Printf(gcf, "------------------+-------------+---------------\n");

        for (i = 0; i < gcf->devCount; i++)
        {
            dev = &gcf->devices[i];
            UI_Printf(gcf, "%-18s| %-12s| %s\n", dev->path, dev->serial, dev->name);
        }

        PL_ShutDown();
    }
}

static void ST_Program(GCF *gcf, Event event)
{
    if (event == EV_ACTION)
    {
        gcfGetDevices(gcf);
        UI_Printf(gcf, "flash firmware\n");
        gcf->state = ST_Reset;
        GCF_HandleEvent(gcf, event);
    }
    else if (event == EV_RESET_SUCCESS)
    {
        if (gcf->devType == DEV_RASPBEE_1 || gcf->devType == DEV_CONBEE_1)
        {
            PL_SetTimeout(5000);
            gcf->state = ST_BootloaderQuery; /* wait for bootloader message */
        }
        else
        {
            PL_SetTimeout(500);
            gcf->state = ST_BootloaderConnect;
        }
    }
    else if (event == EV_RESET_FAILED)
    {
        PL_ShutDown();
    }
}

static void ST_BootloaderConnect(GCF *gcf, Event event)
{
    if (event == EV_TIMEOUT)
    {
        if (PL_Connect(gcf->devpath, gcf->devBaudrate) == GCF_SUCCESS)
        {
            gcf->state = ST_BootloaderQuery;
            GCF_HandleEvent(gcf, EV_ACTION);
        }
        else
        {
            // todo retry, a couple of times and revert to gcfRetry()
            PL_SetTimeout(500);
            UI_Printf(gcf, "retry connect bootloader %s\n", gcf->devpath);
        }
    }
    else if (event == EV_RX_ASCII)
    {
        /* short cut if we are already in bootloader */
        PL_ClearTimeout();
        PL_SetTimeout(100); /* for connect bootloader */

        gcf->state = ST_BootloaderQuery;
        gcf->substate = ST_Void;
        GCF_HandleEvent(gcf, EV_RX_ASCII);
    }
}

static void ST_BootloaderQuery(GCF *gcf, Event event)
{
    U_SStream ss;
    unsigned char buf[2];

    if (event == EV_ACTION)
    {
        gcf->retry = 0;
        gcf->wp = 0;
        gcf->ascii[0] = '\0';
        U_bzero(&gcf->ascii[0], sizeof(gcf->ascii));

        /* 1) wait for ConBee I and RaspBee I, which send ID on their own */
        PL_SetTimeout(200);
    }
    else if (event == EV_TIMEOUT)
    {
        if (++gcf->retry == 3)
        {
            UI_Printf(gcf, "query bootloader failed\n");
            gcfRetry(gcf);
        }
        else if (gcf->file.gcfFileType < 30)
        {
            /* 2) V1 Bootloader of ConBee II
                  Query the id here, after initial timeout. This also
                  catches cases where no firmware was installed.
            */
            UI_Printf(gcf, "query bootloader id V1\n");

            buf[0] = 'I';
            buf[1] = 'D';

            PROT_Write(buf, sizeof(buf));
            PL_SetTimeout(200);
        }
        else if (gcf->file.gcfFileType >= 30)
        {
            /* 3) V3 Bootloader of RaspBee II, Hive
                  Query the id here, after initial timeout. This also
                  catches cases where no firmware was installed.
            */
            UI_Printf(gcf, "query bootloader id V3\n");

            buf[0] = BTL_MAGIC;
            buf[1] = BTL_ID_REQUEST;
            PROT_SendFlagged(buf, 2);
            PL_SetTimeout(200);
        }
    }
    else if (event == EV_RX_ASCII)
    {
        if (gcf->wp > 32 && gcf->ascii[gcf->wp - 1] == '\n')
        {
            U_sstream_init(&ss, &gcf->ascii[0], gcf->wp);
            if (U_sstream_find(&ss, "Bootloader"))
            {
                PL_ClearTimeout();
                UI_Printf(gcf, "bootloader detected (%u)\n", gcf->wp);

                gcf->state = ST_V1ProgramSync;
                GCF_HandleEvent(gcf, EV_ACTION);
            }
        }
    }
    else if (event == EV_RX_BTL_PKG_DATA)
    {
        if ((unsigned char)gcf->ascii[1] == BTL_ID_RESPONSE)
        {
            unsigned long btlVersion;
            unsigned long appCrc;

            get_u32_le((unsigned char*)&gcf->ascii[2], &btlVersion);
            get_u32_le((unsigned char*)&gcf->ascii[6], &appCrc);

            UI_Printf(gcf, "bootloader version 0x%08X, app crc 0x%08X\n", btlVersion, appCrc);

            gcf->state = ST_V3ProgramSync;
            GCF_HandleEvent(gcf, EV_ACTION);
        }
    }
    else if (event == EV_DISCONNECTED)
    {
        gcfRetry(gcf);
    }
}

static void ST_V1ProgramSync(GCF *gcf, Event event)
{
    U_SStream ss;
    unsigned char buf[4];

    if (event == EV_ACTION)
    {
        gcf->wp = 0;
        gcf->ascii[0] = '\0';

        buf[0] = 0x1A;
        buf[1] = 0x1C;
        buf[2] = 0xA9;
        buf[3] = 0xAE;

        PROT_Write(buf, sizeof(buf));

        PL_SetTimeout(500);
    }
    else if (event == EV_RX_ASCII)
    {
        U_sstream_init(&ss, &gcf->ascii[0], gcf->wp);
        if (gcf->wp > 4 && U_sstream_find(&ss, "READY"))
        {
            PL_ClearTimeout();
            UI_Printf(gcf, "bootloader synced: %s\n", gcf->ascii);
            gcf->state = ST_V1ProgramWriteHeader;
            GCF_HandleEvent(gcf, EV_ACTION);
        }
        else
        {
            PL_SetTimeout(500);
        }
    }
    else if (event == EV_TIMEOUT)
    {
        UI_Printf(gcf, "failed to sync bootloader (%u) %s\n", gcf->wp, gcf->ascii);
        gcfRetry(gcf);
    }
}

static void ST_V1ProgramWriteHeader(GCF *gcf, Event event)
{
    if (event == EV_ACTION)
    {
        unsigned char *p;
        unsigned char buf[10];

        gcf->wp = 0;
        gcf->ascii[0] = '\0';

        p = buf;
        p = put_u32_le(p, &gcf->file.gcfFileSize);
        p = put_u32_le(p, &gcf->file.gcfTargetAddress);
        *p++ = gcf->file.gcfFileType;
        *p++ = gcf->file.gcfCrc;

        gcf->state = ST_V1ProgramUpload;

        PROT_Write(buf, sizeof(buf));

        PL_SetTimeout(1000);
    }
}

static void ST_V1ProgramUpload(GCF *gcf, Event event)
{
    if (event == EV_RX_ASCII)
    {
        unsigned char *end;
        unsigned char *page;
        unsigned long pageNumber;
        unsigned size;

        /* Firmware GET requests (6 bytes)
           "GET" U16 page ";"
        */
        if (gcf->wp < 6 || gcf->ascii[0] != 'G' || gcf->ascii[5] != ';')
        {
            return;
        }

        pageNumber = (unsigned char)gcf->ascii[4];
        pageNumber <<= 8;
        pageNumber |= (unsigned char)(gcf->ascii[3] & 0xFF);

        page = &gcf->file.fcontent[GCF_HEADER_SIZE] + pageNumber * V1_PAGESIZE;
        end = &gcf->file.fcontent[GCF_HEADER_SIZE + gcf->file.gcfFileSize];

        Assert(page < end);
        if (page >= end)
        {
            gcfRetry(gcf);
        }

        gcf->remaining = (unsigned)(end - page);
        size = gcf->remaining > V1_PAGESIZE ? V1_PAGESIZE : gcf->remaining;

        if (pageNumber % 20 == 0 || gcf->remaining < V1_PAGESIZE)
        {
            // UI_Printf(gcf, "GET 0x%04X (page %u)\n", pageNumber, pageNumber);
            UI_UpdateProgress(gcf);
        }

        gcf->wp = 0;
        gcf->ascii[0] = '\0';

        PROT_Write(page, size);

        if ((gcf->remaining - size) == 0)
        {
            gcf->state = ST_V1ProgramValidate;
            UI_Printf(gcf, "\ndone, wait validation...\n");
            PL_SetTimeout(25600);
        }
        else
        {
            PL_SetTimeout(2000);
        }
    }
    else if (event == EV_TIMEOUT)
    {
        gcfRetry(gcf);
    }
}

static void ST_V1ProgramValidate(GCF *gcf, Event event)
{
    U_SStream ss;

    if (event == EV_RX_ASCII)
    {
        PL_Printf(DBG_DEBUG, "VLD %s (%u)\n", gcf->ascii, gcf->wp);
        U_sstream_init(&ss, &gcf->ascii[0], gcf->wp);

        if (gcf->wp > 6 && U_sstream_find(&ss, "#VALID CRC"))
        {
            UI_Printf(gcf, FMT_GREEN "firmware successful written\n" FMT_RESET, gcf->ascii);
            PL_ShutDown();
        }
        else
        {
            PL_SetTimeout(1000);
        }

    }
    else if (event == EV_TIMEOUT)
    {
        gcfRetry(gcf);
    }
}

static void ST_V3ProgramSync(GCF *gcf, Event event)
{
    if (event == EV_ACTION)
    {
        unsigned char *p;
        unsigned char cmd[] = {
                BTL_MAGIC,
                BTL_FW_UPDATE_REQUEST,
                0x00, 0x0C, 0x00, 0x00, /* data size */
                0x00, 0x00, 0x00, 0x00, /* target address */
                0x00,                   /* file type */
                0xAA, 0xAA, 0xAA, 0xAA  /* crc32 todo */
        };

        PL_MSleep(50);
        PL_SetTimeout(1000);

        p = &cmd[2];

        p = put_u32_le(p, &gcf->file.gcfFileSize);
        p = put_u32_le(p, &gcf->file.gcfTargetAddress);
        p = put_u8_le(p, &gcf->file.gcfFileType);
        (void)p;

        PROT_SendFlagged(cmd, sizeof(cmd));
    }
    else if (event == EV_RX_BTL_PKG_DATA)
    {
        if ((unsigned char)gcf->ascii[1] == BTL_FW_UPDATE_RESPONSE)
        {
            if (gcf->ascii[2] == 0x00) /* success */
            {
                PL_SetTimeout(1000);
                gcf->state = ST_V3ProgramUpload;
            }
        }
    }
    else if (event == EV_TIMEOUT)
    {
        gcfRetry(gcf);
    }
}

static void ST_V3ProgramUpload(GCF *gcf, Event event)
{
    if (event == EV_RX_BTL_PKG_DATA)
    {
        if (gcf->ascii[1] == BTL_FW_DATA_REQUEST && gcf->wp == 8)
        {
            unsigned char *buf;
            unsigned char *p;
            unsigned long offset;
            unsigned short length;
            unsigned char status;

            PL_SetTimeout(5000);

            get_u32_le((unsigned char*)&gcf->ascii[2], &offset);
            get_u16_le((unsigned char*)&gcf->ascii[6], &length);

#ifndef NDEBUG
            UI_Printf(gcf, "BTL data request, offset: 0x%08X, length: %u\n", offset, length);
#endif

            buf = (unsigned char*)&gcf->ascii[0];
            p = buf;

            *p++ = BTL_MAGIC;
            *p++ = BTL_FW_DATA_RESPONSE;

            status = 0; // success
            gcf->remaining = 0;

            if ((offset + length) > gcf->file.gcfFileSize)
            {
                status = 1; /* error */
            }
            else if (length > (sizeof(gcf->ascii) - 32))
            {
                status = 2; /* error */
            }
            else if (length == 0)
            {
                status = 3; /* error */
            }
            else
            {
                Assert(gcf->file.gcfFileSize > offset);
                gcf->remaining = gcf->file.gcfFileSize - offset;
                length = length < gcf->remaining ? length : (unsigned short)gcf->remaining;
                Assert(length > 0);
            }

            p = put_u8_le(p, &status);
            p = put_u32_le(p, &offset);
            p = put_u16_le(p, &length);

            if (status == 0)
            {
                Assert(length > 0);
                U_memcpy(p, &gcf->file.fcontent[GCF_HEADER_SIZE + offset], length);
                p += length;
            }
            else
            {
                UI_Printf(gcf, "failed to handle data request, status: %u\n", status);
            }

            Assert(p > buf);
            Assert(p < buf + sizeof(gcf->ascii));

            PROT_SendFlagged(buf, (unsigned)(p - buf));

            UI_UpdateProgress(gcf);

            if (gcf->remaining == length)
            {
                UI_Printf(gcf, "\nfinished\n");
                PL_SetTimeout(500);
            }
        }
    }
    else if (event == EV_TIMEOUT)
    {
        gcfRetry(gcf);
    }
}

static void ST_Connect(GCF *gcf, Event event)
{
    if (event == EV_ACTION)
    {
        if (PL_Connect(gcf->devpath, gcf->devBaudrate) == GCF_SUCCESS)
        {
            gcf->state = ST_Connected;
            PL_SetTimeout(1000);
        }
        else
        {
            gcf->state = ST_Init;
            UI_Printf(gcf, "failed to connect\n");
            PL_SetTimeout(10000);
        }
    }
}

static void ST_Connected(GCF *gcf, Event event)
{
    if (event == EV_TIMEOUT)
    {
        gcfCommandQueryStatus();
        PL_SetTimeout(10000);
    }
    else if (event == EV_DISCONNECTED)
    {
        PL_ClearTimeout();
        gcf->state = ST_Init;
        UI_Printf(gcf, "disconnected\n");
        PL_SetTimeout(1000);
    }
}

GCF *GCF_Init(int argc, char *argv[])
{
    GCF *gcf;

    gcf = &gcfLocal;

    U_bzero(&gcf->rxstate, sizeof(gcf->rxstate));
    gcf->startTime = PL_Time();
    gcf->maxTime = 0;
    gcf->devCount = 0;
    gcf->task = T_NONE;
    gcf->state = ST_Init;
    gcf->substate = ST_Void;
    gcf->argc = argc;
    gcf->argv = argv;
    gcf->wp = 0;
    gcf->ascii[0] = '\0';

    return gcf;
}

void GCF_Exit(GCF *gcf)
{
    (void)gcf;
}

void GCF_HandleEvent(GCF *gcf, Event event)
{
#if 0
    const char *str;

    if      (gcf->substate == ST_ResetFtdi) str = "ST_ResetFtdi";
    else if (gcf->substate == ST_ResetUart) str = "ST_ResetUart";
    else if (gcf->state == ST_Reset) str = "ST_Reset";
    else if (gcf->state == ST_Program) str = "ST_Program";
    else if (gcf->state == ST_V1ProgramSync) str = "ST_V1ProgramSync";
    else if (gcf->state == ST_V1ProgramUpload) str = "ST_V1ProgramUpload";
    else if (gcf->state == ST_V1ProgramValidate) str = "ST_V1ProgramValidate";
    else if (gcf->state == ST_V1ProgramWriteHeader) str = "ST_V1ProgramWriteHeader";
    else if (gcf->state == ST_Connect) str = "ST_Connect";
    else if (gcf->state == ST_BootloaderQuery) str = "ST_BootloaderQuery";
    else if (gcf->state == ST_BootloaderConnect) str = "ST_BootloaderConnect";
    else                               str = "(unknown)";

    PL_Printf(DBG_DEBUG, "GCF_HandleEvent: state: %s, event: %d\n", str, (int)event);
#endif

    gcf->state(gcf, event);
}

int GCF_ParseFile(GCF_File *file)
{
    unsigned char ch;
    const char *version;
    const unsigned char *p;
    unsigned long magic;

    if (file->fsize < 14)
    {
        return -1;
    }

    Assert(file->fname[0] != '\0');

    file->fwVersion = 0;

    version = &file->fname[0];

    /* parse hex number 0x26780700 */
    for (;*version != '\0';)
    {
        if (version[0] == '0' && version[1] == 'x')
        {
            version += 2;

            for (;*version;)
            {
                ch = (unsigned char)*version;
                version++;
                if      (ch >= 'a' && ch <= 'f') { ch = ch - 'a' + 10; }
                else if (ch >= 'A' && ch <= 'F') { ch = ch - 'A' + 10; }
                else if (ch >= '0' && ch <= '9') { ch = ch - '0'; }
                else    { break; }

                file->fwVersion <<= 4;
                file->fwVersion |= (unsigned char)ch;
                file->fwVersion &= 0xFFFFFFFF;
            }

            break;
        }

        version++;
    }

    /* process GCF header (14-bytes, little-endian)

       U32 magic hex: CA FE FE ED
       U8  file type
       U32 target address
       U32 file size
       U8  checksum (Dallas CRC-8)
    */

    p = file->fcontent;

    p = get_u32_le(p, &magic);
    p = get_u8_le(p, &file->gcfFileType);
    p = get_u32_le(p, &file->gcfTargetAddress);
    p = get_u32_le(p, &file->gcfFileSize);
    get_u8_le(p, &file->gcfCrc);

    PL_Printf(DBG_DEBUG, "GCF header: magic: 0x%08X, type: %u, address: 0x%08X, data.size: 0x%08X\n", magic, file->gcfFileType, file->gcfTargetAddress, file->gcfFileSize);

    if (magic != GCF_MAGIC)
    {
        return -2;
    }

    if (file->gcfFileSize != (file->fsize - GCF_HEADER_SIZE))
    {
        return -3;
    }

    return 0;
}


void GCF_Received(GCF *gcf, const unsigned char *data, int len)
{
    int i;
    unsigned char ch;
    unsigned ascii;

    Assert(len > 0);

    /* gcfDebugHex(gcf, "recv", data, len); */

    if (gcf->state == ST_BootloaderQuery ||
        gcf->state == ST_V1ProgramSync ||
        gcf->state == ST_V1ProgramWriteHeader ||
        gcf->state == ST_V1ProgramUpload ||
        gcf->state == ST_V1ProgramValidate)
    {
        ascii = 0;
        for (i = 0; i < len; i++)
        {
            ch = data[i];

            if (gcf->wp < sizeof(gcf->ascii) - 2)
            {
                gcf->ascii[gcf->wp++] = (char)ch;
                gcf->ascii[gcf->wp] = '\0';
                ascii++;

                /*
                if ((ch >= 0x20 && ch <= 127) || ch == '\n' || ch == '\r')
                {
                    PL_Printf(DBG_DEBUG, "%c", (char)ch);
                }
                else
                {
                    PL_Printf(DBG_DEBUG, ".");
                }
                */
            }
            else
            {
                /* sanity rollback */
                gcf->wp = 0;
                gcf->ascii[gcf->wp] = '\0';
            }
        }

        if (ascii > 0)
        {
            GCF_HandleEvent(gcf, EV_RX_ASCII);
        }
    }

    PROT_ReceiveFlagged(&gcf->rxstate, data, len);
}

void PROT_Packet(const unsigned char *data, unsigned len)
{
    Assert(len > 0);

    int i;
    char *p;
    GCF *gcf = &gcfLocal;


    if (data[0] != BTL_MAGIC && gcf->task == T_CONNECT)
    {
        p = &gcf->ascii[0];
        for (i = 0; i < (int)len; i++, p += 2)
        {
            put_hex(data[i], p);
        }
        *p = '\0';
        UI_Printf(gcf, "packet: %d bytes, %s\n", len, gcf->ascii);
    }
    else
    {
        gcfDebugHex(gcf, "recv_packet", data, len);
    }

    if (data[0] == 0x0B && len >= 8) /* write parameter response */
    {
        switch (data[7])
        {
            case 0x26: /* param: watchdog timeout */
            {
                GCF_HandleEvent(gcf, EV_PKG_UART_RESET);
            } break;

            default:
                break;
        }
    }
    else if (data[0] == BTL_MAGIC)
    {
        if (len < sizeof(gcf->ascii))
        {
            U_memcpy(&gcf->ascii[0], data, len);
            gcf->wp = len;
            GCF_HandleEvent(gcf, EV_RX_BTL_PKG_DATA);
        }
    }
}

static DeviceType gcfGetDeviceType(GCF *gcf)
{
    int ftype;
    U_SStream ss;
    DeviceType result;
    PL_Baudrate baudrate;

    result = DEV_UNKNOWN;
    ftype = gcf->file.gcfFileType;
    baudrate = PL_BAUDRATE_UNKNOWN;

    if (gcf->devpath[0] != '\0')
    {
        U_sstream_init(&ss, &gcf->devpath[0], U_strlen(&gcf->devpath[0]));

        if      (U_sstream_find(&ss, "ttyACM"))        { result = DEV_CONBEE_2; baudrate = PL_BAUDRATE_115200; }
        else if (U_sstream_find(&ss, "ConBee_II"))     { result = DEV_CONBEE_2; baudrate = PL_BAUDRATE_115200; }
        else if (U_sstream_find(&ss, "cu.usbmodemDE")) { result = DEV_CONBEE_2; baudrate = PL_BAUDRATE_115200; }
        else if (U_sstream_find(&ss, "ttyUSB"))        { result = DEV_CONBEE_1; baudrate = PL_BAUDRATE_38400; }
        else if (U_sstream_find(&ss, "usb-FTDI"))      { result = DEV_CONBEE_1; baudrate = PL_BAUDRATE_38400; }
        else if (U_sstream_find(&ss, "cu.usbserial"))  { result = DEV_CONBEE_1; baudrate = PL_BAUDRATE_38400; }
        else if (U_sstream_find(&ss, "ttyAMA"))        { result = DEV_RASPBEE_1; baudrate = PL_BAUDRATE_38400; }
        else if (U_sstream_find(&ss, "ttyAML"))        { result = DEV_RASPBEE_1; baudrate = PL_BAUDRATE_38400; } /* Odroid */
        else if (U_sstream_find(&ss, "ttyS"))          { result = DEV_RASPBEE_1; baudrate = PL_BAUDRATE_38400; }
        else if (U_sstream_find(&ss, "/serial"))       { result = DEV_RASPBEE_1; baudrate = PL_BAUDRATE_38400; }
#ifdef _WIN32
        else if (U_sstream_find(&ss, "COM"))
        {
            if (ftype == 1 && gcf->file.gcfTargetAddress == 0)
            {
                result = DEV_CONBEE_1;
                baudrate = PL_BAUDRATE_38400;
            }
            else if (ftype < 30 && gcf->file.gcfTargetAddress == 0x5000)
            {
                result = DEV_CONBEE_2;
                baudrate = PL_BAUDRATE_115200;
            }
        }
#endif
    }

    /* further detemine detive type from the GCF header */
    if      (ftype == 60) { result = DEV_HIVE; baudrate = PL_BAUDRATE_115200; }
    else if (result == DEV_CONBEE_1 && ftype > 9)                   { result = DEV_UNKNOWN; baudrate = PL_BAUDRATE_38400; }
    else if (result == DEV_RASPBEE_2 && ftype >= 30 && ftype <= 39) { result = DEV_RASPBEE_2; baudrate = PL_BAUDRATE_38400; }

    if (gcf->devBaudrate == PL_BAUDRATE_UNKNOWN)
        gcf->devBaudrate = baudrate;

    return result;
}

static void gcfRetry(GCF *gcf)
{
    PL_time_t now = PL_Time();
    if (gcf->maxTime > now)
    {
        UI_Printf(gcf, "retry: %d seconds left\n", (int)(gcf->maxTime - now) / 1000);

        gcf->state = ST_Init;
        gcf->substate = ST_Void;
        PL_SetTimeout(250);
    }
    else
    {
        PL_ShutDown();
    }
}

static void gcfPrintHelp()
{
    const char *usage =

    "GCFFlasher " APP_VERSION " copyright dresden elektronik ingenieurtechnik gmbh\n"
    "usage: GCFFlasher <options>\n"
    "options:\n"
    " -r              force device reboot without programming\n"
    " -f <firmware>   flash firmware file\n"
#ifdef _WIN32
    " -d <com port>   COM port to use, e.g. COM1\n"
#else
    " -d <device>     device number or path to use, e.g. 0, /dev/ttyUSB0 or RaspBee\n"
#endif
    " -c              connect and debug serial protocol\n"
//    " -s <serial>     serial number to use\n"
    " -t <timeout>    retry until timeout (seconds) is reached\n"
    " -l              list devices\n"
//    " -x <loglevel>   debug log level 0, 1, 3\n"
    " -h -?           print this help\n";


    PL_Print(usage);
}

void gcfDebugHex(GCF *gcf, const char *msg, const unsigned char *data, unsigned size)
{
#ifndef NDEBUG
    char *p;
    char buf[1024];
    unsigned i;

    p = &buf[0];

    Assert(size < (sizeof(buf) / 2) - 1);
    for (i = 0; i < size; i++, p += 2)
    {
        put_hex(data[i], p);
    }
    *p = '\0';

    UI_Printf(gcf, FMT_GREEN "%s:" FMT_RESET " %s (%u bytes)\n", msg, &buf[0], size);
#else
    (void)gcf;
    (void)msg;
    (void)data;
    (void)size;
#endif
}

static GCF_Status gcfProcessCommandline(GCF *gcf)
{
    int i;
    const char *arg;
    unsigned long arglen;
    long longval;
    long nread;
    GCF_Status ret = GCF_FAILED;
    U_SStream ss;

    gcf->state = ST_Void;
    gcf->substate = ST_Void;
    gcf->devpath[0] = '\0';
    gcf->devSerialNum[0] = '\0';
    gcf->devType = DEV_UNKNOWN;
    gcf->devBaudrate = PL_BAUDRATE_UNKNOWN;
    gcf->file.fname[0] = '\0';
    gcf->file.gcfFileType = 0;
    gcf->file.fsize = 0;
    gcf->task = T_NONE;

    if (gcf->argc == 1)
    {
        gcf->task = T_HELP;
    }

    for (i = 1; i < gcf->argc; i++)
    {
        arg = gcf->argv[i];

        if (arg[0] == '-')
        {
            switch (arg[1])
            {
                case 'r':
                {
                    gcf->task = T_RESET;
                } break;

                case 'c':
                {
                    gcf->task = T_CONNECT;
                } break;

                case 'd':
                {
                    if ((i + 1) == gcf->argc || gcf->argv[i + 1][0] == '-')
                    {
                        PL_Printf(DBG_INFO, "missing argument for parameter -d\n");
                        return GCF_FAILED;
                    }

                    i++;
                    arg = gcf->argv[i];

                    arglen = U_strlen(arg);
                    if (arglen >= sizeof(gcf->devpath))
                    {
                        PL_Printf(DBG_INFO, "invalid argument, %s, for parameter -d\n", arg);
                        return GCF_FAILED;
                    }

                    U_memcpy(gcf->devpath, arg, arglen + 1);
                } break;

                case 'f':
                {
                    gcf->task = T_PROGRAM;

                    if ((i + 1) == gcf->argc || gcf->argv[i + 1][0] == '-')
                    {
                        PL_Printf(DBG_INFO, "missing argument for parameter -f\n");
                        return GCF_FAILED;
                    }

                    i++;
                    arg = gcf->argv[i];

                    arglen = U_strlen(arg);
                    if (arglen >= sizeof(gcf->file.fname))
                    {
                        PL_Printf(DBG_INFO, "invalid argument, %s, for parameter -f\n", arg);
                        return GCF_FAILED;
                    }

                    U_memcpy(gcf->file.fname, arg, arglen + 1);
                    nread = (long)PL_ReadFile(gcf->file.fname, gcf->file.fcontent, sizeof(gcf->file.fcontent));
                    if (nread <= 0)
                    {
                        PL_Printf(DBG_INFO, "failed to read file: %s\n", gcf->file.fname);
                        return GCF_FAILED;
                    }

                    PL_Printf(DBG_INFO, "read file success: %s (%ld bytes)\n", gcf->file.fname, nread);
                    gcf->file.fsize = (unsigned long)nread;

                    if (GCF_ParseFile(&gcf->file) != 0)
                    {
                        PL_Printf(DBG_INFO, "invalid file: %s\n", gcf->file.fname);
                        return GCF_FAILED;
                    }
                } break;

                case 'l':
                {
                    gcf->task = T_LIST;
                    gcf->state = ST_ListDevices;
                    ret = GCF_SUCCESS;
                } break;

                case 't':
                {
                    if ((i + 1) == gcf->argc || gcf->argv[i + 1][0] == '-')
                    {
                        PL_Printf(DBG_INFO, "missing argument for parameter -t\n");
                        return GCF_FAILED;
                    }

                    i++;
                    arg = gcf->argv[i];

                    U_sstream_init(&ss, gcf->argv[i], U_strlen(gcf->argv[i]));

                    longval = U_sstream_get_long(&ss); /* seconds */

                    if (ss.status != U_SSTREAM_OK || longval < 0 || longval > 3600)
                    {
                        PL_Printf(DBG_INFO, "invalid argument, %s, for parameter -t\n", arg);
                        return GCF_FAILED;
                    }

                    gcf->maxTime = longval;
                    gcf->maxTime *= 1000;
                    gcf->maxTime += gcf->startTime;

                } break;

                case 'x':
                {
                    if ((i + 1) == gcf->argc || gcf->argv[i + 1][0] == '-')
                    {
                        PL_Printf(DBG_INFO, "missing argument for parameter -x\n");
                        return GCF_FAILED;
                    }

                    i++;
                    /* TODO this is a no-op currently */
                } break;

                case '?':
                case 'h':
                {
                    gcf->task = T_HELP;
                    ret = GCF_SUCCESS;
                } break;

                default:
                {
                    PL_Printf(DBG_INFO, "unknown option: %s\n", arg);
                    ret = GCF_FAILED;

                } return ret;
            }
        }
    }

    gcfGetDevices(gcf);
    gcf->devType = gcfGetDeviceType(gcf);

    if (gcf->task == T_PROGRAM)
    {
        if (gcf->devpath[0] == '\0')
        {
            PL_Printf(DBG_INFO, "missing -d argument\n");
            return GCF_FAILED;
        }

        if (gcf->file.fname[0] == '\0')
        {
            PL_Printf(DBG_INFO, "missing -f argument\n");
            return GCF_FAILED;
        }

        /* if no -t parameter was specified, use 10 seconds retry time */
        if (gcf->maxTime < gcf->startTime)
        {
            gcf->maxTime = 10 * 1000;
            gcf->maxTime += gcf->startTime;
        }

        /* The /dev/ttyACM0 and similar doesn't tell if this is RaspBee II,
           the fwVersion of the file is more specific.
        */
        if (gcf->devType == DEV_RASPBEE_1 &&
            (gcf->file.fwVersion & FW_VERSION_PLATFORM_MASK) == FW_VERSION_PLATFORM_R21)
        {
            PL_Printf(DBG_DEBUG, "assume RaspBee II\n");
            gcf->devType = DEV_RASPBEE_2;
        }
        else if (gcf->devType == DEV_RASPBEE_1 && gcf->file.gcfTargetAddress == 0x5000)
        {
            PL_Printf(DBG_DEBUG, "assume RaspBee II\n");
            gcf->devType = DEV_RASPBEE_2;
        }

        gcf->state = ST_Program;
        ret = GCF_SUCCESS;
    }
    else if (gcf->task == T_CONNECT)
    {
        if (gcf->devpath[0] == '\0')
        {
            PL_Printf(DBG_INFO, "missing -d argument\n");
            return GCF_FAILED;
        }

        gcf->state = ST_Connect;
        ret = GCF_SUCCESS;
    }
    else if (gcf->task == T_RESET)
    {
        if (gcf->devpath[0] == '\0')
        {
            PL_Printf(DBG_INFO, "missing -d argument\n");
            return GCF_FAILED;
        }

        gcf->state = ST_Reset;
        ret = GCF_SUCCESS;
    }
    else if (gcf->task == T_HELP)
    {
        gcfPrintHelp();
        PL_ShutDown();
        ret = GCF_SUCCESS;
    }

    return ret;
}

static void gcfCommandResetUart()
{
    const unsigned char cmd[] = {
        0x0B, // command: write parmater
        0x03, // seq
        0x00, // status
        0x0C, 0x00, // frame length (12)
        0x05, 0x00, // buffer length (5)
        0x26, // param: watchdog timout (2 seconds)
        0x02, 0x00, 0x00, 0x00
    };

    PL_Printf(DBG_DEBUG, "send uart reset\n");

    PROT_SendFlagged(cmd, sizeof(cmd));
}

static void gcfCommandQueryStatus()
{
    static unsigned char seq = 1;

    unsigned char cmd[] = {
        0x07, // command: write parmater
        0x02, // seq
        0x00, // status
        0x08, 0x00, // frame length (12)
        0x00, 0x00, 0x00 // dummy bytes
    };

    cmd[1] = seq++;

    PROT_SendFlagged(cmd, sizeof(cmd));
}

static void gcfCommandQueryFirmwareVersion()
{
    const unsigned char cmd[] = {
        0x0D, // command: write parmater
        0x05, // seq
        0x00, // status
        0x09, 0x00, // frame length (9)
        0x00, 0x00, 0x00, 0x00 // dummy bytes
    };

    PROT_SendFlagged(cmd, sizeof(cmd));
}
