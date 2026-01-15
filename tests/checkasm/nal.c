/*
 * Copyright (c) 2024
 *
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
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <string.h>

#include "libavutil/common.h"
#include "libavcodec/defs.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"
#include "libavutil/cpu.h"
#if ARCH_AARCH64
#include "libavutil/aarch64/cpu.h"
#endif

#include "checkasm.h"

#include "libavutil/nal.h"

#define BUF_SIZE (8192 + AV_INPUT_BUFFER_PADDING_SIZE)

void checkasm_check_nal(void)
{
    LOCAL_ALIGNED_8(uint8_t, buf, [BUF_SIZE]);
    const uint8_t *ref_res, *new_res;

#if ARCH_AARCH64
    int cpu_flags = av_get_cpu_flags();
    int have_neon_impl = have_neon(cpu_flags);

    if (have_neon_impl) {
        declare_func(const uint8_t *, const uint8_t *p, const uint8_t *end);

        // Set C version as reference implementation
        func_ref = ff_nal_find_startcode_c;

        // Test 1: Startcode at beginning
        memset(buf, 0xFF, BUF_SIZE);
        AV_WN32A(buf, 0x01000000);
        if (check_func(ff_nal_find_startcode_neon, "startcode_at_beginning")) {
            ref_res = call_ref(buf, buf + 8192);
            new_res = call_new(buf, buf + 8192);
            if (ref_res != new_res)
                fail();
            bench_new(buf, buf + 8192);
        }

        // Test 2: Startcode at offset 4 (three-byte)
        memset(buf, 0xFF, BUF_SIZE);
        AV_WN32A(buf + 4, 0x010000);
        if (check_func(ff_nal_find_startcode_neon, "startcode_at_offset_4")) {
            ref_res = call_ref(buf, buf + 8192);
            new_res = call_new(buf, buf + 8192);
            if (ref_res != new_res)
                fail();
            bench_new(buf, buf + 8192);
        }

        // Test 3: Multiple startcodes, find first one
        memset(buf, 0, BUF_SIZE);
        AV_WN32A(buf + 100, 0x01000000);
        AV_WN32A(buf + 500, 0x01000000);
        AV_WN32A(buf + 1000, 0x01000000);
        if (check_func(ff_nal_find_startcode_neon, "multiple_startcodes")) {
            ref_res = call_ref(buf, buf + 8192);
            new_res = call_new(buf, buf + 8192);
            if (ref_res != new_res)
                fail();
            bench_new(buf, buf + 8192);
        }

        // Test 4: No startcode (all 0xFF) - CRITICAL TEST
        memset(buf, 0xFF, 256);
        memset(buf + 256, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        if (check_func(ff_nal_find_startcode_neon, "no_startcode_0xFF")) {
            ref_res = call_ref(buf, buf + 256);
            new_res = call_new(buf, buf + 256);
            if (ref_res != new_res)
                fail();
            bench_new(buf, buf + 256);
        }

        // Test 5: No startcode (all zeros)
        memset(buf, 0, 256);
        memset(buf + 256, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        if (check_func(ff_nal_find_startcode_neon, "no_startcode_zeros")) {
            ref_res = call_ref(buf, buf + 256);
            new_res = call_new(buf, buf + 256);
            if (ref_res != new_res)
                fail();
            bench_new(buf, buf + 256);
        }

        // Test 6: Startcode near end
        memset(buf, 0xFF, BUF_SIZE);
        AV_WN32A(buf + 8188, 0x01000000);
        if (check_func(ff_nal_find_startcode_neon, "startcode_near_end")) {
            ref_res = call_ref(buf, buf + 8192);
            new_res = call_new(buf, buf + 8192);
            if (ref_res != new_res)
                fail();
            bench_new(buf, buf + 8192);
        }

        // Test 7: Search from middle
        memset(buf, 0, BUF_SIZE);
        AV_WN32A(buf + 100, 0x01000000);
        AV_WN32A(buf + 500, 0x01000000);
        if (check_func(ff_nal_find_startcode_neon, "search_from_middle")) {
            ref_res = call_ref(buf + 200, buf + 8192);
            new_res = call_new(buf + 200, buf + 8192);
            if (ref_res != new_res)
                fail();
            bench_new(buf + 200, buf + 8192);
        }

        // Test 8: Small buffer (16 bytes)
        memset(buf, 0xFF, 16);
        memset(buf + 16, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        if (check_func(ff_nal_find_startcode_neon, "small_buffer_16")) {
            ref_res = call_ref(buf, buf + 16);
            new_res = call_new(buf, buf + 16);
            if (ref_res != new_res)
                fail();
            bench_new(buf, buf + 16);
        }

        // Test 9: Very small buffer (4 bytes)
        memset(buf, 0xFF, 4);
        memset(buf + 4, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        if (check_func(ff_nal_find_startcode_neon, "tiny_buffer_4")) {
            ref_res = call_ref(buf, buf + 4);
            new_res = call_new(buf, buf + 4);
            if (ref_res != new_res)
                fail();
            bench_new(buf, buf + 4);
        }

        // Test 10: Three-byte startcode pattern
        memset(buf, 0xFF, BUF_SIZE);
        buf[50] = 0x00;
        buf[51] = 0x00;
        buf[52] = 0x01;
        if (check_func(ff_nal_find_startcode_neon, "three_byte_startcode")) {
            ref_res = call_ref(buf, buf + 8192);
            new_res = call_new(buf, buf + 8192);
            if (ref_res != new_res)
                fail();
            bench_new(buf, buf + 8192);
        }

        // Test 11: Random data with startcode
        for (int i = 0; i < 8192; i++) {
            buf[i] = rnd() & 0xFF;
        }
        memset(buf + 8192, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        int pos = (rnd() & 0x1FFF) + 100;
        AV_WN32A(buf + pos, 0x01000000);
        if (check_func(ff_nal_find_startcode_neon, "random_with_startcode")) {
            ref_res = call_ref(buf, buf + 8192);
            new_res = call_new(buf, buf + 8192);
            if (ref_res != new_res)
                fail();
            bench_new(buf, buf + 8192);
        }

        // Test 12: Large buffer with no startcode
        memset(buf, 0xAA, 8192);
        memset(buf + 8192, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        if (check_func(ff_nal_find_startcode_neon, "large_no_startcode")) {
            ref_res = call_ref(buf, buf + 8192);
            new_res = call_new(buf, buf + 8192);
            if (ref_res != new_res)
                fail();
            bench_new(buf, buf + 8192);
        }
    }
#endif

    report("nal");
}
