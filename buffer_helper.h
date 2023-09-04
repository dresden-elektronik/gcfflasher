/*
 * Copyright (c) 2021-2023 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef BUF_HELPER_H
#define BUF_HELPER_H

unsigned char *put_u8_le(unsigned char *out, const unsigned char *in);
unsigned char *put_u16_le(unsigned char *out, const unsigned short *in);
unsigned char *put_u32_le(unsigned char *out, const unsigned long *in);
/*unsigned char *put_u64_le(unsigned char *out, const uint64_t *in);*/
const unsigned char *get_u8_le(const unsigned char *in, unsigned char *out);
const unsigned char *get_u16_le(const unsigned char *in, unsigned short *out);
const unsigned char *get_u32_le(const unsigned char *in, unsigned long *out);
/*const unsigned char *get_u64_le(const unsigned char *in, uint64_t *out);*/

#endif /* BUF_HELPER_H */
