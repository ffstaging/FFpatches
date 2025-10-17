/*
 * Copyright (c) 2025 Makar Kuznietsov
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

#include "libavutil/attributes.h"
#include "libavutil/x86/cpu.h"

#include "libavfilter/vf_boxblur_dsp.h"

/*
 * We implement x86 CPU dispatch for 1D row blurs used by boxblur's
 * separable horizontal/vertical passes.
 */

#if HAVE_X86ASM
#if HAVE_AVX2_EXTERNAL
/* 32-byte vector width */
void ff_boxblur_blur_rowb_avx2(uint8_t *dst, ptrdiff_t dst_step,
                               const uint8_t *src, ptrdiff_t src_step,
                               int bytes, int radius);
void ff_boxblur_blur_roww_avx2(uint16_t *dst, ptrdiff_t dst_step,
                               const uint16_t *src, ptrdiff_t src_step,
                               int bytes, int radius);

static void blur_row8_avx2(uint8_t *dst, ptrdiff_t dst_step,
                           const uint8_t *src, ptrdiff_t src_step,
                           int len, int radius)
{
    ff_boxblur_blur_rowb_avx2(dst, dst_step, src, src_step, len, radius);
}

static void blur_row16_avx2(uint16_t *dst, ptrdiff_t dst_step,
                            const uint16_t *src, ptrdiff_t src_step,
                            int len, int radius)
{
    ff_boxblur_blur_roww_avx2(dst, dst_step, src, src_step, len * 2, radius);
}
#endif
#endif /* HAVE_X86ASM */

av_cold void ff_boxblur_dsp_init_x86(FFBoxblurDSPContext *dsp)
{
#if HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();
#if HAVE_AVX2_EXTERNAL
    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        dsp->blur_row8  = blur_row8_avx2;
        dsp->blur_row16 = blur_row16_avx2;
    }
#endif
#endif
    (void) dsp;
}


