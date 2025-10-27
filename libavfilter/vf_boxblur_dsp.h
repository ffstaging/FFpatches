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
    /* Optimized middle-loop function for steady-state blur */
    void (*middle)(void *dst, const void *src,
                   int x_start, int x_end, int radius,
                   int inv, int *sum_ptr);
} FFBoxblurDSPContext;

/* C reference implementations */
void boxblur_middle8_c(uint8_t *dst, const uint8_t *src,
                       int x_start, int x_end, int radius,
                       int inv, int *sum_ptr);

void boxblur_middle16_c(uint16_t *dst, const uint16_t *src,
                        int x_start, int x_end, int radius,
                        int inv, int *sum_ptr);

void ff_boxblur_dsp_init(FFBoxblurDSPContext *dsp, int depth);

#endif /* AVFILTER_BOXBLUR_DSP_H */

