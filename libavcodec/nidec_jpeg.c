/*
 * XCoder JPEG Decoder
 * Copyright (c) 2021 NetInt
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

#include "nidec.h"
#include "hwconfig.h"
#include "profiles.h"
static const AVCodecHWConfigInternal *ff_ni_quad_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public = {
            .pix_fmt = AV_PIX_FMT_NI_QUAD,
            .methods = AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX |
                      AV_CODEC_HW_CONFIG_METHOD_AD_HOC |
                      AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX,
            .device_type = AV_HWDEVICE_TYPE_NI_QUADRA,
        },
        .hwaccel = NULL,
    },
    NULL
};

static const AVOption dec_options[] = {
    NI_DEC_OPTIONS,
    {NULL},
};

#define JPEG_NI_QUADRA_DEC "jpeg_ni_quadra_dec"

static const AVClass jpeg_xcoderdec_class = {
    .class_name = JPEG_NI_QUADRA_DEC,
    .item_name  = av_default_item_name,
    .option     = dec_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

FFCodec ff_jpeg_ni_quadra_decoder = {
    .p.name         = JPEG_NI_QUADRA_DEC,
    CODEC_LONG_NAME("JPEG NETINT Quadra decoder v" NI_XCODER_REVISION),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MJPEG,
    .p.capabilities = AV_CODEC_CAP_AVOID_PROBING | AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE,
    .p.priv_class   = &jpeg_xcoderdec_class,
    .p.pix_fmts     = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_NI_QUAD,
                                                    AV_PIX_FMT_NONE },
    FF_CODEC_RECEIVE_FRAME_CB(xcoder_receive_frame),
    .hw_configs     = ff_ni_quad_hw_configs,
    .init           = xcoder_decode_init,
    .close          = xcoder_decode_close,
    .priv_data_size = sizeof(XCoderDecContext),
    .flush          = xcoder_decode_flush,
};
