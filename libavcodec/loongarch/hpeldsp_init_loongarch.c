/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
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

#include "libavutil/loongarch/cpu.h"
#include "libavcodec/hpeldsp.h"
#include "libavcodec/loongarch/hpeldsp_lasx.h"

static op_pixels_func put_pixels16_xy2_8_c_fallback = NULL;
static op_pixels_func put_no_rnd_pixels16_y2_8_c_fallback = NULL;
static op_pixels_func put_no_rnd_pixels16_xy2_8_c_fallback = NULL;
static op_pixels_func put_no_rnd_pixels8_y2_8_c_fallback = NULL;
static op_pixels_func put_no_rnd_pixels8_xy2_8_c_fallback = NULL;
static op_pixels_func put_no_rnd_pixels16_x2_8_c_fallback = NULL;
static op_pixels_func put_no_rnd_pixels8_x2_8_c_fallback = NULL;

static inline void put_no_rnd_pix16_y2_8_lasx_wrap(uint8_t *block,
                                                   const uint8_t *pixels,
                                                   ptrdiff_t line_size, int h)
{
    if (h == 16 || h == 8) {
        ff_put_no_rnd_pixels16_y2_8_lasx(block, pixels, line_size, h);
    } else {
        put_no_rnd_pixels16_y2_8_c_fallback(block, pixels, line_size, h);
    }
}

static inline void put_no_rnd_pix16_xy2_8_lasx_wrap(uint8_t *block,
                                                    const uint8_t *pixels,
                                                    ptrdiff_t line_size, int h)
{
    if (h == 16 || h == 8) {
        ff_put_no_rnd_pixels16_xy2_8_lasx(block, pixels, line_size, h);
    } else {
        put_no_rnd_pixels16_xy2_8_c_fallback(block, pixels, line_size, h);
    }
}

static inline void put_no_rnd_pix8_y2_8_lasx_wrap(uint8_t *block,
                                                  const uint8_t *pixels,
                                                  ptrdiff_t line_size, int h)
{
    if (h == 8 || h == 4) {
        ff_put_no_rnd_pixels8_y2_8_lasx(block, pixels, line_size, h);
    } else {
        put_no_rnd_pixels8_y2_8_c_fallback(block, pixels, line_size, h);
    }
}

static inline void put_no_rnd_pix8_xy2_8_lasx_wrap(uint8_t *block,
                                                   const uint8_t *pixels,
                                                   ptrdiff_t line_size, int h)
{
    if (h == 8 || h == 4) {
        ff_put_no_rnd_pixels8_xy2_8_lasx(block, pixels, line_size, h);
    } else {
        put_no_rnd_pixels8_xy2_8_c_fallback(block, pixels, line_size, h);
    }
}

static inline void put_pix16_xy2_8_lasx_wrap(uint8_t *block,
                                             const uint8_t *pixels,
                                             ptrdiff_t line_size, int h)
{
   if (h == 16) {
      ff_put_pixels16_xy2_8_lasx(block, pixels, line_size, h);
   } else {
      put_pixels16_xy2_8_c_fallback(block, pixels, line_size, h);
   }
}

static inline void put_no_rnd_pix16_x2_8_lasx_wrap(uint8_t *block,
                                                   const uint8_t *pixels,
                                                   ptrdiff_t line_size, int h)
{
    if (h == 16 || h == 8) {
        ff_put_no_rnd_pixels16_x2_8_lasx(block, pixels, line_size, h);
    } else {
        put_no_rnd_pixels16_x2_8_c_fallback(block, pixels, line_size, h);
   }
}

static inline void put_no_rnd_pix8_x2_8_lasx_wrap(uint8_t *block,
                                                 const uint8_t *pixels,
                                                 ptrdiff_t line_size, int h)
{
    if (h == 8 || h == 4) {
       ff_put_no_rnd_pixels8_x2_8_lasx(block, pixels, line_size, h);
    } else {
       put_no_rnd_pixels8_x2_8_c_fallback(block, pixels, line_size, h);
    }
}

void ff_hpeldsp_init_loongarch(HpelDSPContext *c, int flags)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_lasx(cpu_flags)) {

        put_pixels16_xy2_8_c_fallback        = c->put_pixels_tab[0][3];
        put_no_rnd_pixels16_y2_8_c_fallback  = c->put_no_rnd_pixels_tab[0][2];
        put_no_rnd_pixels16_xy2_8_c_fallback = c->put_no_rnd_pixels_tab[0][3];
        put_no_rnd_pixels8_y2_8_c_fallback   = c->put_no_rnd_pixels_tab[1][2];
        put_no_rnd_pixels8_xy2_8_c_fallback  = c->put_no_rnd_pixels_tab[1][3];
        put_no_rnd_pixels16_x2_8_c_fallback  = c->put_no_rnd_pixels_tab[0][1];
        put_no_rnd_pixels8_x2_8_c_fallback   = c->put_no_rnd_pixels_tab[1][1];

        c->put_pixels_tab[0][0] = ff_put_pixels16_8_lsx;
        c->put_pixels_tab[0][1] = ff_put_pixels16_x2_8_lasx;
        c->put_pixels_tab[0][2] = ff_put_pixels16_y2_8_lasx;
        c->put_pixels_tab[0][3] = put_pix16_xy2_8_lasx_wrap;

        c->put_pixels_tab[1][0] = ff_put_pixels8_8_lasx;
        c->put_pixels_tab[1][1] = ff_put_pixels8_x2_8_lasx;
        c->put_pixels_tab[1][2] = ff_put_pixels8_y2_8_lasx;
        c->put_pixels_tab[1][3] = ff_put_pixels8_xy2_8_lasx;
        c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_8_lsx;
        c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pix16_x2_8_lasx_wrap;
        c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pix16_y2_8_lasx_wrap;
        c->put_no_rnd_pixels_tab[0][3] = put_no_rnd_pix16_xy2_8_lasx_wrap;

        c->put_no_rnd_pixels_tab[1][0] = ff_put_pixels8_8_lasx;
        c->put_no_rnd_pixels_tab[1][1] = put_no_rnd_pix8_x2_8_lasx_wrap;
        c->put_no_rnd_pixels_tab[1][2] = put_no_rnd_pix8_y2_8_lasx_wrap;
        c->put_no_rnd_pixels_tab[1][3] = put_no_rnd_pix8_xy2_8_lasx_wrap;
    }
}
