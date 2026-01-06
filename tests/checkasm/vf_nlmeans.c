/*
 * Copyright (c) 2018 Clément Bœsch <u pkh me>
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

#include <math.h>
#include "checkasm.h"
#include "libavfilter/vf_nlmeans_init.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"

#define randomize_buffer(buf, size) do {    \
    int i;                                  \
    for (i = 0; i < size / 4; i++)          \
        ((uint32_t *)buf)[i] = rnd();       \
} while (0)

static int float_almost_equal(float a, float b, float eps)
{
    return fabsf(a - b) < eps;
}

void checkasm_check_nlmeans(void)
{
    NLMeansDSPContext dsp = {0};

    const int w = 123;  // source width
    const int h = 45;   // source height
    const int p = 3;    // patch half size
    const int r = 2;    // research window half size

    ff_nlmeans_init(&dsp);

    /* See the filter's code for the explanations on the variables */
    if (check_func(dsp.compute_safe_ssd_integral_image, "ssd_integral_image")) {
        int offx, offy;
        const int e = p + r;
        const int ii_w = w + e*2;
        const int ii_h = h + e*2;
        const int ii_lz_32 = FFALIGN(ii_w + 1, 4);
        uint32_t *ii_orig_ref = av_calloc(ii_h + 1, ii_lz_32 * sizeof(*ii_orig_ref));
        uint32_t *ii_ref = ii_orig_ref + ii_lz_32 + 1;
        uint32_t *ii_orig_new = av_calloc(ii_h + 1, ii_lz_32 * sizeof(*ii_orig_new));
        uint32_t *ii_new = ii_orig_new + ii_lz_32 + 1;
        const int src_lz = FFALIGN(w, 16);
        uint8_t *src = av_calloc(h, src_lz);

        declare_func(void, uint32_t *dst, ptrdiff_t dst_linesize_32,
                     const uint8_t *s1, ptrdiff_t linesize1,
                     const uint8_t *s2, ptrdiff_t linesize2,
                     int w, int h);

        randomize_buffer(src, h * src_lz);

        for (offy = -r; offy <= r; offy++) {
            for (offx = -r; offx <= r; offx++) {
                if (offx || offy) {
                    const int s1x = e;
                    const int s1y = e;
                    const int s2x = e + offx;
                    const int s2y = e + offy;
                    const int startx_safe = FFMAX(s1x, s2x);
                    const int starty_safe = FFMAX(s1y, s2y);
                    const int u_endx_safe = FFMIN(s1x + w, s2x + w);
                    const int endy_safe   = FFMIN(s1y + h, s2y + h);
                    const int safe_pw = (u_endx_safe - startx_safe) & ~0xf;
                    const int safe_ph = endy_safe - starty_safe;

                    av_assert0(safe_pw && safe_ph);
                    av_assert0(startx_safe - s1x >= 0); av_assert0(startx_safe - s1x < w);
                    av_assert0(starty_safe - s1y >= 0); av_assert0(starty_safe - s1y < h);
                    av_assert0(startx_safe - s2x >= 0); av_assert0(startx_safe - s2x < w);
                    av_assert0(starty_safe - s2y >= 0); av_assert0(starty_safe - s2y < h);

                    memset(ii_ref, 0, (ii_lz_32 * ii_h - 1) * sizeof(*ii_ref));
                    memset(ii_new, 0, (ii_lz_32 * ii_h - 1) * sizeof(*ii_new));

                    call_ref(ii_ref + starty_safe*ii_lz_32 + startx_safe, ii_lz_32,
                             src + (starty_safe - s1y) * src_lz + (startx_safe - s1x), src_lz,
                             src + (starty_safe - s2y) * src_lz + (startx_safe - s2x), src_lz,
                             safe_pw, safe_ph);
                    call_new(ii_new + starty_safe*ii_lz_32 + startx_safe, ii_lz_32,
                             src + (starty_safe - s1y) * src_lz + (startx_safe - s1x), src_lz,
                             src + (starty_safe - s2y) * src_lz + (startx_safe - s2x), src_lz,
                             safe_pw, safe_ph);

                    if (memcmp(ii_ref, ii_new, (ii_lz_32 * ii_h - 1) * sizeof(*ii_ref)))
                        fail();

                    memset(ii_new, 0, (ii_lz_32 * ii_h - 1) * sizeof(*ii_new));
                    bench_new(ii_new + starty_safe*ii_lz_32 + startx_safe, ii_lz_32,
                             src + (starty_safe - s1y) * src_lz + (startx_safe - s1x), src_lz,
                             src + (starty_safe - s2y) * src_lz + (startx_safe - s2x), src_lz,
                             safe_pw, safe_ph);
                }
            }
        }

        av_freep(&ii_orig_ref);
        av_freep(&ii_orig_new);
        av_freep(&src);
    }

    if (check_func(dsp.compute_weights_line, "compute_weights_line")) {
        const int test_w = 256;
        const int max_meaningful_diff = 255;
        const int startx = 10;
        const int endx = 200;

        // Allocate aligned buffers
        uint32_t *iia     = av_malloc_array(test_w + 16, sizeof(uint32_t));
        uint32_t *iib     = av_malloc_array(test_w + 16, sizeof(uint32_t));
        uint32_t *iid     = av_malloc_array(test_w + 16, sizeof(uint32_t));
        uint32_t *iie     = av_malloc_array(test_w + 16, sizeof(uint32_t));
        uint8_t  *src     = av_malloc(test_w + 16);
        float    *tw_ref  = av_calloc(test_w + 16, sizeof(float));
        float    *tw_new  = av_calloc(test_w + 16, sizeof(float));
        float    *sum_ref = av_calloc(test_w + 16, sizeof(float));
        float    *sum_new = av_calloc(test_w + 16, sizeof(float));
        float    *lut     = av_malloc_array(max_meaningful_diff + 1, sizeof(float));

        declare_func(void, const uint32_t *const iia,
                     const uint32_t *const iib,
                     const uint32_t *const iid,
                     const uint32_t *const iie,
                     const uint8_t *const src,
                     float *total_weight,
                     float *sum,
                     const float *const weight_lut,
                     ptrdiff_t max_meaningful_diff,
                     ptrdiff_t startx, ptrdiff_t endx);

        if (!iia || !iib || !iid || !iie || !src || !tw_ref || !tw_new ||
            !sum_ref || !sum_new || !lut)
            goto cleanup_weights;

        // Initialize LUT: weight = exp(-diff * scale)
        // Using scale = 0.01 for testing
        for (int i = 0; i <= max_meaningful_diff; i++)
            lut[i] = expf(-i * 0.01f);

        // Initialize source pixels
        for (int i = 0; i < test_w; i++)
            src[i] = rnd() & 0xff;

        // Initialize integral images
        // We need to ensure diff = e - d - b + a is non-negative and within range
        // Set up as if computing real integral image values
        for (int i = 0; i < test_w; i++) {
            uint32_t base = rnd() % 1000;
            iia[i] = base;
            iib[i] = base + (rnd() % 100);
            iid[i] = base + (rnd() % 100);
            // e = a + (b - a) + (d - a) + diff
            // So diff = e - d - b + a will be in range [0, max_meaningful_diff]
            uint32_t diff = rnd() % (max_meaningful_diff + 1);
            iie[i] = iia[i] + (iib[i] - iia[i]) + (iid[i] - iia[i]) + diff;
        }

        // Clear output buffers
        memset(tw_ref,  0, (test_w + 16) * sizeof(float));
        memset(tw_new,  0, (test_w + 16) * sizeof(float));
        memset(sum_ref, 0, (test_w + 16) * sizeof(float));
        memset(sum_new, 0, (test_w + 16) * sizeof(float));

        call_ref(iia, iib, iid, iie, src, tw_ref, sum_ref, lut,
                 max_meaningful_diff, startx, endx);
        call_new(iia, iib, iid, iie, src, tw_new, sum_new, lut,
                 max_meaningful_diff, startx, endx);

        // Compare results with small tolerance for floating point
        for (int i = startx; i < endx; i++) {
            if (!float_almost_equal(tw_ref[i], tw_new[i], 1e-5f)) {
                fprintf(stderr, "total_weight mismatch at %d: ref=%f new=%f\n",
                        i, tw_ref[i], tw_new[i]);
                fail();
                break;
            }
            if (!float_almost_equal(sum_ref[i], sum_new[i], 1e-4f)) {
                fprintf(stderr, "sum mismatch at %d: ref=%f new=%f\n",
                        i, sum_ref[i], sum_new[i]);
                fail();
                break;
            }
        }

        // Benchmark
        memset(tw_new,  0, (test_w + 16) * sizeof(float));
        memset(sum_new, 0, (test_w + 16) * sizeof(float));
        bench_new(iia, iib, iid, iie, src, tw_new, sum_new, lut,
                  max_meaningful_diff, startx, endx);

cleanup_weights:
        av_freep(&iia);
        av_freep(&iib);
        av_freep(&iid);
        av_freep(&iie);
        av_freep(&src);
        av_freep(&tw_ref);
        av_freep(&tw_new);
        av_freep(&sum_ref);
        av_freep(&sum_new);
        av_freep(&lut);
    }

    report("dsp");
}
