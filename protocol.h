/*
 * Copyright (c) 2021-2026 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

typedef struct {
    unsigned bufpos;
    unsigned char escaped;
    unsigned char buf[256];
} PROT_RxState;

/* Platform independent declarations. */
void PROT_SendFlagged(const unsigned char *data, unsigned len);
/* Returns >0 on CRC errors. */
int PROT_ReceiveFlagged(PROT_RxState *rx, const unsigned char *data, unsigned len);
void PROT_Packet(const unsigned char *data, unsigned len);

/*! Platform specific declarations.
    Following functions need to be implemented in the platform layer.
 */
int PROT_Write(const unsigned char *data, unsigned len);
int PROT_Putc(unsigned char ch);
int PROT_Flush(void);


#endif /* PROTOCOL_H */
