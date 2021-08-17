/*
 * Copyright (c) 2021 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

typedef struct {
    uint16_t bufpos;
    uint16_t crc;
    uint8_t escaped;
    uint8_t buf[256];
} PROT_RxState;

/* Platform independent declarations. */
void PROT_SendFlagged(const uint8_t *data, uint16_t len);
void PROT_ReceiveFlagged(PROT_RxState *rx, const uint8_t *data, uint16_t len);
void PROT_Packet(const uint8_t *data, uint16_t len);

/*! Platform specific declarations.
    Following functions need to be implemented in the platform layer.
 */
int PROT_Write(const uint8_t *data, uint16_t len);
int PROT_Putc(uint8_t ch);
int PROT_Flush();


#endif /* PROTOCOL_H */
