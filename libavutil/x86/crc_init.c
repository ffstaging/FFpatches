/*
 * Copyright (c) 2025 Shreesh Adiga <16567adigashreesh@gmail.com>
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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/cpu.h"
#include "libavutil/crc.h"
#include "libavutil/thread.h"
#include "libavutil/x86/cpu.h"
#include "libavutil/x86/crc.h"

static int av_crc_init_clmul(AVCRC *ctx, int le, int bits, uint32_t poly, int ctx_size);
#if CONFIG_HARDCODED_TABLES
static const AVCRC av_crc_table_clmul[AV_CRC_MAX][16] = {
    [AV_CRC_8_ATM] = {
        0x32000000, 0x0, 0xbc000000, 0x0,
        0xc4000000, 0x0, 0x94000000, 0x0,
        0x62000000, 0x0, 0x79000000, 0x0,
        0x07156a16, 0x1, 0x07000000, 0x1,
    },
    [AV_CRC_8_EBU] = {
        0xb5000000, 0x0, 0xf3000000, 0x0,
        0xfc000000, 0x0, 0x0d000000, 0x0,
        0x6a000000, 0x0, 0x65000000, 0x0,
        0x1c4b8192, 0x1, 0x1d000000, 0x1,
    },
    [AV_CRC_16_ANSI] = {
        0xf9e30000, 0x0, 0x807d0000, 0x0,
        0xf9130000, 0x0, 0xff830000, 0x0,
        0x807b0000, 0x0, 0x86630000, 0x0,
        0xfffbffe7, 0x1, 0x80050000, 0x1,
    },
    [AV_CRC_16_CCITT] = {
        0x60190000, 0x0, 0x59b00000, 0x0,
        0xd5f60000, 0x0, 0x45630000, 0x0,
        0xaa510000, 0x0, 0xeb230000, 0x0,
        0x11303471, 0x1, 0x10210000, 0x1,
    },
    [AV_CRC_24_IEEE] = {
        0x1f428700, 0x0, 0x467d2400, 0x0,
        0x2c8c9d00, 0x0, 0x64e4d700, 0x0,
        0xd9fe8c00, 0x0, 0xfd7e0c00, 0x0,
        0xf845fe24, 0x1, 0x864cfb00, 0x1,
    },
    [AV_CRC_32_IEEE] = {
        0x8833794c, 0x0, 0xe6228b11, 0x0,
        0xc5b9cd4c, 0x0, 0xe8a45605, 0x0,
        0x490d678d, 0x0, 0xf200aa66, 0x0,
        0x04d101df, 0x1, 0x04c11db7, 0x1,
    },
    [AV_CRC_32_IEEE_LE] = {
        0xc6e41596, 0x1, 0x54442bd4, 0x1,
        0xccaa009e, 0x0, 0x751997d0, 0x1,
        0xccaa009e, 0x0, 0x63cd6124, 0x1,
        0xf7011640, 0x1, 0xdb710641, 0x1,
    },
    [AV_CRC_16_ANSI_LE] = {
        0x0000bffa, 0x0, 0x1b0c2, 0x0,
        0x00018cc2, 0x0, 0x1d0c2, 0x0,
        0x00018cc2, 0x0, 0x1bc02, 0x0,
        0xcfffbffe, 0x1, 0x14003, 0x0,
    },
};
#else
static AVCRC av_crc_table_clmul[AV_CRC_MAX][16];

#define DECLARE_CRC_INIT_TABLE_ONCE(id, le, bits, poly)                                                         \
static AVOnce id ## _once_control = AV_ONCE_INIT;                                                               \
static void id ## _init_table_once(void)                                                                        \
{                                                                                                               \
    av_assert0(av_crc_init_clmul(av_crc_table_clmul[id], le, bits, poly, sizeof(av_crc_table_clmul[id])) >= 0); \
}

#define CRC_INIT_TABLE_ONCE(id) ff_thread_once(&id ## _once_control, id ## _init_table_once)

DECLARE_CRC_INIT_TABLE_ONCE(AV_CRC_8_ATM,      0,  8,       0x07)
DECLARE_CRC_INIT_TABLE_ONCE(AV_CRC_8_EBU,      0,  8,       0x1D)
DECLARE_CRC_INIT_TABLE_ONCE(AV_CRC_16_ANSI,    0, 16,     0x8005)
DECLARE_CRC_INIT_TABLE_ONCE(AV_CRC_16_CCITT,   0, 16,     0x1021)
DECLARE_CRC_INIT_TABLE_ONCE(AV_CRC_24_IEEE,    0, 24,   0x864CFB)
DECLARE_CRC_INIT_TABLE_ONCE(AV_CRC_32_IEEE,    0, 32, 0x04C11DB7)
DECLARE_CRC_INIT_TABLE_ONCE(AV_CRC_32_IEEE_LE, 1, 32, 0xEDB88320)
DECLARE_CRC_INIT_TABLE_ONCE(AV_CRC_16_ANSI_LE, 1, 16,     0xA001)
#endif

static uint32_t av_crc_clmul(const AVCRC *ctx, uint32_t crc, const uint8_t *buffer, size_t length)
{
    if (ctx[4] == ctx[8]) {
        return ff_av_crc_le_clmul(ctx, crc, buffer, length);
    } else {
        return ff_av_crc_clmul(ctx, crc, buffer, length);
    }
}

static const AVCRC *av_crc_get_table_clmul(AVCRCId crc_id)
{
#if !CONFIG_HARDCODED_TABLES
    switch (crc_id) {
    case AV_CRC_8_ATM:      CRC_INIT_TABLE_ONCE(AV_CRC_8_ATM); break;
    case AV_CRC_8_EBU:      CRC_INIT_TABLE_ONCE(AV_CRC_8_EBU); break;
    case AV_CRC_16_ANSI:    CRC_INIT_TABLE_ONCE(AV_CRC_16_ANSI); break;
    case AV_CRC_16_CCITT:   CRC_INIT_TABLE_ONCE(AV_CRC_16_CCITT); break;
    case AV_CRC_24_IEEE:    CRC_INIT_TABLE_ONCE(AV_CRC_24_IEEE); break;
    case AV_CRC_32_IEEE:    CRC_INIT_TABLE_ONCE(AV_CRC_32_IEEE); break;
    case AV_CRC_32_IEEE_LE: CRC_INIT_TABLE_ONCE(AV_CRC_32_IEEE_LE); break;
    case AV_CRC_16_ANSI_LE: CRC_INIT_TABLE_ONCE(AV_CRC_16_ANSI_LE); break;
    default: av_assert0(0);
    }
#endif
    return av_crc_table_clmul[crc_id];
}

static uint64_t reverse(uint64_t p, unsigned int deg) {
    uint64_t ret = 0;
    for (int i = 0; i < (deg + 1); i++) {
        ret = (ret << 1) | (p & 1);
        p >>= 1;
    }
    return ret;
}

static uint64_t xnmodp(unsigned n, uint64_t poly, unsigned deg, uint64_t *div, int bitreverse)
{
    uint64_t mod, mask, high;

    if (n < deg) {
        *div = 0;
        return poly;
    }
    mask = ((uint64_t)1 << deg) - 1;
    poly &= mask;
    mod = poly;
    *div = 1;
    deg--;
    while (--n > deg) {
        high = (mod >> deg) & 1;
        *div = (*div << 1) | high;
        mod <<= 1;
        if (high)
            mod ^= poly;
    }
    uint64_t ret = mod & mask;
    if (bitreverse) {
        *div = reverse(*div, deg) << 1;
        return reverse(ret, deg) << 1;
    }
    return ret;
}

static int av_crc_init_clmul(AVCRC *ctx, int le, int bits, uint32_t poly, int ctx_size)
{
    if (bits < 8 || bits > 32 || poly >= (1LL << bits))
        return AVERROR(EINVAL);
    if (ctx_size < sizeof(AVCRC) * 16)
        return AVERROR(EINVAL);

    uint64_t poly_;
    if (le) {
        // convert the reversed representation to regular form
        poly = reverse(poly, bits) >> 1;
    }
    // convert to 32 degree polynomial
    poly_ = ((uint64_t)poly) << (32 - bits);

    uint64_t x1, x2, x3, x4, x5, x6, x7, x8, div;
    if (le) {
        x1 = xnmodp(4 * 128 - 32, poly_, 32, &div, le);
        x2 = xnmodp(4 * 128 + 32, poly_, 32, &div, le);
        x3 = xnmodp(128 - 32, poly_, 32, &div, le);
        x4 = xnmodp(128 + 32, poly_, 32, &div, le);
        x5 = x3;
        x6 = xnmodp(64, poly_, 32, &div, le);
        x7 = div;
        x8 = reverse(poly_ | (1ULL << 32), 32);
    } else {
        x1 = xnmodp(4 * 128 + 64, poly_, 32, &div, le);
        x2 = xnmodp(4 * 128, poly_, 32, &div, le);
        x3 = xnmodp(128 + 64, poly_, 32, &div, le);
        x4 = xnmodp(128, poly_, 32, &div, le);
        x5 = xnmodp(64, poly_, 32, &div, le);
        x7 = div;
        x6 = xnmodp(96, poly_, 32, &div, le);
        x8 = poly_ | (1ULL << 32);
    }
    ctx[0]  = (AVCRC)x1;
    ctx[1]  = (AVCRC)(x1 >> 32);
    ctx[2]  = (AVCRC)x2;
    ctx[3]  = (AVCRC)(x2 >> 32);
    ctx[4]  = (AVCRC)x3;
    ctx[5]  = (AVCRC)(x3 >> 32);
    ctx[6]  = (AVCRC)x4;
    ctx[7]  = (AVCRC)(x4 >> 32);
    ctx[8]  = (AVCRC)x5;
    ctx[9]  = (AVCRC)(x5 >> 32);
    ctx[10] = (AVCRC)x6;
    ctx[11] = (AVCRC)(x6 >> 32);
    ctx[12] = (AVCRC)x7;
    ctx[13] = (AVCRC)(x7 >> 32);
    ctx[14] = (AVCRC)x8;
    ctx[15] = (AVCRC)(x8 >> 32);
    return 0;
}

av_cold void av_crc_init_x86(void)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_CLMUL(cpu_flags)) {
        av_crc_func = av_crc_clmul;
        av_crc_get_table_func = av_crc_get_table_clmul;
        av_crc_init_func = av_crc_init_clmul;
    }
}
