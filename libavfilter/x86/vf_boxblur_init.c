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
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"

#include "libavfilter/vf_boxblur_dsp.h"

/* Forward declaration */
void ff_boxblur_dsp_init_x86(FFBoxblurDSPContext *dsp, int depth);

/* AVX2 optimized middle-loop functions */
#if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
void ff_boxblur_middle_avx2(uint8_t *dst, const uint8_t *src,
                             int x_start, int x_end, int radius,
                             int inv, int *sum_ptr);

void ff_boxblur_middle16_avx2(uint16_t *dst, const uint16_t *src,
                               int x_start, int x_end, int radius,
                               int inv, int *sum_ptr);
#endif

av_cold void ff_boxblur_dsp_init_x86(FFBoxblurDSPContext *dsp, int depth)
{
#if ARCH_X86_64 && HAVE_AVX2_EXTERNAL
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        dsp->middle = depth > 8 ? (void *)ff_boxblur_middle16_avx2 : (void *)ff_boxblur_middle_avx2;
    }
#endif
}
