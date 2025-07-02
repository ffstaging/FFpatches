/*
 * NetInt XCoder H.264 Encoder
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

#include "nienc.h"

static const AVOption enc_options[] = {
    NI_ENC_OPTIONS,
    NI_ENC_OPTION_GEN_GLOBAL_HEADERS,
    NI_ENC_OPTION_UDU_SEI,
    {NULL}
};

static const AVClass h264_xcoderenc_class = {
    .class_name = "h264_ni_quadra_enc",
    .item_name  = av_default_item_name,
    .option     = enc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

FFCodec ff_h264_ni_quadra_encoder = {
    .p.name           = "h264_ni_quadra_enc",
    CODEC_LONG_NAME("H.264 NETINT Quadra encoder v" NI_XCODER_REVISION),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_H264,
    .p.priv_class     = &h264_xcoderenc_class,
    .p.capabilities   = AV_CODEC_CAP_DELAY,
    .p.pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P,
                                                      AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_NV12,
                                                      AV_PIX_FMT_P010LE, AV_PIX_FMT_NI_QUAD,
                                                      AV_PIX_FMT_NONE },
    FF_CODEC_RECEIVE_PACKET_CB(ff_xcoder_receive_packet),
    .init             = xcoder_encode_init,
    .close            = xcoder_encode_close,
    .priv_data_size   = sizeof(XCoderEncContext),
    .hw_configs       = ff_ni_enc_hw_configs,
};
