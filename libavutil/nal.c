/*
 * NAL utility functions
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>
#include <string.h>

#include "config.h"
#include "attributes.h"
#include "cpu.h"
#include "thread.h"
#if ARCH_AARCH64
#include "aarch64/cpu.h"
#endif
#include "nal.h"

const uint8_t *ff_nal_find_startcode_c(const uint8_t *p, const uint8_t *end)
{
    const uint8_t *a = p + 4 - ((intptr_t)p & 3);

    for (end -= 3; p < a && p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    for (end -= 3; p < end; p += 4) {
        uint32_t x = *(const uint32_t*)p;
//      if ((x - 0x01000100) & (~x) & 0x80008000) // little endian
//      if ((x - 0x00010001) & (~x) & 0x00800080) // big endian
        if ((x - 0x01010101) & (~x) & 0x80808080) { // generic
            if (p[1] == 0) {
                if (p[0] == 0 && p[2] == 1)
                    return p;
                if (p[2] == 0 && p[3] == 1)
                    return p+1;
            }
            if (p[3] == 0) {
                if (p[2] == 0 && p[4] == 1)
                    return p+2;
                if (p[4] == 0 && p[5] == 1)
                    return p+3;
            }
        }
    }

    for (end += 3; p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    return end + 3;
}

// Function pointer to the active implementation
static const uint8_t *(*nal_find_startcode_func)(const uint8_t *p, const uint8_t *end);

// Thread-safe initialization using ff_thread_once
static AVOnce nal_func_init_once = AV_ONCE_INIT;

static void nal_find_startcode_init(void)
{
#if ARCH_AARCH64
    int cpu_flags = av_get_cpu_flags();
    if (have_neon(cpu_flags))
        nal_find_startcode_func = ff_nal_find_startcode_neon;
    else
        nal_find_startcode_func = ff_nal_find_startcode_c;
#else
    nal_find_startcode_func = ff_nal_find_startcode_c;
#endif
}

const uint8_t *av_nal_find_startcode(const uint8_t *p, const uint8_t *end)
{
    // Initialize function pointer on first call (thread-safe)
    ff_thread_once(&nal_func_init_once, nal_find_startcode_init);

    // Call the optimized implementation
    p = nal_find_startcode_func(p, end);

    if (p < end && !p[-1])
        p--;
    return p;
}
