/*
 * Copyright (c) 2021 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#ifndef BUF_HELPER_H
#define BUF_HELPER_H

#include <stdint.h>

uint8_t *put_u8_le(uint8_t *out, const uint8_t *in);
uint8_t *put_u16_le(uint8_t *out, const uint16_t *in);
uint8_t *put_u32_le(uint8_t *out, const uint32_t *in);
uint8_t *put_u64_le(uint8_t *out, const uint64_t *in);
const uint8_t *get_u8_le(const uint8_t *in, uint8_t *out);
const uint8_t *get_u16_le(const uint8_t *in, uint16_t *out);
const uint8_t *get_u32_le(const uint8_t *in, uint32_t *out);
const uint8_t *get_u64_le(const uint8_t *in, uint64_t *out);

#endif /* BUF_HELPER_H */
