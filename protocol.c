/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "protocol.h"
#include "gcf.h"

#define FR_END       (unsigned char)0xC0
#define FR_ESC       (unsigned char)0xDB
#define T_FR_END     (unsigned char)0xDC
#define T_FR_ESC     (unsigned char)0xDD
#define ASC_FLAG     0x01

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

   c = (~crc + 1) & 0xFF;
   if (c == FR_ESC)
   {
      PROT_Putc(FR_ESC);
      PROT_Putc(T_FR_ESC);
   }
   else if (c == FR_END)
   {
      PROT_Putc(FR_ESC);
      PROT_Putc(T_FR_END);
   }
   else
   {
      PROT_Putc(c);
   }

   c = ( (~crc + 1) >> 8)   & 0xFF;
   if (c == FR_ESC)
   {
      PROT_Putc(FR_ESC);
      PROT_Putc(T_FR_ESC);
   }
   else if (c == FR_END)
   {
      PROT_Putc(FR_ESC);
      PROT_Putc(T_FR_END);
   }
   else
   {
      PROT_Putc(c);
   }

   /* tie off the packet */
   PROT_Putc(FR_END);

   PROT_Flush();
}

void PROT_ReceiveFlagged(PROT_RxState *rx, const unsigned char *data, unsigned len)
{
    unsigned char c;
    unsigned short pos = 0;

   if (len == 0)
   {
       return;
   }

nextTurn:
   while(pos < len)
   {
      c = data[pos];
      pos++;

      switch (c)
      {
      case FR_END:
         if (rx->escaped)
         {
            /* invalid */
            rx->bufpos = 0;
            rx->crc = 0;
         }
         else
         {
            if (rx->bufpos >= 2)
            {
               unsigned char crcvalid = 0;
               /* Checksum bytes are added to the checksum rx->crc - subtract them here */
               rx->crc -= rx->buf[rx->bufpos-1];
               rx->crc -= rx->buf[rx->bufpos-2];
               /* TODO clean this messy condition up */
               if ((((~(rx->crc)+1 ) & 0xFF) == rx->buf[rx->bufpos - 2]) &&
                  ((((~(rx->crc)+1 ) >> 8) & 0xFF) == rx->buf[rx->bufpos - 1]))
               {
                  crcvalid = 1;
               }

               if (crcvalid)
               {
                 PROT_Packet(&rx->buf[0], rx->bufpos - 2);
               }
               else
               {
                   PL_Printf(DBG_DEBUG, "invalid CRC\n");
               }
            }
            rx->bufpos = 0;
            rx->crc = 0;
         }
         rx->escaped &= ~ASC_FLAG;
         goto nextTurn;

      case FR_ESC:
         rx->escaped |= ASC_FLAG;
         goto nextTurn;
      }

      if (rx->escaped & ASC_FLAG)
      {
         /* translate the 2 byte escape sequence back to original char */
         rx->escaped &= ~ASC_FLAG;

         switch (c)
         {
         case T_FR_ESC: c = FR_ESC; break;
         case T_FR_END: c = FR_END; break;
         default: goto nextTurn;
         }
      }

      /* we reach here with every byte for the buffer
         legacy BUG: checksum bytes are added but should not be */
      if (rx->bufpos < sizeof(rx->buf))
      {
         rx->buf[rx->bufpos++] = c;
         rx->crc += c;
      }
   }
}
