/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2018 Danil Iashchenko
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

#ifndef AVFILTER_BOXBLUR_H
#define AVFILTER_BOXBLUR_H

#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"

typedef struct FilterParam {
    int radius;
    int power;
    char *radius_expr;
} FilterParam;

#define Y 0
#define U 1
#define V 2
#define A 3

int ff_boxblur_eval_filter_params(AVFilterLink *inlink,
                                  FilterParam *luma_param,
                                  FilterParam *chroma_param,
                                  FilterParam *alpha_param);

/* Forward declaration */
typedef struct FFBoxblurDSPContext FFBoxblurDSPContext;

/* Blur functions - used for testing and internally */
void ff_boxblur_blur8(uint8_t *dst, int dst_step, const uint8_t *src,
                      int src_step, int len, int radius, FFBoxblurDSPContext *dsp);
void ff_boxblur_blur16(uint16_t *dst, int dst_step, const uint16_t *src,
                       int src_step, int len, int radius, FFBoxblurDSPContext *dsp);

#endif // AVFILTER_BOXBLUR_H
