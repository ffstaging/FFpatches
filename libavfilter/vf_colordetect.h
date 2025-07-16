/*
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

#ifndef AVFILTER_VF_COLORDETECT_H
#define AVFILTER_VF_COLORDETECT_H

#include <stddef.h>
#include <stdint.h>

#include <libavutil/macros.h>
#include <libavutil/pixfmt.h>

typedef struct FFColorDetectDSPContext {
    /* Returns 1 if an out-of-range value was detected, 0 otherwise */
    int (*detect_range)(const uint8_t *data, ptrdiff_t stride,
                        ptrdiff_t width, ptrdiff_t height,
                        int mpeg_min, int mpeg_max);

    /* Returns 1 if the color value exceeds the alpha value, 0 otherwise */
    int (*detect_alpha)(const uint8_t *color, ptrdiff_t color_stride,
                        const uint8_t *alpha, ptrdiff_t alpha_stride,
                        ptrdiff_t width, ptrdiff_t height,
                        int p, int q, int k);
} FFColorDetectDSPContext;

void ff_color_detect_dsp_init(FFColorDetectDSPContext *dsp, int depth,
                              enum AVColorRange color_range);

static inline int ff_detect_range_c(const uint8_t *data, ptrdiff_t stride,
                                    ptrdiff_t width, ptrdiff_t height,
                                    int mpeg_min, int mpeg_max)
{
    while (height--) {
        uint8_t min = mpeg_min, max = mpeg_max;
        for (int x = 0; x < width; x++) {
            const uint8_t val = data[x];
            min = FFMIN(min, val);
            max = FFMAX(max, val);
        }
        if (min < mpeg_min || max > mpeg_max)
            return 1;
        data += stride;
    }

    return 0;
}

static inline int ff_detect_range16_c(const uint8_t *data, ptrdiff_t stride,
                                      ptrdiff_t width, ptrdiff_t height,
                                      int mpeg_min, int mpeg_max)
{
    while (height--) {
        const uint16_t *data16 = (const uint16_t *) data;
        uint16_t min = mpeg_min, max = mpeg_max;
        for (int x = 0; x < width; x++) {
            const uint16_t val = data16[x];
            min = FFMIN(min, val);
            max = FFMAX(max, val);
        }
        if (min < mpeg_min || max > mpeg_max)
            return 1;
        data += stride;
    }

    return 0;
}

static inline int
ff_detect_alpha_full_c(const uint8_t *color, ptrdiff_t color_stride,
                       const uint8_t *alpha, ptrdiff_t alpha_stride,
                       ptrdiff_t width, ptrdiff_t height,
                       int p, int q, int k)
{
    int ret = 0;

    while (height--) {
        for (int x = 0; x < width; x++)
            ret |= color[x] > alpha[x];
        if (ret)
            return ret;
        color += color_stride;
        alpha += alpha_stride;
    }
    return 0;
}

static inline int
ff_detect_alpha_limited_c(const uint8_t *color, ptrdiff_t color_stride,
                          const uint8_t *alpha, ptrdiff_t alpha_stride,
                          ptrdiff_t width, ptrdiff_t height,
                          int p, int q, int k)
{
    int ret = 0;

    while (height--) {
        for (int x = 0; x < width; x++)
            ret |= p * color[x] + k > q * alpha[x];
        if (ret)
            return ret;
        color += color_stride;
        alpha += alpha_stride;
    }
    return 0;
}

static inline int
ff_detect_alpha16_full_c(const uint8_t *color, ptrdiff_t color_stride,
                         const uint8_t *alpha, ptrdiff_t alpha_stride,
                         ptrdiff_t width, ptrdiff_t height,
                         int p, int q, int k)
{
    int ret = 0;

    while (height--) {
        const uint16_t *color16 = (const uint16_t *) color;
        const uint16_t *alpha16 = (const uint16_t *) alpha;
        for (int x = 0; x < width; x++)
            ret |= color16[x] > alpha16[x];
        if (ret)
            return ret;
        color += color_stride;
        alpha += alpha_stride;
    }
    return 0;
}

static inline int
ff_detect_alpha16_limited_c(const uint8_t *color, ptrdiff_t color_stride,
                            const uint8_t *alpha, ptrdiff_t alpha_stride,
                            ptrdiff_t width, ptrdiff_t height,
                            int p, int q, int k)
{
    int ret = 0;

    while (height--) {
        const uint16_t *color16 = (const uint16_t *) color;
        const uint16_t *alpha16 = (const uint16_t *) alpha;
        for (int x = 0; x < width; x++)
            ret |= p * color16[x] + k > q * alpha16[x];
        if (ret)
            return ret;
        color += color_stride;
        alpha += alpha_stride;
    }
    return 0;
}

#endif /* AVFILTER_VF_COLORDETECT_H */
