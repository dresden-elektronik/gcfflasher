/*
 * Copyright (c) 2021-2025 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

/* This file implements the platform independent part of GCFFlasher.
 */

#ifndef APP_VERSION /* provided by CMakeLists.txt */
#define APP_VERSION "v0.0.0-beta"
#endif

#include "u_sstream.h"
#include "u_bstream.h"
#include "u_strlen.h"
#include "u_mem.h"
#include "buffer_helper.h"
#include "gcf.h"
#include "protocol.h"
#include "net.h"
#include "net_sock.h"

#define UI_MAX_INPUT_LENGTH 1024
#define UI_MAX_LINE_LENGTH 384
#define UI_MAX_LINES 32

#define MAX_DEVICES 4

#define GCF_HEADER_SIZE 14
#define GCF_MAGIC 0xCAFEFEED

#define FLASH_TYPE_APP_ENCRYPTED             60
#define FLASH_TYPE_APP_COMPRESSED_ENCRYPTED  70
#define FLASH_TYPE_BTL_ENCRYPTED             80
#define FLASH_TYPE_APP_ENCRYPTED_2           90

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
    T_SNIFF,
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
    unsigned long gcfCrc32;

    unsigned char fcontent[MAX_GCF_FILE_SIZE];
    unsigned dataOffset;
} GCF_File;

typedef struct UI_Line
{
    char buf[UI_MAX_LINE_LENGTH];
} UI_Line;

/* The GCF struct holds the complete state as well as GCF file data. */
typedef struct GCF_t
{
    int argc;
    char **argv;
    unsigned rp;     /* ascii[] read pointer */
    unsigned wp;     /* ascii[] write pointer */
    char ascii[512]; /* buffer for raw data */
    state_handler_t state;
    state_handler_t substate;

    /* UI line buffering */
    unsigned uiCurrentLine;
    UI_Line uiLines[UI_MAX_LINES];
    U_SStream uiStringStream;
    int uiDebugLevel;
    int uiInteractive;

    int uiInputPos;
    int uiInputSize;
    char uiInputLine[UI_MAX_INPUT_LENGTH];

    int retry;

    unsigned remaining; /* remaining bytes during upload */

    Task task;

    PROT_RxState rxstate;

    /* sniffer state */
    int sniffChannel;
    const char *sniffHost;
    unsigned sniffWp;
    unsigned sniffLength;
    unsigned char sniffPacket[256];
    unsigned sniffSeqNum;
    S_Udp sniffUdp;

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
static void gcfPrintHelp(void);
static GCF_Status gcfProcessCommandline(GCF *gcf);
static void gcfGetDevices(GCF *gcf);
static void gcfCommandResetUart(void);
static void gcfCommandQueryStatus(void);
static void gcfCommandQueryFirmwareVersion(void);
static void gcfCommandQueryParameter(unsigned char seq, unsigned char id, unsigned char *data, unsigned dataLength);
static void ST_Void(GCF *gcf, Event event);
static void ST_Init(GCF *gcf, Event event);

static void ST_Program(GCF *gcf, Event event);
static void ST_V1ProgramSync(GCF *gcf, Event event);
static void ST_V1ProgramWriteHeader(GCF *gcf, Event event);
static void ST_V1ProgramUpload(GCF *gcf, Event event);
static void ST_V1ProgramValidate(GCF *gcf, Event event);

static void ST_V3ProgramSync(GCF *gcf, Event event);
static void ST_V3ProgramUpload(GCF *gcf, Event event);
static void ST_V3ProgramWaitID(GCF *gcf, Event event);

static void ST_BootloaderConnect(GCF *gcf, Event event);
static void ST_BootloaderQuery(GCF *gcf, Event event);

static void ST_Connect(GCF *gcf, Event event);
static void ST_Connected(GCF *gcf, Event event);

static void ST_SniffConnect(GCF *gcf, Event event);
static void ST_SniffConfig(GCF *gcf, Event event);
static void ST_SniffConfigConfirm(GCF *gcf, Event event);
static void ST_SniffSyncData(GCF *gcf, Event event);
static void ST_SniffRecvData(GCF *gcf, Event event);
static void ST_SniffTeardown(GCF *gcf, Event event);

static void ST_Reset(GCF *gcf, Event event);
static void ST_ResetUart(GCF *gcf, Event event);
static void ST_ResetFtdi(GCF *gcf, Event event);
static void ST_ResetRaspBee(GCF *gcf, Event event);

static void ST_ListDevices(GCF *gcf, Event event);

static UI_Line *UI_NextLine(GCF *gcf);
U_SStream *UI_StringStream(GCF *gcf);
void U_sstream_put_u8hex(U_SStream *ss, unsigned char val);
void U_sstream_put_u32hex(U_SStream *ss, unsigned long val);

static GCF gcfLocal;
static unsigned char gcfSeq = 1;


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

    return line;
}

void UI_Puts(GCF *gcf, const char *str)
{
    (void)gcf;
    if (str[0])
    {
        PL_Print(str);
    }
}

/* helper to get hex byte */
static int GCF_sstream_get_hexbyte(U_SStream *ss, unsigned char *byte)
{
    int i;
    unsigned tmp;

    *byte = 0;

    if (ss->status != U_SSTREAM_OK)
        return 0;

    if (ss->len < (ss->pos + 2))
        return 0;

    for (i = 0; i < 2; i++)
    {
        *byte <<= 4;
        tmp = (unsigned)ss->str[ss->pos + i];
        if      (tmp >= '0' && tmp <= '9') tmp -= '0';
        else if (tmp >= 'a' && tmp <= 'f') { tmp -= 'a'; tmp += 10; }
        else if (tmp >= 'A' && tmp <= 'F') { tmp -= 'A'; tmp += 10; }
        else
        {
            return 0;
        }
        *byte += (unsigned char)(tmp);
    }

    ss->pos += 2;
    return 1;
}

U_SStream *UI_StringStream(GCF *gcf)
{
    UI_Line *line;
    line = UI_NextLine(gcf);
    U_sstream_init(&gcf->uiStringStream, line->buf, sizeof(line->buf));
    return &gcf->uiStringStream;
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
        UI_Puts(gcf, "command UART reset done\n");
        if (gcf->devType == DEV_RASPBEE_1 || gcf->devType == DEV_CONBEE_1)
        {
            /* due FTDI don't wait for disconnect */
            PL_ClearTimeout();
            GCF_HandleEvent(gcf, EV_UART_RESET_SUCCESS);
        }
    }
    else if (event == EV_TIMEOUT)
    {
        UI_Puts(gcf, "command reset timeout\n");
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
            UI_Puts(gcf, "FTDI reset done\n");
            GCF_HandleEvent(gcf, EV_FTDI_RESET_SUCCESS);
        }
        else
        {
            UI_Puts(gcf,  "FTDI reset failed\n");
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
            UI_Puts(gcf, "RaspBee reset done\n");
            GCF_HandleEvent(gcf, EV_RASPBEE_RESET_SUCCESS);
        }
        else
        {
            UI_Puts(gcf, "RaspBee reset failed\n");
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
    unsigned i;
    Device *dev;
    U_SStream *ss;

    if (event == EV_ACTION)
    {
        gcfGetDevices(gcf);

        if (gcf->devCount == 0)
        {
            UI_Puts(gcf, "no devices found\n");
        }

        UI_Puts(gcf, "Path              | Serial      | Type\n");
        UI_Puts(gcf, "------------------+-------------+---------------\n");

        for (i = 0; i < gcf->devCount; i++)
        {
            dev = &gcf->devices[i];
            ss = UI_StringStream(gcf);

            /* 1st column */
            U_sstream_put_str(ss, dev->path);
            for (;ss->pos < 18;)
            {
                U_sstream_put_str(ss, " ");
            }
            U_sstream_put_str(ss, "| ");

            /* 2nd column */
            U_sstream_put_str(ss, dev->serial);
            for (;ss->pos < 32;)
            {
                U_sstream_put_str(ss, " ");
            }
            U_sstream_put_str(ss, "| ");

            /* 3rd column */
            U_sstream_put_str(ss, dev->name);
            U_sstream_put_str(ss, "\n");

            UI_Puts(gcf, ss->str);
        }

        PL_ShutDown();
    }
}

static void ST_Program(GCF *gcf, Event event)
{
    if (event == EV_ACTION)
    {
        gcfGetDevices(gcf);
        UI_Puts(gcf, "flash firmware\n");
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
    U_SStream *ss;
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
            ss = UI_StringStream(gcf);
            U_sstream_put_str(ss, "retry connect bootloader ");
            U_sstream_put_str(ss, gcf->devpath);
            U_sstream_put_str(ss, "\n");
            UI_Puts(gcf, ss->str);
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
    U_SStream *ss;
    U_SStream ss1;
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
            UI_Puts(gcf, "query bootloader failed\n");
            gcfRetry(gcf);
        }
        else if (gcf->file.gcfFileType < 30)
        {
            /* 2) V1 Bootloader of ConBee II
                  Query the id here, after initial timeout. This also
                  catches cases where no firmware was installed.
            */
            UI_Puts(gcf, "query bootloader id V1\n");

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
            UI_Puts(gcf, "query bootloader id V3\n");

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
            U_sstream_init(&ss1, &gcf->ascii[0], gcf->wp);
            if (U_sstream_find(&ss1, "Bootloader"))
            {
                PL_ClearTimeout();
                UI_Puts(gcf, "bootloader detected\n");

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

            ss = UI_StringStream(gcf);
            U_sstream_put_str(ss, "bootloader version 0x");
            U_sstream_put_u32hex(ss, btlVersion);
            U_sstream_put_str(ss, ", app crc 0x");
            U_sstream_put_u32hex(ss, appCrc);
            U_sstream_put_str(ss, "\n\n");
            UI_Puts(gcf, ss->str);

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
    U_SStream *ss;
    U_SStream ss1;
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
        U_sstream_init(&ss1, &gcf->ascii[0], gcf->wp);
        if (gcf->wp > 4 && U_sstream_find(&ss1, "READY"))
        {
            PL_ClearTimeout();
            ss = UI_StringStream(gcf);
            U_sstream_put_str(ss, "bootloader synced: ");
            U_sstream_put_str(ss, gcf->ascii);
            U_sstream_put_str(ss, "\n");
            UI_Puts(gcf, ss->str);
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
        UI_Puts(gcf, "failed to sync bootloader\n");
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
            UI_UpdateProgress(gcf);
        }

        gcf->wp = 0;
        gcf->ascii[0] = '\0';

        PROT_Write(page, size);

        if ((gcf->remaining - size) == 0)
        {
            gcf->state = ST_V1ProgramValidate;
            UI_Puts(gcf, "\ndone, wait validation...\n");
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
        U_sstream_init(&ss, &gcf->ascii[0], gcf->wp);

        if (gcf->wp > 6 && U_sstream_find(&ss, "#VALID CRC"))
        {
            UI_Puts(gcf, FMT_GREEN "firmware successful written\n" FMT_RESET);
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
                0xAA, 0xAA, 0xAA, 0xAA  /* crc32 */
        };

        PL_MSleep(50);
        PL_SetTimeout(1000);

        p = &cmd[2];

        p = put_u32_le(p, &gcf->file.gcfFileSize);
        p = put_u32_le(p, &gcf->file.gcfTargetAddress);
        p = put_u8_le(p, &gcf->file.gcfFileType);
        p = put_u32_le(p, &gcf->file.gcfCrc32);
        (void)p;

        PROT_SendFlagged(cmd, sizeof(cmd));
    }
    else if (event == EV_RX_BTL_PKG_DATA)
    {
        if ((unsigned char)gcf->ascii[1] == BTL_FW_UPDATE_RESPONSE)
        {
            if (gcf->ascii[2] == 0x00) /* success */
            {
                PL_SetTimeout(3000);
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
    U_SStream *ss;
    if (event == EV_RX_BTL_PKG_DATA)
    {
        if ((unsigned char)gcf->ascii[1] == BTL_FW_DATA_REQUEST && gcf->wp == 8)
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
            ss = UI_StringStream(gcf);
            U_sstream_put_str(ss, "BTL data request, offset: ");
            U_sstream_put_long(ss, (long)offset);
            U_sstream_put_str(ss, ", length: ");
            U_sstream_put_long(ss, (long)length);
            U_sstream_put_str(ss, "\n");
            UI_Puts(gcf, ss->str);
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
                gcf->remaining = (unsigned)(gcf->file.gcfFileSize - offset);
                Assert(gcf->remaining < MAX_GCF_FILE_SIZE);
                length = length < gcf->remaining ? length : (unsigned short)gcf->remaining;
                Assert(length > 0);
            }

            p = put_u8_le(p, &status);
            p = put_u32_le(p, &offset);
            p = put_u16_le(p, &length);

            if (status == 0)
            {
                Assert(length > 0);
                U_memcpy(p, &gcf->file.fcontent[gcf->file.dataOffset + offset], length);
                p += length;
            }
            else
            {
                ss = UI_StringStream(gcf);
                U_sstream_put_str(ss, "failed to handle data request, status: ");
                U_sstream_put_long(ss, (long)status);
                U_sstream_put_str(ss, "\n");
                UI_Puts(gcf, ss->str);
            }

            Assert(p > buf);
            Assert(p < buf + sizeof(gcf->ascii));

            PROT_SendFlagged(buf, (unsigned)(p - buf));

            UI_UpdateProgress(gcf);

            if (gcf->remaining == length)
            {
                UI_Puts(gcf, "\ndone, wait (up to 20 seconds) for verification\n");
                PL_SetTimeout(20000);
                gcf->state = ST_V3ProgramWaitID;
            }
        }
        else
        {
            PL_Printf(DBG_DEBUG, "unexpected command %02X\n", (unsigned char)gcf->ascii[1]);
        }
    }
    else if (event == EV_TIMEOUT)
    {
        gcfRetry(gcf);
    }
}

static void ST_V3ProgramWaitID(GCF *gcf, Event event)
{
    U_SStream *ss;
    if (event == EV_RX_BTL_PKG_DATA)
    {
        if ((unsigned char)gcf->ascii[1] == BTL_ID_RESPONSE)
        {
            unsigned long btlVersion;
            unsigned long appCrc;

            get_u32_le((unsigned char*)&gcf->ascii[2], &btlVersion);
            get_u32_le((unsigned char*)&gcf->ascii[6], &appCrc);

            if (gcf->file.gcfCrc32 != 0)
            {
                ss = UI_StringStream(gcf);
                U_sstream_put_str(ss, "app checksum 0x");
                U_sstream_put_u32hex(ss, appCrc);
                if (appCrc == gcf->file.gcfCrc32)
                {
                    U_sstream_put_str(ss, " (OK)");
                }
                else
                {
                    U_sstream_put_str(ss, " (expected 0x");
                    U_sstream_put_u32hex(ss, gcf->file.gcfCrc32);
                    U_sstream_put_str(ss, ")");
                }
                U_sstream_put_str(ss, "\n");
                UI_Puts(gcf, ss->str);
            }

            UI_Puts(gcf, "finished\n");
            PL_ShutDown();
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
            UI_Puts(gcf, "failed to connect\n");
            PL_SetTimeout(10000);
        }
    }
}

static void ST_Connected(GCF *gcf, Event event)
{
    if (event == EV_TIMEOUT)
    {
        if (gcf->uiInteractive == 0)
        {
            gcfCommandQueryStatus();
        }

        PL_SetTimeout(10000);
    }
    else if (event == EV_DISCONNECTED)
    {
        PL_ClearTimeout();
        gcf->state = ST_Init;
        UI_Puts(gcf, "disconnected\n");
        PL_SetTimeout(1000);
    }
}

#ifdef USE_SNIFF

static void ST_SniffConnect(GCF *gcf, Event event)
{
    if (event == EV_ACTION)
    {
        gcf->sniffSeqNum = 0;

        SOCK_UdpInit(&gcf->sniffUdp, SOCK_GetHostAF(gcf->sniffHost));
        SOCK_UdpSetPeer(&gcf->sniffUdp, gcf->sniffHost, 17754);

        if (PL_Connect(gcf->devpath, gcf->devBaudrate) == GCF_SUCCESS)
        {
            gcf->state = ST_SniffConfig;
            PL_SetTimeout(250);
        }
        else
        {
            gcf->state = ST_SniffTeardown;
            UI_Puts(gcf, "failed to connect\n");
            PL_SetTimeout(10000);
        }
    }
}

static void ST_SniffConfig(GCF *gcf, Event event)
{
    U_SStream ss;
    char buf[128];

    if (event == EV_TIMEOUT)
    {
        U_sstream_init(&ss, &buf[0], sizeof(buf));
        U_sstream_put_str(&ss, "\nidle\n");
        U_sstream_put_str(&ss, "\nchan ");
        U_sstream_put_long(&ss, gcf->sniffChannel);
        U_sstream_put_str(&ss, "\n");
        U_sstream_put_str(&ss, "\nsniff\n");

        PROT_Write((unsigned char*)&buf[0], ss.pos);

        gcf->wp = 0;

        gcf->state = ST_SniffConfigConfirm;
        PL_SetTimeout(1000);
    }
    else if (event == EV_DISCONNECTED)
    {
        PL_ClearTimeout();
        gcf->state = ST_SniffTeardown;
        PL_SetTimeout(1000);
    }
}

static void ST_SniffConfigConfirm(GCF *gcf, Event event)
{
    U_SStream ss;

    if (event == EV_RX_ASCII)
    {
        U_sstream_init(&ss, &gcf->ascii[0], gcf->wp);

        if (U_sstream_find(&ss, "Receiving...OK"))
        {
            PL_ClearTimeout();
            gcf->state = ST_SniffSyncData;
            gcf->sniffWp = 0;
            gcf->sniffLength = 0;
            UI_Puts(gcf, "sniffing started, send traffic to host ");
            UI_Puts(gcf, gcf->sniffHost);
            UI_Puts(gcf, " port 17754\n");
            PL_SetTimeout(3600000);
            gcf->wp = 0;
            gcf->rp = 0;
        }
    }
    else if (event == EV_TIMEOUT)
    {
        gcf->state = ST_SniffTeardown;
        PL_SetTimeout(1000);
    }
    else if (event == EV_DISCONNECTED)
    {
        PL_ClearTimeout();
        gcf->state = ST_SniffTeardown;
        PL_SetTimeout(1000);
    }
}

static void ST_SniffSyncData(GCF *gcf, Event event)
{
    unsigned i;

    if (event == EV_RX_ASCII || event == EV_PL_LOOP)
    {
        gcf->sniffLength = 0;

        if (gcf->rp < gcf->wp)
        {
            for (;gcf->ascii[gcf->rp] != 0x01 && gcf->rp < gcf->wp;)
            {
                gcf->rp += 1; /* forward to start marker */
            }

            i = gcf->rp;

            /* frame starts with 0x01 and ends with trailer 0x04 */
            if (gcf->ascii[i] == 0x01 && (i + 1) < gcf->wp)
            {
                gcf->sniffWp = 0;
                gcf->sniffLength = (unsigned)gcf->ascii[i + 1] & 0xFF;

                if (gcf->sniffLength < 8) /* min. frame length due 8byte dummy timestamp 01..08 */
                {
                    gcf->rp += 1;
                    return;
                }

                if ((2 + gcf->sniffLength) < (gcf->wp - gcf->rp))
                {
                    if (gcf->ascii[i + 2 + gcf->sniffLength] == 0x04) /* full frame */
                    {
                        gcf->rp = i + 2;
                        gcf->state = ST_SniffRecvData;
                        GCF_HandleEvent(gcf, EV_RX_ASCII);
                    }
                    else
                    {
                        /* invalid frame */
                        gcf->rp += 1;
                    }
                }

                return;
            }
        }

        /* no sync data found */
        gcf->rp = 0;
        gcf->wp = 0;
    }
    if (event == EV_TIMEOUT)
    {
        gcf->state = ST_SniffTeardown;
        PL_SetTimeout(1000);
    }
    else if (event == EV_DISCONNECTED)
    {
        PL_ClearTimeout();
        gcf->state = ST_SniffTeardown;
        PL_SetTimeout(1000);
    }
}

static void ST_SniffRecvData(GCF *gcf, Event event)
{
    unsigned i;
    unsigned char type;
    U_SStream *ss;
    U_BStream bs;
    char buf[256];

    if (event == EV_RX_ASCII)
    {
        for (i = gcf->rp; i < gcf->wp; i++)
        {
            Assert(gcf->sniffWp < sizeof(gcf->sniffPacket));
            Assert(gcf->rp < sizeof(gcf->ascii));
            gcf->sniffPacket[gcf->sniffWp] = gcf->ascii[gcf->rp];
            gcf->sniffWp++;
            gcf->rp++;

            if (gcf->sniffWp == gcf->sniffLength + 1) /* extra 0x04 end marker */
                break;
        }

        /* move unprocessed data to start in gcf->ascii */
        for (i = 0; gcf->rp < gcf->wp; i++)
        {
            gcf->ascii[i] = gcf->ascii[gcf->rp];
            gcf->rp++;
        }

        gcf->rp = 0;
        gcf->wp = i;

        if (gcf->sniffWp == gcf->sniffLength + 1)
        {
            if (gcf->sniffPacket[gcf->sniffLength] == 0x04) /* end marker */
            {
                if (gcf->uiDebugLevel != 0)
                {
                    ss = UI_StringStream(gcf);
                    U_sstream_put_str(ss, "pkg(");
                    U_sstream_put_long(ss, (long)gcf->sniffLength);
                    U_sstream_put_str(ss, "/");
                    U_sstream_put_long(ss, (long)gcf->sniffSeqNum);
                    U_sstream_put_str(ss, ") ");

                    for (i = 0; i < gcf->sniffLength; i++)
                    {
                        U_sstream_put_u8hex(ss, (unsigned char)gcf->ascii[i]);
                        U_sstream_put_str(ss, " ");
                    }

                    U_sstream_put_str(ss, "\n");
                    UI_Puts(gcf, ss->str);
                }

                /*------------------------------------------------------------
                *
                *      ZEP Packets must be received in the following format:
                *      |UDP Header|  ZEP Header |IEEE 802.15.4 Packet|
                *      | 8 bytes  | 16/32 bytes |    <= 127 bytes    |
                *------------------------------------------------------------
                *
                *      ZEP v1 Header will have the following format:
                *      |Preamble|Version|Channel ID|Device ID|CRC/LQI Mode|LQI Val|Reserved|Length|
                *      |2 bytes |1 byte |  1 byte  | 2 bytes |   1 byte   |1 byte |7 bytes |1 byte|
                *
                *      ZEP v2 Header will have the following format (if type=1/Data):
                *      |Preamble|Version| Type |Channel ID|Device ID|CRC/LQI Mode|LQI Val|NTP Timestamp|Sequence#|Reserved|Length|
                *      |2 bytes |1 byte |1 byte|  1 byte  | 2 bytes |   1 byte   |1 byte |   8 bytes   | 4 bytes |10 bytes|1 byte|
                *
                *      ZEP v2 Header will have the following format (if type=2/Ack):
                *      |Preamble|Version| Type |Sequence#|
                *      |2 bytes |1 byte |1 byte| 4 bytes |
                *------------------------------------------------------------
                */
                U_bstream_init(&bs, &buf[0], sizeof(buf));

                U_bstream_put_u8(&bs, (unsigned char)'E');
                U_bstream_put_u8(&bs, (unsigned char)'X');
                U_bstream_put_u8(&bs, 2); /* version */

                type = gcf->sniffLength >= (8 + 5) ? 1 : 2; /* data(1), ack(2) */
                U_bstream_put_u8(&bs, type);

                if (type == 1) /* data */
                {
                    U_bstream_put_u8(&bs, gcf->sniffChannel);
                    U_bstream_put_u8(&bs, 0); /* device ID */
                    U_bstream_put_u8(&bs, 0); /* device ID */
                    U_bstream_put_u8(&bs, 0); /* CRC/LQI mode*/
                    U_bstream_put_u8(&bs, 0); /* LQI val */

                    U_bstream_put_u8(&bs, 0); /* NTP timestamp */
                    U_bstream_put_u8(&bs, 0); /* NTP timestamp */
                    U_bstream_put_u8(&bs, 0); /* NTP timestamp */
                    U_bstream_put_u8(&bs, 0); /* NTP timestamp */
                    U_bstream_put_u8(&bs, 0); /* NTP timestamp */
                    U_bstream_put_u8(&bs, 0); /* NTP timestamp */
                    U_bstream_put_u8(&bs, 0); /* NTP timestamp */
                    U_bstream_put_u8(&bs, 0); /* NTP timestamp */
                }

                U_bstream_put_u32_be(&bs, gcf->sniffSeqNum);
                gcf->sniffSeqNum++;

                if (type == 1) /* data */
                {
                    for (i = 0; i < 10; i++)
                        U_bstream_put_u8(&bs, 0); /* reserved 10 bytes */

                    U_bstream_put_u8(&bs, gcf->sniffLength - 8); /* length */
                    for (i = 8; i < gcf->sniffLength; i++)
                        U_bstream_put_u8(&bs, gcf->sniffPacket[i]); /* data */
                }

                SOCK_UdpSend(&gcf->sniffUdp, bs.data, bs.pos);
            }

            gcf->sniffWp = 0;
            gcf->sniffLength = 0;
            gcf->state = ST_SniffSyncData;
        }
    }
    if (event == EV_TIMEOUT)
    {
        gcf->state = ST_SniffTeardown;
        PL_SetTimeout(1000);
    }
    else if (event == EV_DISCONNECTED)
    {
        PL_ClearTimeout();
        gcf->state = ST_SniffTeardown;
        PL_SetTimeout(1000);
    }
}

static void ST_SniffTeardown(GCF *gcf, Event event)
{
    (void)event;

    SOCK_UdpFree(&gcf->sniffUdp);
    PL_ClearTimeout();
    gcf->state = ST_Init;
    UI_Puts(gcf, "sniffer stop\n");
    PL_SetTimeout(1000);
}

#endif /* USE_SNIFF */

GCF *GCF_Init(int argc, char *argv[])
{
    GCF *gcf;

    gcf = &gcfLocal;

    U_bzero(&gcf->rxstate, sizeof(gcf->rxstate));
    gcf->startTime = PL_Time();
    gcf->maxTime = 0;
    gcf->sniffChannel = 0;
    gcf->sniffHost = "127.0.0.1";
    gcf->devCount = 0;
    gcf->task = T_NONE;
    gcf->uiInteractive = 0;
    gcf->uiDebugLevel = 0;
    gcf->uiInputPos = 0;
    gcf->uiInputSize = 0;
    gcf->uiInputLine[0] = '\0';
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

    if (event == EV_PL_LOOP && gcf->state == ST_SniffSyncData)
    {
        /* allowed to process loop */
    }
    else if (event == EV_PL_LOOP)
    {
        NET_Step();
        return;
    }

    gcf->state(gcf, event);
}

int GCF_ParseFile(GCF_File *file)
{
    unsigned char ch;
    const char *version;
    unsigned long magic;
    U_BStream bs[1];

    if (file->fsize < 14)
    {
        return -1;
    }

    U_bstream_init(bs, file->fcontent, file->fsize);

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
    magic = U_bstream_get_u32_le(bs);
    file->gcfFileType = U_bstream_get_u8(bs);
    file->gcfTargetAddress = U_bstream_get_u32_le(bs);
    file->gcfFileSize = U_bstream_get_u32_le(bs);
    file->gcfCrc = U_bstream_get_u8(bs);

    PL_Printf(DBG_DEBUG, "GCF header0: magic: 0x%08X, type: %u, address: 0x%08X, data.size: %lu\n", magic, file->gcfFileType, file->gcfTargetAddress, file->gcfFileSize);

    /* newer products have extended format with CRC32 */
    file->gcfCrc32 = 0;
    file->dataOffset = GCF_HEADER_SIZE;

    if (file->gcfFileType == FLASH_TYPE_APP_ENCRYPTED)
    {
        /*
         * u32 magic
         *   0xDEC0DE02 Hive
         *   0xDEC0DE03 ConBee III
         *
         * u32 total_size
         * image_1
         * ...
         * image_N
         * u32 crc32 over everything
         *
         * image format:
         *   u32 image_size
         *   u32 image_type
         *   u32 target_address
         *   u32 plain_image_size (uncompressed)
         *   u32 plain_crc2
         *   u8[] data (4-byte aligned)
         */

        unsigned long magic1;
        unsigned long totalSize;
        unsigned long imageSize;
        unsigned long imageType;
        unsigned long imageTargetAddress;
        unsigned long imagePlainSize;

        magic1 = U_bstream_get_u32_le(bs);

        totalSize = U_bstream_get_u32_le(bs);
        Assert(totalSize == file->gcfFileSize);
        imageSize = U_bstream_get_u32_le(bs);
        (void)imageSize;
        imageType = U_bstream_get_u32_le(bs);
        imageTargetAddress = U_bstream_get_u32_le(bs);
        imagePlainSize = U_bstream_get_u32_le(bs);
        file->gcfCrc32 = U_bstream_get_u32_le(bs);

        PL_Printf(DBG_DEBUG, "GCF header1: product: 0x%08X, img.type: %u, img.address: 0x%08X, img.data.size: %lu, crc32: 0x%08X\n",
                  magic1, imageType, imageTargetAddress, imagePlainSize, file->gcfCrc32);
    }
    else if (file->gcfFileType == FLASH_TYPE_APP_ENCRYPTED_2)
    {
        /* here the CRC32 is part of the header but not of the 'gcfFileSize' */
        file->gcfCrc32 = U_bstream_get_u32_le(bs);
        file->dataOffset = GCF_HEADER_SIZE + 4;
    }

    if (magic != GCF_MAGIC)
    {
        return -2;
    }

    if (file->gcfFileSize != (file->fsize - file->dataOffset))
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

    /*gcfDebugHex(gcf, "recv", data, len);*/

    if (gcf->task == T_SNIFF ||
        gcf->state == ST_BootloaderQuery ||
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

        if (gcf->task == T_SNIFF)
        {
            return;
        }
    }

    PROT_ReceiveFlagged(&gcf->rxstate, data, (unsigned)len);
}

static void GCF_ProcessInput(GCF *gcf)
{
    U_SStream rs;
    U_SStream *ss;

    unsigned w;
    unsigned h;
    long param;
    unsigned argLength;
    unsigned char arg[32];

    if (gcf->uiInputSize == 0)
    {
        UI_Puts(gcf, "use 'help' to see a list of available commands\n");
        return;
    }


    /* echo input line */
    if (gcf->uiInputSize > 0 && gcf->uiInputSize < UI_MAX_INPUT_LENGTH)
    {
        UI_GetWinSize(&w, &h);

        ss = UI_StringStream(gcf);

        U_sstream_put_str(ss, "\n> ");
        U_sstream_put_str(ss, gcf->uiInputLine);

        for (;ss->pos < w && (ss->pos < (ss->len - 2)); )
        {
            U_sstream_put_str(ss, " ");
        }

        U_sstream_put_str(ss, "\n");
        UI_Puts(gcf, ss->str);
    }

    /* process input line */
    U_sstream_init(&rs, gcf->uiInputLine, gcf->uiInputSize);

    if (U_sstream_starts_with(&rs, "help"))
    {
        UI_Puts(gcf, "commands:\n");
        UI_Puts(gcf, "rp <id> [hex payload]         | read config parameter id (decimal) and optional\n"
                     "                              | payload as 0x... hex string\n");
    }
    else if (U_sstream_starts_with(&rs, "read ") ||  U_sstream_starts_with(&rs, "rp "))
    {
        U_sstream_find(&rs, " ");
        U_sstream_skip_whitespace(&rs);
        param = U_sstream_get_long(&rs);
        if ((rs.status != U_SSTREAM_OK) || (param > 255))
        {
            UI_Puts(gcf, "invalid argument for parameter <id>\n");
        }
        else
        {
            U_sstream_skip_whitespace(&rs);
            ss = UI_StringStream(gcf);

            U_sstream_put_str(ss, "> reading parameter: ");
            U_sstream_put_long(ss, param);

            argLength = 0;
            if (U_sstream_starts_with(&rs, "0x"))
            {
                U_sstream_seek(&rs, U_sstream_pos(&rs) + 2);
                unsigned char byte;
                for (; GCF_sstream_get_hexbyte(&rs, &byte) && argLength < sizeof(arg);)
                {
                    arg[argLength] = byte;
                    argLength++;
                    U_sstream_put_str(ss, " ");
                    U_sstream_put_long(ss, (long)byte);
                }
            }

            U_sstream_put_str(ss, "\n");
            UI_Puts(gcf, ss->str);

            gcfCommandQueryParameter(gcfSeq++, (unsigned char)param, arg, argLength);
        }
    }

    gcf->uiInputPos = 0;
    gcf->uiInputSize = 0;
}

/* Very basic TUI keyboard input support. */
void GCF_KeyboardInput(GCF *gcf, unsigned long codepoint)
{
    int i;
    unsigned w;
    unsigned h;

    if (codepoint == PL_KEY_ENTER)
    {
        GCF_ProcessInput(gcf);
    }
    else if (codepoint == PL_KEY_BACKSPACE)
    {
        if (gcf->uiInputPos > 0 && gcf->uiInputSize > 0)
        {
            for (i = 0; i < (gcf->uiInputSize - gcf->uiInputPos); i++)
            {
                gcf->uiInputLine[gcf->uiInputPos + i - 1] = gcf->uiInputLine[gcf->uiInputPos + i];
            }

            gcf->uiInputPos--;
            gcf->uiInputSize--;
            gcf->uiInputLine[gcf->uiInputSize] = '\0';
        }
    }
    else if (codepoint == PL_KEY_DELETE)
    {
        if (gcf->uiInputPos >= 0 && gcf->uiInputPos < gcf->uiInputSize && gcf->uiInputSize > 0)
        {
            for (i = 0; i < (gcf->uiInputSize - gcf->uiInputPos); i++)
            {
                gcf->uiInputLine[gcf->uiInputPos + i] = gcf->uiInputLine[gcf->uiInputPos + i + 1];
            }

            gcf->uiInputSize--;
            gcf->uiInputLine[gcf->uiInputSize] = '\0';
        }
    }
    else if (codepoint == PL_KEY_LEFT)
    {
        if (gcf->uiInputPos > 0)
            gcf->uiInputPos--;
    }
    else if (codepoint == PL_KEY_RIGHT)
    {
        if (gcf->uiInputPos < gcf->uiInputSize)
            gcf->uiInputPos++;
    }
    else if (codepoint == PL_KEY_POS1)
    {
        gcf->uiInputPos = 0;
    }
    else if (codepoint == PL_KEY_END)
    {
        gcf->uiInputPos = gcf->uiInputSize;
    }
    else if (codepoint >= 32 && codepoint <= 126)
    {
        if (gcf->uiInputSize < UI_MAX_INPUT_LENGTH)
        {
            for (i = 0; i < (gcf->uiInputSize - gcf->uiInputPos); i++)
            {
                gcf->uiInputLine[gcf->uiInputSize - i] = gcf->uiInputLine[gcf->uiInputSize - (i + 1)];
            }

            gcf->uiInputLine[gcf->uiInputPos++] = codepoint & 0xFF;
            gcf->uiInputSize++;
            gcf->uiInputLine[gcf->uiInputSize] = '\0';
        }
    }

    /* echo */
    if (gcf->uiInputSize > 0 && gcf->uiInputSize < UI_MAX_INPUT_LENGTH)
    {
        char buf[384];
        UI_GetWinSize(&w, &h);
        UI_SetCursor(0, h);
        for (i = 0; i < gcf->uiInputSize; i++)
        {
            buf[i] = gcf->uiInputLine[i];
        }
        for (; i < (int)(sizeof(buf) - 1) && i < (int)w; i++)
        {
            buf[i] = ' ';
        }
        buf[i] = '\0';
        PL_Print(&buf[0]);
        UI_SetCursor(gcf->uiInputPos + 1, h);
    }
}

void NET_Received(int client_id, const unsigned char *buf, unsigned bufsize)
{
    (void)buf;
    PL_Printf(DBG_DEBUG, "NET received from client %d: %d bytes\n", client_id, bufsize);
}

void PROT_Packet(const unsigned char *data, unsigned len)
{
    int i;
    char *p;
    GCF *gcf;
    U_SStream *ss;

    Assert(len > 0);

    gcf = &gcfLocal;

    if (gcf->uiInteractive && gcf->uiInputSize)
    {
        /* don't scramble console output */
    }
    else if (data[0] != BTL_MAGIC && gcf->task == T_CONNECT)
    {
        p = &gcf->ascii[0];
        for (i = 0; i < (int)len; i++, p += 2)
        {
            put_hex(data[i], p);
        }
        *p = '\0';
        ss = UI_StringStream(gcf);
        U_sstream_put_str(ss, "packet: ");
        U_sstream_put_long(ss, (long)len);
        U_sstream_put_str(ss, " bytes, ");
        U_sstream_put_str(ss, gcf->ascii);
        U_sstream_put_str(ss, "\n");
        UI_Puts(gcf, ss->str);
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
    PL_time_t now;
    U_SStream *ss;

    now = PL_Time();

    if (gcf->maxTime > now)
    {
        ss = UI_StringStream(gcf);
        U_sstream_put_str(ss, "retry: ");
        U_sstream_put_long(ss, (long)(gcf->maxTime - now) / 1000);
        U_sstream_put_str(ss, " seconds left\n");
        UI_Puts(gcf, ss->str);

        gcf->state = ST_Init;
        gcf->substate = ST_Void;
        PL_SetTimeout(250);
    }
    else
    {
        PL_ShutDown();
    }
}

static void gcfPrintHelp(void)
{
    const char *usage =
    "usage: GCFFlasher <options>\n"
    "options:\n"
    " -r              force device reboot without programming\n"
    " -f <firmware>   flash firmware file\n"
#if defined(PL_WIN) || defined(PL_DOS)
    " -d <com port>   COM port to use, e.g. COM1\n"
#else
    " -d <device>     device number or path to use, e.g. 0, /dev/ttyUSB0 or RaspBee\n"
#ifdef USE_NET
    " -n <interface>  listen interface\n"
    "                 when only -p is specified default is 0.0.0.0 for any interface\n"
    " -p <port>       listen port\n"
#endif
#endif
    #ifdef USE_SNIFF
    " -s <channel>    enable sniffer on Zigbee channel (requires sniffer firmware)\n"
    "                 the Wireshark sniffer traffic is send to UDP port 17754\n"
    " -H <host>       send sniffer traffic to Wireshark running on host\n"
    "                 default is 172.0.0.1 (localhost)\n"
    #endif
    " -c              connect and debug serial protocol\n"
//    " -s <serial>     serial number to use\n"
    " -t <timeout>    retry until timeout (seconds) is reached\n"
    " -l              list devices\n"
    " -x <loglevel>   debug log level 0, 1, 3\n"
#ifdef PL_LINUX
    " -i              interactive mode for debugging\n"
#endif
    " -h -?           print this help\n";

#ifdef __WATCOMC__
    PL_Printf(DBG_INFO, "GCFFlasher copyright dresden elektronik ingenieurtechnik gmbh\n"); /* compiler isn't happy with string define */
#else
    PL_Printf(DBG_INFO, "GCFFlasher %s copyright dresden elektronik ingenieurtechnik gmbh\n", APP_VERSION);
#endif

    PL_Print(usage);
}

/* helper to print 0x%08X format */
void U_sstream_put_u32hex(U_SStream *ss, unsigned long val)
{
    unsigned i;
    unsigned char nib;

    if ((ss->len - ss->pos) < (8 + 1))
    {
        ss->status = U_SSTREAM_ERR_NO_SPACE;
        return;
    }

    for (i = 0; i < 4; i++)
    {
        nib = (val >> 24) & 0xFF;
        val <<= 8;
        put_hex(nib, &ss->str[ss->pos]);
        ss->pos += 2;
    }

    ss->str[ss->pos] = '\0';
}

/* helper to print %02X format */
void U_sstream_put_u8hex(U_SStream *ss, unsigned char val)
{
    if ((ss->len - ss->pos) < (2 + 1))
    {
        ss->status = U_SSTREAM_ERR_NO_SPACE;
        return;
    }

    put_hex(val, &ss->str[ss->pos]);
    ss->pos += 2;

    ss->str[ss->pos] = '\0';
}

void gcfDebugHex(GCF *gcf, const char *msg, const unsigned char *data, unsigned size)
{
    if (gcf->uiDebugLevel == 0)
        return;

    char *p;
    char buf[1024];
    unsigned i;
    U_SStream *ss;

    p = &buf[0];

    Assert(size < (sizeof(buf) / 2) - 1);
    for (i = 0; i < size; i++, p += 2)
    {
        put_hex(data[i], p);
    }
    *p = '\0';

    ss = UI_StringStream(gcf);
    U_sstream_put_str(ss, FMT_GREEN);
    U_sstream_put_str(ss, msg);
    U_sstream_put_str(ss, ":" FMT_RESET " ");
    U_sstream_put_str(ss, &buf[0]);
    U_sstream_put_str(ss, " (");
    U_sstream_put_long(ss, (long)size);
    U_sstream_put_str(ss, ")\n");
    UI_Puts(gcf, ss->str);
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
    gcf->uiInteractive = 0;
    gcf->uiDebugLevel = 0;
    gcf->sniffChannel = 0;
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

                case 'i':
                {
                    gcf->uiInteractive = 1;
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

                    gcf->maxTime = (PL_time_t)longval;
                    gcf->maxTime *= 1000;
                    gcf->maxTime += gcf->startTime;

                } break;

#ifdef USE_SNIFF
                case 's':
                {
                    if ((i + 1) == gcf->argc)
                    {
                        PL_Printf(DBG_INFO, "missing argument for parameter -s\n");
                        return GCF_FAILED;
                    }

                    i++;
                    arg = gcf->argv[i];

                    U_sstream_init(&ss, gcf->argv[i], U_strlen(gcf->argv[i]));

                    longval = U_sstream_get_long(&ss); /* seconds */

                    if (ss.status != U_SSTREAM_OK || longval < 11 || longval > 26)
                    {
                        PL_Printf(DBG_INFO, "invalid argument, %s, for parameter -s\n", arg);
                        return GCF_FAILED;
                    }

                    gcf->task = T_SNIFF;
                    gcf->sniffChannel = (int)longval;
                } break;

                case 'H':
                {
                    if ((i + 1) == gcf->argc)
                    {
                        PL_Printf(DBG_INFO, "missing argument for parameter -H\n");
                        return GCF_FAILED;
                    }

                    i++;
                    gcf->sniffHost = gcf->argv[i];
                } break;
#endif /* USE_SNIFF */

                case 'x':
                {
                    if ((i + 1) == gcf->argc || gcf->argv[i + 1][0] == '-')
                    {
                        PL_Printf(DBG_INFO, "missing argument for parameter -x\n");
                        return GCF_FAILED;
                    }

                    i++;
                    arg = gcf->argv[i];

                    U_sstream_init(&ss, gcf->argv[i], U_strlen(gcf->argv[i]));

                    longval = U_sstream_get_long(&ss); /* debug level */

                    if (ss.status != U_SSTREAM_OK || longval < 0 || longval > 3)
                    {
                        PL_Printf(DBG_INFO, "invalid argument, %s, for parameter -x\n", arg);
                        return GCF_FAILED;
                    }

                    gcf->uiDebugLevel = (int)longval;
                } break;

#ifdef USE_NET
                case 'p':
                {
                    if ((i + 1) == gcf->argc || gcf->argv[i + 1][0] == '-')
                    {
                        PL_Printf(DBG_INFO, "missing argument for parameter -p\n");
                        return GCF_FAILED;
                    }

                    i++;
                    arg = gcf->argv[i];

                    U_sstream_init(&ss, gcf->argv[i], U_strlen(gcf->argv[i]));

                    longval = U_sstream_get_long(&ss); /* port */

                    if (ss.status != U_SSTREAM_OK || longval < 0 || longval > 65535)
                    {
                        PL_Printf(DBG_INFO, "invalid argument, %s, for parameter -p\n", arg);
                        return GCF_FAILED;
                    }

                    if (NET_Init(0, (unsigned short)longval) != 1)
                    {
                        PL_Printf(DBG_INFO, "failed to start network server\n");
                        return GCF_FAILED;
                    }
                }
                    break;
#endif /* USE_NET */
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
#ifdef USE_SNIFF
    else if (gcf->task == T_SNIFF)
    {
        if (gcf->devpath[0] == '\0')
        {
            PL_Printf(DBG_INFO, "missing -d argument\n");
            return GCF_FAILED;
        }

        gcf->state = ST_SniffConnect;
        ret = GCF_SUCCESS;
    }
#endif /* USE_SNIFF */
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

static void gcfCommandResetUart(void)
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

static void gcfCommandQueryParameter(unsigned char seq, unsigned char id, unsigned char *data, unsigned dataLength)
{
    unsigned i;
    U_BStream  bs;
    unsigned char cmd[127];

    U_bstream_init(&bs, cmd, sizeof(cmd));
    U_bstream_put_u8(&bs, 0x0A); /* command: read parameter */
    U_bstream_put_u8(&bs, seq); /*  sequence number */
    U_bstream_put_u8(&bs, 0x00); /* status */
    U_bstream_put_u16_le(&bs, 3 + 2 + 2 + 1 + dataLength); /* frame length */
    U_bstream_put_u16_le(&bs, dataLength + 1); /* dynamic buffer length */
    U_bstream_put_u8(&bs, id); /* parameter id */

    for (i = 0; i < dataLength; i++)
    {
        U_bstream_put_u8(&bs, data[i]);
    }

    PROT_SendFlagged(cmd, bs.pos);
}

static void gcfCommandQueryStatus(void)
{
    unsigned char cmd[] = {
        0x07, // command: write parmater
        0x02, // seq
        0x00, // status
        0x08, 0x00, // frame length (12)
        0x00, 0x00, 0x00 // dummy bytes
    };

    cmd[1] = gcfSeq++;

    PROT_SendFlagged(cmd, sizeof(cmd));
}

static void gcfCommandQueryFirmwareVersion(void)
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
