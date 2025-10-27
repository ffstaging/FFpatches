/*
 * Copyright (c) 2025 Makar Kuznietsov
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
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>
#include "checkasm.h"
#include "libavutil/mem_internal.h"
#include "libavutil/cpu.h"

#include "libavfilter/boxblur.h"
#include "libavfilter/vf_boxblur_dsp.h"

static int current_depth = 8;

static void blur8_c(uint8_t *dst, int dst_step, const uint8_t *src,
                    int src_step, int len, int radius)
{
    FFBoxblurDSPContext dsp;
    int saved_flags = av_get_cpu_flags();
    av_force_cpu_flags(saved_flags & ~AV_CPU_FLAG_AVX2);
    ff_boxblur_dsp_init(&dsp, current_depth);
    av_force_cpu_flags(saved_flags);
    ff_boxblur_blur8(dst, dst_step, src, src_step, len, radius, &dsp);
}

static void blur8_simd(uint8_t *dst, int dst_step, const uint8_t *src,
                       int src_step, int len, int radius)
{
    FFBoxblurDSPContext dsp;
    ff_boxblur_dsp_init(&dsp, current_depth);
    ff_boxblur_blur8(dst, dst_step, src, src_step, len, radius, &dsp);
}

static void check_blur8(int depth)
{
    LOCAL_ALIGNED_32(uint8_t, src, [2048]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [2048]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [2048]);

    declare_func(void, uint8_t *, int, const uint8_t *, int, int, int);

    current_depth = depth;

    /* Register exactly one version per CPU run so checkasm records C and AVX2 */
    void (*fn)(uint8_t *, int, const uint8_t *, int, int, int) =
        (av_get_cpu_flags() & AV_CPU_FLAG_AVX2) ? blur8_simd : blur8_c;

    if (check_func(fn, "boxblur_blur8")) {
        for (int iter = 0; iter < 16; iter++) {
            const int len = 64 + (rnd() % 256);
            const int radius = FFMIN((len - 1) / 2, 1 + (rnd() % 15));
            for (int i = 0; i < len; i++)
                src[i] = rnd();

            call_ref(dst0, 1, src, 1, len, radius);
            call_new(dst1, 1, src, 1, len, radius);
            if (memcmp(dst0, dst1, len))
                fail();
        }

        /* Benchmark with typical size */
        const int bench_len = 256;
        const int bench_radius = 8;
        for (int i = 0; i < bench_len; i++)
            src[i] = rnd();
        bench_new(dst1, 1, src, 1, bench_len, bench_radius);
    }
}

static void blur16_c(uint16_t *dst, int dst_step, const uint16_t *src,
                     int src_step, int len, int radius)
{
    FFBoxblurDSPContext dsp;
    int saved_flags = av_get_cpu_flags();
    av_force_cpu_flags(saved_flags & ~AV_CPU_FLAG_AVX2);
    ff_boxblur_dsp_init(&dsp, current_depth);
    av_force_cpu_flags(saved_flags);
    ff_boxblur_blur16(dst, dst_step, src, src_step, len, radius, &dsp);
}

static void blur16_simd(uint16_t *dst, int dst_step, const uint16_t *src,
                        int src_step, int len, int radius)
{
    FFBoxblurDSPContext dsp;
    ff_boxblur_dsp_init(&dsp, current_depth);
    ff_boxblur_blur16(dst, dst_step, src, src_step, len, radius, &dsp);
}

static void check_blur16(int depth)
{
    LOCAL_ALIGNED_32(uint16_t, src, [2048]);
    LOCAL_ALIGNED_32(uint16_t, dst0, [2048]);
    LOCAL_ALIGNED_32(uint16_t, dst1, [2048]);

    declare_func(void, uint16_t *, int, const uint16_t *, int, int, int);

    current_depth = depth;

    /* Register exactly one version per CPU run so checkasm records C and AVX2 */
    void (*fn)(uint16_t *, int, const uint16_t *, int, int, int) =
        (av_get_cpu_flags() & AV_CPU_FLAG_AVX2) ? blur16_simd : blur16_c;

    if (check_func(fn, "boxblur_blur16")) {
        for (int iter = 0; iter < 16; iter++) {
            const int len = 64 + (rnd() % 256);
            const int radius = FFMIN((len - 1) / 2, 1 + (rnd() % 15));
            for (int i = 0; i < len; i++)
                src[i] = rnd();

            call_ref(dst0, 1, src, 1, len, radius);
            call_new(dst1, 1, src, 1, len, radius);
            if (memcmp(dst0, dst1, len * sizeof(uint16_t)))
                fail();
        }

        /* Benchmark with typical size */
        const int bench_len = 256;
        const int bench_radius = 8;
        for (int i = 0; i < bench_len; i++)
            src[i] = rnd();
        bench_new(dst1, 1, src, 1, bench_len, bench_radius);
    }
}

void checkasm_check_boxblur(void)
{
    check_blur8(8);
    report("boxblur_blur8");
    
    check_blur16(16);
    report("boxblur_blur16");
}
