/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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
 * @ingroup lavu_crc32
 * Public header for CRC hash function implementation.
 */

#ifndef AVUTIL_CRC_H
#define AVUTIL_CRC_H

#include <stdint.h>
#include <stddef.h>
#include "attributes.h"

/**
 * @defgroup lavu_crc32 CRC
 * @ingroup lavu_hash
 * CRC (Cyclic Redundancy Check) hash function implementation.
 *
 * This module supports numerous CRC polynomials, in addition to the most
 * widely used CRC-32-IEEE. See @ref AVCRCId for a list of available
 * polynomials.
 *
 * @{
 */

typedef uint32_t AVCRC;

typedef enum {
    AV_CRC_8_ATM,
    AV_CRC_16_ANSI,
    AV_CRC_16_CCITT,
    AV_CRC_32_IEEE,
    AV_CRC_32_IEEE_LE,  /*< reversed bitorder version of AV_CRC_32_IEEE */
    AV_CRC_16_ANSI_LE,  /*< reversed bitorder version of AV_CRC_16_ANSI */
    AV_CRC_24_IEEE,
    AV_CRC_8_EBU,
    AV_CRC_MAX,         /*< Not part of public API! Do not use outside libavutil. */
}AVCRCId;

/**
 * Initialize a CRC table.
 * @param ctx must be an array of size sizeof(AVCRC)*257 or sizeof(AVCRC)*1024
 * @param le If 1, the lowest bit represents the coefficient for the highest
 *           exponent of the corresponding polynomial (both for poly and
 *           actual CRC).
 *           If 0, you must swap the CRC parameter and the result of av_crc
 *           if you need the standard representation (can be simplified in
 *           most cases to e.g. bswap16):
 *           av_bswap32(crc << (32-bits))
 * @param bits number of bits for the CRC
 * @param poly generator polynomial without the x**bits coefficient, in the
 *             representation as specified by le
 * @param ctx_size size of ctx in bytes
 * @return <0 on failure
 */
int av_crc_init(AVCRC *ctx, int le, int bits, uint32_t poly, int ctx_size);

/**
 * Get an initialized standard CRC table.
 * @param crc_id ID of a standard CRC
 * @return a pointer to the CRC table or NULL on failure
 */
const AVCRC *av_crc_get_table(AVCRCId crc_id);

/**
 * Calculate the CRC of a block.
 * @param ctx initialized AVCRC array (see av_crc_init())
 * @param crc CRC of previous blocks if any or initial value for CRC
 * @param buffer buffer whose CRC to calculate
 * @param length length of the buffer
 * @return CRC updated with the data from the given block
 *
x * @see av_crc_init() "le" parameter
 */
uint32_t av_crc(const AVCRC *ctx, uint32_t crc,
                const uint8_t *buffer, size_t length) av_pure;

/**
 * Function pointer to a function to perform a round of CRC calculations on a buffer.
 *
 * @note Using a different context than the one allocated during av_crc2_init()
 * is not allowed.
 *
 * @param ctx the transform context
 * @param crc the current CRC state
 * @param buffer the buffer on which to perform CRC
 * @param length the length of the buffer
 *
 * The buffer array must be aligned to the maximum required by the CPU
 * architecture unless the AV_CRC_UNALIGNED flag was set in av_crc2_init().
 */
typedef uint64_t (*av_crc_fn)(const uint8_t *ctx, uint64_t crc,
                              const uint8_t *buffer, size_t length);

/**
 * Get the parameters of a common CRC algorithm.
 */
void av_crc_preset(AVCRCId crc, uint64_t *poly, int *bits, int *le);

enum AVCRCFlags {
    /**
     * Specifies that the pointer to perform the CRC on is not guaranteed to be aligned.
     */
    AV_CRC_FLAG_UNALIGNED = 1 << 0,

    /**
     * The lowest bit represents the coefficient for the highest
     * exponent of the corresponding polynomial (both for poly and actual CRC).
     * If set, you must bitswap the CRC parameter and the result of av_crc_fn
     * if you need the standard representation (can be simplified in
     * most cases to e.g. bswap16):
     * av_bswap32(crc << (32-bits))
     */
    AV_CRC_FLAG_LE = 1 << 1,
};

/**
 * Initialize a context and a function pointer for a CRC algorithm.
 *
 * @param ctx a pointer to where a CRC context will be set
 *            NOTE: must be freed using av_free()
 * @param fn a function pointer where the function to perform a CRC will be set
 * @param bits the length of the polynomial
 * @param poly the polynomial for the CRC
 * @param flags the set of flags to use
 */
int av_crc2_init(uint8_t **ctx, av_crc_fn *fn,
                 uint64_t poly, int bits, enum AVCRCFlags flags);

/**
 * Convenience wrapper function to perform a well-known CRC algorithm on a function.
 * Guaranteed to not require new allocations.
 */
uint64_t av_crc_calc(AVCRCId crc_id, uint64_t crc, uint8_t *buffer, size_t length);

/**
 * @}
 */

#endif /* AVUTIL_CRC_H */
