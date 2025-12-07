/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stddef.h>
#include <stdint.h>


#include "checkasm.h"
#include "libavutil/attributes.h"
// Undefine av_pure so that calls to av_crc are not optimized away.
#undef av_pure
#define av_pure
#include "libavutil/crc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"


void checkasm_check_crc(void)
{
    declare_func(uint32_t, const AVCRC *ctx, uint32_t crc,
                 const uint8_t *buffer, size_t length);

    for (unsigned i = 0; i < AV_CRC_MAX; ++i) {
        const AVCRC *table_new = av_crc_get_table(i);
        const AVCRC *table_ref;

        if (table_ref = check_opaque((AVCRC*)table_new, "crc_%u", i)) {
            DECLARE_ALIGNED(4, uint8_t, buf)[8192];
            size_t offset = rnd() & 31;
            static size_t sizes[AV_CRC_MAX];
            static unsigned sizes_initialized = 0;
            uint32_t prev_crc = rnd();

            if (!(sizes_initialized & (1 << i))) {
                sizes_initialized |= 1 << i;
                sizes[i] = rnd() % (sizeof(buf) - 1 - offset);
            }

            size_t size = sizes[i];

            for (size_t j = 0; j < sizeof(buf); j += 4)
                AV_WN32A(buf + j, rnd());

            uint32_t crc_ref = call_ref_ext(av_crc, table_ref, prev_crc, buf + offset, size);
            uint32_t crc_new = call_new_ext(av_crc, table_new, prev_crc, buf + offset, size);

            if (crc_ref != crc_new)
                fail();

            bench(av_crc, table_new, prev_crc, buf + offset, size);
        }
    }
}
