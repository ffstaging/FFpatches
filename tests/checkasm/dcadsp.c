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

#include <stdint.h>
#include <string.h>

#include "libavcodec/dcadata.h"
#include "libavcodec/dcadsp.h"
#include "libavutil/common.h"
#include "libavutil/mem_internal.h"

#include "checkasm.h"

#define N 32
#define BLOCKSIZE 128
#define BUF_SIZE (N * BLOCKSIZE)
#define LFE_HISTORY 8
#define LFE_SIZE (N + LFE_HISTORY + 1)

#define randomize(buf, len) do {             \
    for (int i = 0; i < len; i++)            \
        (buf)[i] = av_clip_intp2(rnd(), 16); \
} while (0)

#define EPS 0.0005f

static void test_lfe_fir_float(const DCADSPContext *dca)
{
    LOCAL_ALIGNED_16(float, dst0, [BUF_SIZE]);
    LOCAL_ALIGNED_16(float, dst1, [BUF_SIZE]);
    LOCAL_ALIGNED_16(int32_t, lfe, [LFE_SIZE]);

    declare_func(void, float *pcm_samples, const int32_t *lfe_samples,
                       const float *filter_coeff, ptrdiff_t npcmblocks);

    for (int i = 0; i < 2; i++) {
        const float *coeffs = i ? ff_dca_lfe_fir_128 : ff_dca_lfe_fir_64;
        if (check_func(dca->lfe_fir_float[i], "lfe_fir%d_float", i)) {
            memset(dst0, 0, BUF_SIZE * sizeof(float));
            memset(dst1, 0, BUF_SIZE * sizeof(float));
            randomize(lfe, LFE_SIZE);
            call_ref(dst0, lfe + LFE_HISTORY, coeffs, N);
            call_new(dst1, lfe + LFE_HISTORY, coeffs, N);
            if (!float_near_abs_eps_array(dst0, dst1, EPS, BUF_SIZE))
                fail();
            bench_new(dst1, lfe + LFE_HISTORY, coeffs, N);
        }
    }
}

void checkasm_check_dcadsp(void)
{
    DCADSPContext dca;

    ff_dcadsp_init(&dca);

    test_lfe_fir_float(&dca);
    report("lfe_fir_float");
}
