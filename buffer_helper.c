/*
 * Copyright (c) 2021 dresden elektronik ingenieurtechnik gmbh.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "buffer_helper.h"

uint8_t *put_u8_le(uint8_t *out, const uint8_t *in)
{
   *out++ = *in;
   return out;
}

uint8_t *put_u16_le(uint8_t *out, const uint16_t *in)
{
    *out++ = *in & 0x00FF;
    *out++ = (*in & 0xFF00) >> 8;
    return out;
}

uint8_t *put_u32_le(uint8_t *out, const uint32_t *in)
{
    *out++ = (*in & 0x000000FF) >> 0;
    *out++ = (*in & 0x0000FF00) >> 8;
    *out++ = (*in & 0x00FF0000) >> 16;
    *out++ = (*in & 0xFF000000) >> 24;
    return out;
}

uint8_t *put_u64_le(uint8_t *out, const uint64_t *in)
{
    *out++ = (*in & 0x00000000000000FFLLU) >> 0;
    *out++ = (*in & 0x000000000000FF00LLU) >> 8;
    *out++ = (*in & 0x0000000000FF0000LLU) >> 16;
    *out++ = (*in & 0x00000000FF000000LLU) >> 24;
    *out++ = (*in & 0x000000FF00000000LLU) >> 32;
    *out++ = (*in & 0x0000FF0000000000LLU) >> 40;
    *out++ = (*in & 0x00FF000000000000LLU) >> 48;
    *out++ = (*in & 0xFF00000000000000LLU) >> 56;
    return out;
}

const uint8_t *get_u8_le(const uint8_t *in, uint8_t *out)
{
    *out = in[0];
    return in + sizeof(*out);
}

const uint8_t *get_u16_le(const uint8_t *in, uint16_t *out)
{
    *out = in[0];
    *out |= in[1] << 8;
    return in + sizeof(*out);
}

const uint8_t *get_u32_le(const uint8_t *in, uint32_t *out)
{
    *out = in[0];
    *out |= in[1] << 8;
    *out |= in[2] << 16;
    *out |= in[3] << 24;
    return in + sizeof(*out);
}

const uint8_t *get_u64_le(const uint8_t *in, uint64_t *out)
{
    *out =  (uint64_t)in[0];
    *out |= (uint64_t)in[1] << 8;
    *out |= (uint64_t)in[2] << 16ULL;
    *out |= (uint64_t)in[3] << 24ULL;
    *out |= (uint64_t)in[4] << 32ULL;
    *out |= (uint64_t)in[5] << 40ULL;
    *out |= (uint64_t)in[6] << 48ULL;
    *out |= (uint64_t)in[7] << 56ULL;
    return in + sizeof(*out);
}
