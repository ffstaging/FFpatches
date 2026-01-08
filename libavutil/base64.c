/*
 * Copyright (c) 2006 Ryan Martell. (rdm4@martellventures.com)
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

/**
 * @file
 * @brief Base64 encode/decode
 * @author Ryan Martell <rdm4@martellventures.com> (with lots of Michael)
 *
 * This is a drop-in compatible implementation of FFmpeg's base64 helpers.
 * The decode routine preserves FFmpeg's historical semantics (strict input,
 * stops at the first invalid character, supports unpadded input).
 *
 * Small performance-oriented changes were made to the encoder:
 *   - The slow "shift loop" tail handling was replaced by a constant-time
 *     switch on the remaining 1 or 2 bytes, reducing branches and shifts.
 *   - The main loop now packs 3 bytes into a 24-bit value directly instead of
 *     reading an overlapping 32-bit word (avoids endian conversions and makes
 *     the loop easier for compilers to optimize).
 *
 * The API and output are fully compatible with the original code.
 */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "base64.h"
#include "error.h"
#include "intreadwrite.h"

/* ---------------- private code
 *
 * map2[c] returns:
 *   - 0..63  : decoded 6-bit value for valid Base64 symbols
 *   - 0xFE   : "stop" symbol (NUL terminator and '=' padding)
 *   - 0xFF   : invalid symbol (produces AVERROR_INVALIDDATA)
 *
 * The decoder uses:
 *   - bits & 0x80 to detect "stop/invalid" quickly (both 0xFE and 0xFF have MSB set)
 *   - bits & 1 to distinguish invalid (0xFF) from stop (0xFE)
 */
static const uint8_t map2[256] =
{
    0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff,

    0x3e, 0xff, 0xff, 0xff, 0x3f, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0xff,
    0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0x00, 0x01,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1a, 0x1b,
    0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
    0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33,

                      0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

#define BASE64_DEC_STEP(i) do { \
    bits = map2[in[i]];         \
    if (bits & 0x80)            \
        goto out ## i;          \
    v = (i) ? (v << 6) + bits : bits; \
} while (0)

int av_base64_decode(uint8_t *out, const char *in_str, int out_size)
{
    uint8_t *dst = out;
    uint8_t *end;
    /* Cast to unsigned to avoid sign extension on platforms where char is signed. */
    const uint8_t *in = (const uint8_t *)in_str;
    unsigned bits = 0xff;
    unsigned v;

    /* Validation-only mode: keep FFmpeg's original behavior. */
    if (!out)
        goto validity_check;

    end = out + out_size;

    /*
     * Fast path: decode complete 4-char blocks while we can safely do a 32-bit store.
     * We write 4 bytes and advance by 3 (the 4th written byte is overwritten on the next iteration).
     */
    while (end - dst > 3) {
        BASE64_DEC_STEP(0);
        BASE64_DEC_STEP(1);
        BASE64_DEC_STEP(2);
        BASE64_DEC_STEP(3);

        /* Convert to native-endian so a native write yields correct byte order in memory. */
        v = av_be2ne32(v << 8);
        AV_WN32(dst, v);

        dst += 3;
        in  += 4;
    }

    /* Tail: decode at most one more block without overrunning the output buffer. */
    if (end - dst) {
        BASE64_DEC_STEP(0);
        BASE64_DEC_STEP(1);
        BASE64_DEC_STEP(2);
        BASE64_DEC_STEP(3);

        *dst++ = v >> 16;
        if (end - dst)
            *dst++ = v >> 8;
        if (end - dst)
            *dst++ = v;

        in += 4;
    }

validity_check:
    /*
     * Strict validation: keep decoding groups of 4 until we hit the first stop/invalid.
     * Using BASE64_DEC_STEP(0) ensures we always jump to out0 and never touch out1/out2/out3
     * (important for the out == NULL validation-only mode).
     */
    while (1) {
        BASE64_DEC_STEP(0); in++;
        BASE64_DEC_STEP(0); in++;
        BASE64_DEC_STEP(0); in++;
        BASE64_DEC_STEP(0); in++;
    }

out3:
    if (end - dst)
        *dst++ = v >> 10;
    v <<= 2;
out2:
    if (end - dst)
        *dst++ = v >> 4;
out1:
out0:
    /* bits==0xFE => stop (NUL or '=') => success. bits==0xFF => invalid => error. */
    return (bits & 1) ? AVERROR_INVALIDDATA : (out ? (int)(dst - out) : 0);
}

/*****************************************************************************
 * b64_encode: Stolen from VLC's http.c.
 * Simplified by Michael.
 * Fixed edge cases and made it work from data (vs. strings) by Ryan.
 *
 * Encoder micro-optimizations:
 *   - Direct 24-bit packing (3 bytes -> 4 symbols) in the main loop.
 *   - Branchless tail handling via a small switch for 1 or 2 remaining bytes.
 *****************************************************************************/

char *av_base64_encode(char *out, int out_size, const uint8_t *in, int in_size)
{
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *ret, *dst;

    if (in_size >= (int)(UINT_MAX / 4) || out_size < AV_BASE64_SIZE(in_size))
        return NULL;

    ret = dst = out;

    /* Encode full 3-byte blocks. */
    while (in_size >= 3) {
        uint32_t v = ((uint32_t)in[0] << 16) |
                     ((uint32_t)in[1] <<  8) |
                     ((uint32_t)in[2]      );
        in += 3;
        in_size -= 3;

        dst[0] = b64[ (v >> 18)        ];
        dst[1] = b64[ (v >> 12) & 0x3F ];
        dst[2] = b64[ (v >>  6) & 0x3F ];
        dst[3] = b64[ (v      ) & 0x3F ];
        dst += 4;
    }

    /* Encode the remaining 1 or 2 bytes (if any) and add '=' padding. */
    if (in_size == 1) {
        uint32_t v = (uint32_t)in[0];
        dst[0] = b64[(v >> 2) & 0x3F];
        dst[1] = b64[(v & 0x03) << 4];
        dst[2] = '=';
        dst[3] = '=';
        dst += 4;
    } else if (in_size == 2) {
        uint32_t v = ((uint32_t)in[0] << 8) | (uint32_t)in[1];
        dst[0] = b64[(v >> 10) & 0x3F];
        dst[1] = b64[(v >>  4) & 0x3F];
        dst[2] = b64[(v & 0x0F) << 2];
        dst[3] = '=';
        dst += 4;
    }

    /* NUL-terminate. The caller guaranteed enough space via AV_BASE64_SIZE(). */
    *dst = '\0';
    return ret;
}
