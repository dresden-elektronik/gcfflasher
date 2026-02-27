/*
 * Copyright (c) 2021-2026 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "protocol.h"

#define FR_END       (unsigned char)0xC0
#define FR_ESC       (unsigned char)0xDB
#define T_FR_END     (unsigned char)0xDC
#define T_FR_ESC     (unsigned char)0xDD
#define ASC_FLAG     0x01

static void protPutc(unsigned char c)
{
    switch (c)
    {
        case FR_ESC:
            PROT_Putc(FR_ESC);
            PROT_Putc(T_FR_ESC);
            break;
        case FR_END:
            PROT_Putc(FR_ESC);
            PROT_Putc(T_FR_END);
            break;
        default:
            PROT_Putc(c);
            break;
    }
}

void PROT_SendFlagged(const unsigned char *data, unsigned len)
{
    unsigned char c = 0;
    unsigned short i = 0;
    unsigned short crc = 0;

    /* put an end before the packet */
    PROT_Putc(FR_END);

    while (i < len)
    {
        c = data[i++];
        crc += c;
        protPutc(c);
    }

    crc = (~crc + 1);
    protPutc(crc & 0xFF);
    protPutc((crc >> 8) & 0xFF);

    /* tie off the packet */
    PROT_Putc(FR_END);
    PROT_Flush();
}

int PROT_ReceiveFlagged(PROT_RxState *rx, const unsigned char *data, unsigned len)
{
    int err;
    unsigned i;
    unsigned char c;
    unsigned short pos;
    unsigned short crc;
    unsigned short crc1;

    pos = 0;
    err = 0;

nextTurn:
    for (; pos < len; )
    {
        pos++;
        c = data[pos];

        if (c == FR_END)
        {
            if (rx->escaped)
            {
                rx->bufpos = 0; /* invalid */
            }
            else
            {
                if (rx->bufpos > 2)
                {
                    for (crc = 0, i = 0; i < rx->bufpos - 2; i++)
                        crc += rx->buf[i];

                    crc = (~crc + 1);
                    crc1 = (rx->buf[rx->bufpos - 1] << 8) + rx->buf[rx->bufpos - 2];

                    if (crc1 == crc)
                    {
                        PROT_Packet(&rx->buf[0], rx->bufpos - 2);
                    }
                    else
                    {
                        err += 1;
                    }
                }
                rx->bufpos = 0;
            }
            rx->escaped &= ~ASC_FLAG;
            goto nextTurn;
        }
        else if (c == FR_ESC)
        {
            rx->escaped |= ASC_FLAG;
            goto nextTurn;
        }

        if (rx->escaped & ASC_FLAG)
        {
            /* translate the 2-byte escape sequence back to original char */
            rx->escaped &= ~ASC_FLAG;

            switch (c)
            {
                case T_FR_ESC: c = FR_ESC; break;
                case T_FR_END: c = FR_END; break;
                default:
                    rx->bufpos = 0; /* invalid */
            }
        }

        /* we reach here with every byte for the buffer. */
        if (rx->bufpos < sizeof(rx->buf))  /* Prevent buffer overflow by reserving space for CRC */
        {
            rx->buf[rx->bufpos++] = c;
        }
        else
        {
            /* Buffer is full, discard the byte and reset state */
            rx->bufpos = 0;
        }
    }

    return err;
}
