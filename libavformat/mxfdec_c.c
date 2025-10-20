/*
 * C binding for MXF demuxer.
 * Copyright (c) 2025 Tomas HÃ¤rdin
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

#include "libavutil/opt.h"
#include "demux.h"
#include "mxfdec.h"

static const AVOption options[] = {
    { "eia608_extract", "extract eia 608 captions from s436m track",
      offsetof(MXFContext, eia608_extract), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1,
      AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass demuxer_class = {
    .class_name = "mxf",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

const FFInputFormat ff_mxf_demuxer = {
    .p.name         = "mxf",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MXF (Material eXchange Format)"),
    .p.flags        = AVFMT_SEEK_TO_PTS | AVFMT_NOGENSEARCH,
    .p.priv_class   = &demuxer_class,
    .priv_data_size = sizeof(MXFContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = mxf_probe,
    .read_header    = mxf_read_header,
    .read_packet    = mxf_read_packet,
    .read_close     = mxf_read_close,
    .read_seek      = mxf_read_seek,
};
