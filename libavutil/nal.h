/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_NAL_H
#define AVUTIL_NAL_H

#include <stdint.h>

/**
 * @file
 * @ingroup lavu_misc
 * NAL (Network Abstraction Layer) utility functions
 */

/**
 * @addtogroup lavu_misc
 * @{
 */

/**
 * Find a H.264/H.265 NAL startcode (00 00 01 or 00 00 00 01) in a buffer.
 *
 * @param p    Pointer to start searching from
 * @param end  Pointer to end of buffer (must have at least 3 bytes padding)
 * @return     Pointer to startcode, or end+3 if not found
 *
 * @note This function searches for the pattern 00 00 01 (three-byte startcode)
 *       or 00 00 00 01 (four-byte startcode). When found, it returns a pointer
 *       to the startcode. If the byte before the startcode is also 0, it returns
 *       that position instead (to handle the four-byte case).
 *       If no startcode is found, returns end + 3.
 */
const uint8_t *av_nal_find_startcode(const uint8_t *p, const uint8_t *end);

/* Internal implementations exposed for testing */
const uint8_t *ff_nal_find_startcode_c(const uint8_t *p, const uint8_t *end);
#if ARCH_AARCH64
const uint8_t *ff_nal_find_startcode_neon(const uint8_t *p, const uint8_t *end);
#endif

/**
 * @}
 */

#endif /* AVUTIL_NAL_H */
