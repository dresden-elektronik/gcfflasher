/*
 * Copyright (c) 2023 Manuel Pietschmann.
 * All rights reserved.
 *
 * The software in this package is published under the terms of the BSD
 * style license a copy of which has been included with this distribution in
 * the LICENSE.txt file.
 *
 */

#include "u_bstream.h"

void U_bstream_init(U_BStream *bs, void *data, unsigned long size)
{
    bs->data = (unsigned char*)data;
    bs->pos = 0;
    bs->size = size;
    bs->status = U_BSTREAM_OK;
}

static int U_bstream_verify_write(U_BStream *bs, unsigned long size)
{
    if (bs->status != U_BSTREAM_OK)
        return 0;

    if (!bs->data)
    {
        bs->status = U_BSTREAM_NOT_INITIALISED;
        return 0;
    }

    if ((bs->pos + size) > bs->size)
    {
        bs->status = U_BSTREAM_WRITE_PAST_END;
        return 0;
    }

    return 1;
}

static int U_bstream_verify_read(U_BStream *bs, unsigned long size)
{
    if (bs->status != U_BSTREAM_OK)
        return 0;

    if (!bs->data)
    {
        bs->status = U_BSTREAM_NOT_INITIALISED;
        return 0;
    }

    if ((bs->pos + size) > bs->size)
    {
        bs->status = U_BSTREAM_READ_PAST_END;
        return 0;
    }

    return 1;
}

void U_bstream_put_u8(U_BStream *bs, unsigned char v)
{
    if (U_bstream_verify_write(bs, 1))
    {
        bs->data[bs->pos++] = v;
    }
}

void U_bstream_put_u16_le(U_BStream *bs, unsigned short v)
{
    if (U_bstream_verify_write(bs, 2))
    {
        bs->data[bs->pos++] = (v >> 0) & 0xFF;
        bs->data[bs->pos++] = (v >> 8) & 0xFF;
    }
}

void U_bstream_put_u32_le(U_BStream *bs, unsigned long v)
{
    if (U_bstream_verify_write(bs, 4))
    {
        bs->data[bs->pos++] = (v >> 0) & 0xFF;
        bs->data[bs->pos++] = (v >> 8) & 0xFF;
        bs->data[bs->pos++] = (v >> 16) & 0xFF;
        bs->data[bs->pos++] = (v >> 24) & 0xFF;
    }
}

unsigned char U_bstream_get_u8(U_BStream *bs)
{
    unsigned char result;
    result = 0;

    if (U_bstream_verify_read(bs, 1))
    {
        result = bs->data[bs->pos];
        bs->pos++;
    }

    return result;
}

unsigned short U_bstream_get_u16_le(U_BStream *bs)
{
    unsigned short result;
    result = 0;

    if (U_bstream_verify_read(bs, 2))
    {
        result = bs->data[bs->pos + 1];
        result <<= 8;
        result |= bs->data[bs->pos];
        bs->pos += 2;
    }

    return result;
}

unsigned short U_bstream_get_u16_be(U_BStream *bs)
{
    unsigned short result;
    result = 0;

    if (U_bstream_verify_read(bs, 2))
    {
        result = bs->data[bs->pos];
        result <<= 8;
        result |= bs->data[bs->pos + 1];
        bs->pos += 2;
    }

    return result;
}

unsigned long U_bstream_get_u32_le(U_BStream *bs)
{
    unsigned long result;
    result = 0;

    if (U_bstream_verify_read(bs, 4))
    {
        result = bs->data[bs->pos + 3];
        result <<= 8;
        result |= bs->data[bs->pos + 2];
        result <<= 8;
        result |= bs->data[bs->pos + 1];
        result <<= 8;
        result |= bs->data[bs->pos + 0];
        bs->pos += 4;
    }

    return result;
}

unsigned long U_bstream_get_u32_be(U_BStream *bs)
{
    unsigned long result;
    result = 0;

    if (U_bstream_verify_read(bs, 4))
    {
        result = bs->data[bs->pos];
        result <<= 8;
        result |= bs->data[bs->pos + 1];
        result <<= 8;
        result |= bs->data[bs->pos + 2];
        result <<= 8;
        result |= bs->data[bs->pos + 3];
        bs->pos += 4;
    }

    return result;
}
