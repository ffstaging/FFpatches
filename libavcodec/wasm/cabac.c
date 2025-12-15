/*
 * CABAC helpers for WebAssembly
 *
 * Copyright (c) 2025 Xia Tao
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "libavcodec/wasm/cabac.h"
#include "libavcodec/cabac_functions.h"
#include <stdint.h>
#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"

#ifndef UNCHECKED_BITSTREAM_READER
#define UNCHECKED_BITSTREAM_READER !CONFIG_SAFE_BITSTREAM_READER
#endif

static av_always_inline int get_cabac_core_wasm(CABACContext *c, uint8_t *state)
{
    int s     = *state;
    int range = c->range;
    int low   = c->low;
    const uint8_t *lps_range  = ff_h264_cabac_tables + H264_LPS_RANGE_OFFSET;
    const uint8_t *mlps_state = ff_h264_cabac_tables + H264_MLPS_STATE_OFFSET;
    const uint8_t *norm_shift = ff_h264_cabac_tables + H264_NORM_SHIFT_OFFSET;
    int RangeLPS = lps_range[2 * (range & 0xC0) + s];
    int lps_mask;
    int bit;

    {
        int range_mps = range - RangeLPS;
        int thresh    = range_mps << (CABAC_BITS + 1);
        int is_lps    = low > thresh;

        lps_mask = -is_lps;

        low   = low - (thresh & lps_mask);
        range = range_mps + ((RangeLPS - range_mps) & lps_mask);
    }

    s ^= lps_mask;
    *state = (mlps_state + 128)[s];
    bit    = s & 1;

    lps_mask = norm_shift[range];
    range  <<= lps_mask;
    low    <<= lps_mask;

    c->range = range;
    c->low   = low;

    if (!(low & CABAC_MASK))
        refill2(c);

    return bit;
}

int ff_get_cabac_wasm(CABACContext *c, uint8_t *state)
{
    return get_cabac_core_wasm(c, state);
}

static av_always_inline int get_cabac_bypass_core_wasm(CABACContext *c)
{
    c->low += c->low;

    if (!(c->low & CABAC_MASK))
        refill(c);

    {
        int range_shifted = c->range << (CABAC_BITS + 1);
        int low           = c->low;
        int bit           = low >= range_shifted;

        if (bit)
            low -= range_shifted;

        c->low = low;
        return bit;
    }
}

int ff_get_cabac_bypass_wasm(CABACContext *c)
{
    return get_cabac_bypass_core_wasm(c);
}

static av_always_inline int get_cabac_bypass_sign_core_wasm(CABACContext *c, int val)
{
    c->low += c->low;

    if (!(c->low & CABAC_MASK))
        refill(c);

    {
        int range_shifted = c->range << (CABAC_BITS + 1);
        int low           = c->low;
        int bit           = low >= range_shifted;

        if (bit)
            low -= range_shifted;

        c->low = low;

        return bit ? val : -val;
    }
}

int ff_get_cabac_bypass_sign_wasm(CABACContext *c, int val)
{
    return get_cabac_bypass_sign_core_wasm(c, val);
}
