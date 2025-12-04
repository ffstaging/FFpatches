/*
 * Copyright (C) 2025      Nikles Haas
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

#include "libavutil/pixdesc.h"
#include "libswscale/ops.h"
#include "libswscale/format.h"

static int run_test(SwsContext *const ctx,
                    const AVPixFmtDescriptor *const src_desc,
                    const AVPixFmtDescriptor *const dst_desc)
{
    bool dummy;

    SwsFormat src = {
        .format = av_pix_fmt_desc_get_id(src_desc),
        .desc   = src_desc,
    };

    SwsFormat dst = {
        .format = av_pix_fmt_desc_get_id(dst_desc),
        .desc   = dst_desc,
    };

    ff_infer_colors(&src.color, &dst.color);

    SwsOpList *ops = ff_sws_op_list_alloc();
    if (!ops)
        return AVERROR(ENOMEM);

    if (ff_sws_decode_pixfmt(ops, src.format) < 0)
        goto fail;
    if (ff_sws_decode_colors(ctx, SWS_PIXEL_F32, ops, src, &dummy) < 0)
        goto fail;
    if (ff_sws_encode_colors(ctx, SWS_PIXEL_F32, ops, dst, &dummy) < 0)
        goto fail;
    if (ff_sws_encode_pixfmt(ops, dst.format) < 0)
        goto fail;

    av_log(NULL, AV_LOG_INFO, "%s -> %s:\n",
           av_get_pix_fmt_name(src.format), av_get_pix_fmt_name(dst.format));

    ff_sws_op_list_optimize(ops);
    ff_sws_op_list_print(NULL, AV_LOG_INFO, ops);

fail:
    /* silently skip unsupported formats */
    ff_sws_op_list_free(&ops);
    return 0;
}

static void log_cb(void *avcl, int level, const char *fmt, va_list vl)
{
    if (level == AV_LOG_INFO)
        vfprintf(stdout, fmt, vl);
    else
        av_log_default_callback(avcl, level, fmt, vl);
}

int main(int argc, char **argv)
{
    SwsContext *ctx = sws_alloc_context();
    int ret = 1;

    av_log_set_callback(log_cb);

    for (const AVPixFmtDescriptor *src = av_pix_fmt_desc_next(NULL);
         src; src = av_pix_fmt_desc_next(src))
    {
        for (const AVPixFmtDescriptor *dst = av_pix_fmt_desc_next(NULL);
            dst; dst = av_pix_fmt_desc_next(dst))
        {
            int ret = run_test(ctx, src, dst);
            if (ret < 0)
                goto fail;
        }
    }

    ret = 0;
fail:
    sws_free_context(&ctx);
    return ret;
}
