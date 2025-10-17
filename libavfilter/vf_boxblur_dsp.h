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

#ifndef AVFILTER_BOXBLUR_DSP_H
#define AVFILTER_BOXBLUR_DSP_H

#include <stddef.h>
#include <stdint.h>

typedef struct FFBoxblurDSPContext {
    /* 1D horizontal blur on one row of len pixels */
    void (*blur_row8)(uint8_t *dst, ptrdiff_t dst_step,
                      const uint8_t *src, ptrdiff_t src_step,
                      int len, int radius);

    void (*blur_row16)(uint16_t *dst, ptrdiff_t dst_step,
                       const uint16_t *src, ptrdiff_t src_step,
                       int len, int radius);
} FFBoxblurDSPContext;

/* C initializers */
void ff_boxblur_dsp_init(FFBoxblurDSPContext *dsp);
void ff_boxblur_dsp_init_aarch64(FFBoxblurDSPContext *dsp);
void ff_boxblur_dsp_init_x86(FFBoxblurDSPContext *dsp);

#endif /* AVFILTER_BOXBLUR_DSP_H */


