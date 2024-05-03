/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

/* DOS port out of curiosity
 *
 * cmake -G "Watcom WMake" -D CMAKE_SYSTEM_NAME=DOS -D CMAKE_SYSTEM_PROCESSOR=x86 -B build .
 *
 */


#include <dos.h>
#include <stdio.h>
#include <conio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include "gcf.h"
#include "u_sstream.h"
#include "u_strlen.h"

typedef struct
{
    PL_time_t timer;
    volatile unsigned long time;

    uint8_t running;
    uint8_t rx_buf[1024];
    uint8_t txbuf[2048];
    size_t txpos;

    volatile int rx_head;
    volatile int rx_tail;

    unsigned short com_port;
    unsigned short com_int;
    unsigned short com_int_vect;

    GCF *gcf;
} PL_Internal;

static PL_Internal platform;

/*! Returns a monotonic time in milliseconds. */
PL_time_t PL_Time(void)
{
    return platform.time;
}

/*! Lets the program sleep for \p ms milliseconds. */
void PL_MSleep(unsigned long ms)
{
    delay((int)ms);
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
    int result;

    (void)max;

    result = 1;

    devs->name[0] = 'C';
    devs->name[1] = 'O';
    devs->name[2] = 'M';
    devs->name[3] = '1';
    devs->name[4] = '\0';

    devs->serial[0] = '1';
    devs->serial[1] = '\0';

    devs->baudrate = 115200;

    devs->path[0] = 'C';
    devs->path[1] = 'O';
    devs->path[2] = 'M';
    devs->path[3] = '1';
    devs->path[4] = '\0';

    return result;
}

#define COM1_BASE_ADDR 0x3F8
#define COM2_BASE_ADDR 0x2F8
#define COM3_BASE_ADDR 0x3E8
#define COM4_BASE_ADDR 0x2E8

#define COM1_INT 4
#define COM2_INT 3
#define COM3_INT 4
#define COM4_INT 3

/* COM1 - 0x0C, COM2 - 0x0B, COM3 - 0x0C, COM4 - 0x0B */
#define COM1_INT_VECT 0x0C
#define COM2_INT_VECT 0x0B
#define COM3_INT_VECT 0x0C
#define COM4_INT_VECT 0x0B

void interrupt (*oldportisr)();

static int rx_count;
void __interrupt __far port_isr()
{
    int c;

    if (platform.rx_tail == platform.rx_head)
    {
        platform.rx_head = 0;
        platform.rx_tail = 0;
    }

    do {
        /* get the content of the LSR (line status register)
           if Bit 0 of LSR is set, than one or more data bytes are available */
        c = inp(platform.com_port + 5);
        if (c & 1) {	/* check Bit 0 */
            platform.rx_buf[platform.rx_tail] = (unsigned char)inp(platform.com_port);  /* get the character and store it in the buffer */
            platform.rx_tail = (platform.rx_tail + 1) & (sizeof(platform.rx_buf) - 1);
            rx_count++;
        }
    } while (c & 1);	    /* while data ready */

    outp(0x20,0x20);  /* clear the interrupt */
}

/*! Opens the serial port connection for device.

    https://zeiner.at/informatik/c/serialport.html

    \param path - The path like /dev/ttyACM0 or COM7.
    \returns GCF_SUCCESS or GCF_FAILED
 */
GCF_Status PL_Connect(const char *path, PL_Baudrate baudrate)
{
    PL_Printf(DBG_INFO, "PL_Connect: %s\n", path);

    unsigned short port;

    platform.com_port = COM4_BASE_ADDR;
    platform.com_int = COM4_INT;
    platform.com_int_vect = COM4_INT_VECT;

    port = platform.com_port;

    outp(port + 1 , 0);        /* Turn off interrupts */
    oldportisr = _dos_getvect(platform.com_int_vect); /* Save old Interrupt Vector of later recovery */

    /* Set Interrupt Vector Entry */
    /* COM1 - 0x0C, COM2 - 0x0B, COM3 - 0x0C, COM4 - 0x0B */
    _dos_setvect(platform.com_int_vect, port_isr);

    /*         PORT 1 - Communication Settings         */

    outp(port + 3 , 0x80);  /* SET DLAB ON */
    outp(port + 0 , 0x01);  /* Set Baud rate - Divisor Latch Low Byte */
    /*         0x01 = 115,200 BPS */
    /*         0x03 =  38,400 BPS */
    outp(port + 1 , 0x00);  /* Set Baud rate - Divisor Latch High Byte */
    outp(port + 3 , 0x03);  /* 8 Bits, No Parity, 1 Stop Bit */
    outp(port + 2 , 0xC7);  /* FIFO Control Register */
//    outp(port + 2 , 0x07);  /* clear FIFO */
//    outp(port + 2 , 0x00);  /* disable FIFO */

    outp(port + 4 , 0x0B);  /* Turn on DTR, RTS, and OUT2 */
    /* Set Programmable Interrupt Controller */
    /* COM1, COM3 (IRQ4) - 0xEF  */
    /* COM2, COM4 (IRQ3) - 0xF7  */

    if (port == COM1_BASE_ADDR || port == COM3_BASE_ADDR)
        outp(0x21,(inp(0x21) & 0xEF));
    else
        outp(0x21,(inp(0x21) & 0xF7));

    outp(port + 1 , 0x01);  /* Interrupt when data received */

    return GCF_SUCCESS;
}

/*! Closed the serial port connection. */
void PL_Disconnect(void)
{
    PL_Printf(DBG_DEBUG, "PL_Disconnect\n");
    platform.txpos = 0;

    if (platform.com_port)
    {
        outp(platform.com_port + 1 , 0);     /* Turn off interrupts */
        /* COM1 und COM3 (IRQ4) - 0x10  */
        /* COM2 unf COM4 (IRQ3) - 0x08  */
        if (platform.com_port == COM1_BASE_ADDR || platform.com_port == COM3_BASE_ADDR)
            outp(0x21, (inp(0x21) | 0x10));  /* MASK IRQ using PIC */
        else
            outp(0x21, (inp(0x21) | 0x08));  /* MASK IRQ using PIC */

        platform.com_port = 0;
        platform.com_int_vect = 0;
        platform.com_int = 0;
    }

    GCF_HandleEvent(platform.gcf, EV_DISCONNECTED);
}

/*! Shuts down platform layer (ends main loop). */
void PL_ShutDown(void)
{
    platform.running = 0;
}

/*! Executes a MCU reset for ConBee I via FTDI CBUS0 reset. */
int PL_ResetFTDI(int num, const char *serialnum)
{
    return -1;
}

/*! Executes a MCU reset for RaspBee I / II via GPIO17 reset pin. */
int PL_ResetRaspBee(void)
{
    return -1;
}

int PL_ReadFile(const char *path, unsigned char *buf, unsigned long buflen)
{
    int result = -1;

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
//        return;
    }
#else
    (void)level;
#endif
    va_list args;
    va_start (args, format);
    vprintf(format, args);
    va_end (args);
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
    int n;
    n = 0;

    if (platform.com_port == 0)
        return 0;

    gcfDebugHex(platform.gcf, "send", data, len);

    for (; len != 0; len--)
    {
        outp(platform.com_port, (int)*data);
        data++;
        n++;
    }

    return n;
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

void (__interrupt __far *prev_int_1c)();

void __interrupt __far timer_rtn()
{
    platform.time += (1000/18); /* 18.2 per second pulses */
    _chain_intr( prev_int_1c );
}

static void PL_Loop(GCF *gcf)
{
    unsigned pos;
    unsigned char buf[128];
    platform.gcf = gcf;
    platform.running = 1;

    /* hook up timer */
    prev_int_1c = _dos_getvect( 0x1c );
    _dos_setvect( 0x1c, timer_rtn );

    GCF_HandleEvent(gcf, EV_PL_STARTED);

    while (platform.running)
    {
        outp(platform.com_port + 1 , 0);        /* Turn off interrupts */
        if (rx_count)
        {
            PL_Printf(DBG_INFO, "rx count: %d\n", rx_count);
            rx_count = 0;
        }
        for (pos = 0; platform.rx_head != platform.rx_tail && pos < sizeof(buf); pos++)
        {
            buf[pos] = platform.rx_buf[platform.rx_head];
            platform.rx_head = (platform.rx_head + 1) & sizeof(platform.rx_buf) - 1;
        }
        outp(platform.com_port + 1 , 1);        /* Turn on interrupts */

        if (pos)
        {
            GCF_Received(gcf, buf, pos);
        }

        if (platform.timer != 0)
        {
            if (platform.timer < PL_Time())
            {
                platform.timer = 0;
                GCF_HandleEvent(gcf, EV_TIMEOUT);
            }
        }

        delay(5);

        if (platform.time > 30000)
            platform.running = 0;
    }

    PL_Disconnect();

    _dos_setvect( 0x1c, prev_int_1c );
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
