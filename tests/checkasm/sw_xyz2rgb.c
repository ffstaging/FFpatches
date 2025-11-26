/*
 * Copyright (c) 2025 Arpad Panyik <Arpad.Panyik@arm.com>
 *
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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

#include "checkasm.h"

#define NUM_LINES 4
#define MAX_LINE_SIZE 1920

#define randomize_buffers(buf, size)      \
    do {                                  \
        for (int j = 0; j < size; j += 4) \
            AV_WN32(buf + j, rnd());      \
    } while (0)

static void check_xyz12Torgb48le(void)
{
    const int src_pix_fmt = AV_PIX_FMT_XYZ12LE;
    const int dst_pix_fmt = AV_PIX_FMT_RGB48LE;
    const AVPixFmtDescriptor *dst_desc = av_pix_fmt_desc_get(dst_pix_fmt);
    const AVPixFmtDescriptor *src_desc = av_pix_fmt_desc_get(src_pix_fmt);

    static const int input_sizes[] = {1, 2, 3, 4, 5, 6, 7, 8, 16, 17, 21, 31, 32,
                                      64, 128, 256, 512, 1024, MAX_LINE_SIZE};

    declare_func(void, const SwsContext *, uint8_t *, int, const uint8_t *,
                 int, int, int);

    LOCAL_ALIGNED_8(uint8_t, src,     [6 * MAX_LINE_SIZE * NUM_LINES]);
    LOCAL_ALIGNED_8(uint8_t, dst_ref, [6 * MAX_LINE_SIZE * NUM_LINES]);
    LOCAL_ALIGNED_8(uint8_t, dst_new, [6 * MAX_LINE_SIZE * NUM_LINES]);

    randomize_buffers(src,  MAX_LINE_SIZE * NUM_LINES);

    for (int height = 1; height <= NUM_LINES; height++) {
        for (int isi = 0; isi < FF_ARRAY_ELEMS(input_sizes); isi++) {
            SwsContext *sws;
            SwsInternal *c;
            int log_level;
            int width = input_sizes[isi];
            const int srcStride = 6 * MAX_LINE_SIZE;
            const int dstStride = 6 * MAX_LINE_SIZE;

            // Override log level to prevent spamming of the message:
            // "No accelerated colorspace conversion found from %s to %s"
            log_level = av_log_get_level();
            av_log_set_level(AV_LOG_ERROR);
            sws = sws_getContext(width, height, src_pix_fmt,
                                 width, height, dst_pix_fmt,
                                 0, NULL, NULL, NULL);
            av_log_set_level(log_level);
            if (!sws)
                fail();

            c = sws_internal(sws);
            if (check_func(c->xyz12Torgb48, "%s_%s_%dx%d", src_desc->name,
                           dst_desc->name, width, height)) {
                memset(dst_ref, 0xFF, MAX_LINE_SIZE * NUM_LINES);
                memset(dst_new, 0xFF, MAX_LINE_SIZE * NUM_LINES);

                call_ref((const SwsContext*)c, dst_ref, dstStride, src,
                         srcStride, width, height);
                call_new((const SwsContext*)c, dst_new, dstStride, src,
                         srcStride, width, height);

                if (memcmp(dst_ref, dst_new, MAX_LINE_SIZE * NUM_LINES))
                    fail();

                if (!(width & 3) && height == NUM_LINES) {
                    bench_new((const SwsContext*)c, dst_new, dstStride,
                              src, srcStride, width, height);
                }
            }
            sws_freeContext(sws);
        }
    }
}

#undef NUM_LINES
#undef MAX_LINE_SIZE

void checkasm_check_sw_xyz2rgb(void)
{
    check_xyz12Torgb48le();
    report("xyz12Torgb48le");
}
