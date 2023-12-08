/*
 * Copyright (c) 2023 Manuel Pietschmann.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef U_BSTREAM_H
#define U_BSTREAM_H

/* byte stream */

typedef enum
{
    U_BSTREAM_OK,
    U_BSTREAM_READ_PAST_END,
    U_BSTREAM_WRITE_PAST_END,
    U_BSTREAM_NOT_INITIALISED
} U_BStreamStatus;

typedef struct U_BStream
{
    unsigned char *data;
    unsigned long pos;
    unsigned long size;
    U_BStreamStatus status;
} U_BStream;

void U_bstream_init(U_BStream *bs, void *data, unsigned long size);
void U_bstream_put_u8(U_BStream *bs, unsigned char v);
void U_bstream_put_u16_le(U_BStream *bs, unsigned short v);
void U_bstream_put_u32_le(U_BStream *bs, unsigned long v);
unsigned char U_bstream_get_u8(U_BStream *bs);
unsigned short U_bstream_get_u16_le(U_BStream *bs);
unsigned short U_bstream_get_u16_be(U_BStream *bs);
unsigned long U_bstream_get_u32_le(U_BStream *bs);
unsigned long U_bstream_get_u32_be(U_BStream *bs);

/* 64-bit support */
#ifdef __STDC_VERSION__ 
  #if __STDC_VERSION__ >= 199901L
    #define U_BSTREAM_HAS_LONG_LONG
  #endif
#endif

#endif /* U_BSTREAM_H */
