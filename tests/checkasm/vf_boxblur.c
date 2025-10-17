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

#include <string.h>
#include "checkasm.h"
#include "libavutil/mem_internal.h"

#include "libavfilter/vf_boxblur_dsp.h"

static void blur_row8_ref(uint8_t *dst, ptrdiff_t dst_step,
                          const uint8_t *src, ptrdiff_t src_step,
                          int len, int radius)
{
    if (radius <= 0 || len <= 0) {
        for (int i = 0; i < len; i++)
            dst[i * dst_step] = src[i * src_step];
        return;
    }
    const int length = radius * 2 + 1;
    const int inv = ((1 << 16) + length / 2) / length;
    int sum = src[radius * src_step];
    for (int x = 0; x < radius; x++)
        sum += (int)src[x * src_step] << 1;
    sum = sum * inv + (1 << 15);
    int x = 0;
    for (; x <= radius && x < len; x++) {
        const int right = (radius + x) * src_step;
        const int left  = (radius - x) * src_step;
        sum += ((int)src[right] - (int)src[left]) * inv;
        dst[x * dst_step] = (uint8_t)(sum >> 16);
    }
    for (; x < len - radius; x++) {
        const int in  = (radius + x) * src_step;
        const int out = (x - radius - 1) * src_step;
        sum += ((int)src[in] - (int)src[out]) * inv;
        dst[x * dst_step] = (uint8_t)(sum >> 16);
    }
    for (; x < len; x++) {
        const int in  = (2 * len - radius - x - 1) * src_step;
        const int out = (x - radius - 1) * src_step;
        sum += ((int)src[in] - (int)src[out]) * inv;
        dst[x * dst_step] = (uint8_t)(sum >> 16);
    }
}

static void blur_row16_ref(uint16_t *dst, ptrdiff_t dst_step,
                           const uint16_t *src, ptrdiff_t src_step,
                           int len, int radius)
{
    if (radius <= 0 || len <= 0) {
        for (int i = 0; i < len; i++)
            *(uint16_t *)((uint8_t *)dst + i * dst_step) = *(const uint16_t *)((const uint8_t *)src + i * src_step);
        return;
    }
    const int step_e = (int)(src_step >> 1);
    const int dstep_e = (int)(dst_step >> 1);
    const int length = radius * 2 + 1;
    const int inv = ((1 << 16) + length / 2) / length;
    int sum = src[radius * step_e];
    for (int x = 0; x < radius; x++)
        sum += (int)src[x * step_e] << 1;
    sum = sum * inv + (1 << 15);
    int x = 0;
    for (; x <= radius && x < len; x++) {
        const int right = (radius + x) * step_e;
        const int left  = (radius - x) * step_e;
        sum += ((int)src[right] - (int)src[left]) * inv;
        dst[x * dstep_e] = (uint16_t)(sum >> 16);
    }
    for (; x < len - radius; x++) {
        const int in  = (radius + x) * step_e;
        const int out = (x - radius - 1) * step_e;
        sum += ((int)src[in] - (int)src[out]) * inv;
        dst[x * dstep_e] = (uint16_t)(sum >> 16);
    }
    for (; x < len; x++) {
        const int in  = (2 * len - radius - x - 1) * step_e;
        const int out = (x - radius - 1) * step_e;
        sum += ((int)src[in] - (int)src[out]) * inv;
        dst[x * dstep_e] = (uint16_t)(sum >> 16);
    }
}

static void check_row8(void)
{
    FFBoxblurDSPContext dsp = {0};
    /* Set ref by default, then let x86 override */
    dsp.blur_row8 = blur_row8_ref;
    ff_boxblur_dsp_init_x86(&dsp);

    declare_func(void, uint8_t *, ptrdiff_t, const uint8_t *, ptrdiff_t, int, int);

    LOCAL_ALIGNED_32(uint8_t, src, [2048]);
    LOCAL_ALIGNED_32(uint8_t, dst_ref, [2048]);
    LOCAL_ALIGNED_32(uint8_t, dst_new, [2048]);

    for (int iter = 0; iter < 16; iter++) {
        const int len = 32 + (rnd() % 256);
        const int radius = FFMIN((len - 1) / 2, rnd() % 16);
        for (int i = 0; i < len; i++)
            src[i] = rnd();

        if (check_func(dsp.blur_row8, "boxblur_blur_row8")) {
            call_ref(dst_ref, 1, src, 1, len, radius);
            call_new(dst_new, 1, src, 1, len, radius);
            if (memcmp(dst_ref, dst_new, len))
                fail();
            bench_new(dst_new, 1, src, 1, len, radius);
        }
    }
}

static void check_row16(void)
{
    FFBoxblurDSPContext dsp = {0};
    dsp.blur_row16 = blur_row16_ref;
    ff_boxblur_dsp_init_x86(&dsp);

    declare_func(void, uint16_t *, ptrdiff_t, const uint16_t *, ptrdiff_t, int, int);

    LOCAL_ALIGNED_32(uint16_t, src, [2048]);
    LOCAL_ALIGNED_32(uint16_t, dst_ref, [2048]);
    LOCAL_ALIGNED_32(uint16_t, dst_new, [2048]);

    for (int iter = 0; iter < 16; iter++) {
        const int len = 32 + (rnd() % 256);
        const int radius = FFMIN((len - 1) / 2, rnd() % 16);
        for (int i = 0; i < len; i++)
            src[i] = rnd();

        if (check_func(dsp.blur_row16, "boxblur_blur_row16")) {
            call_ref(dst_ref, 2, src, 2, len, radius);
            call_new(dst_new, 2, src, 2, len, radius);
            if (memcmp(dst_ref, dst_new, len * sizeof(uint16_t)))
                fail();
            bench_new(dst_new, 2, src, 2, len, radius);
        }
    }
}

void checkasm_check_boxblur(void);
void checkasm_check_boxblur(void)
{
    check_row8();
    report("boxblur_row8");
    check_row16();
    report("boxblur_row16");
}


