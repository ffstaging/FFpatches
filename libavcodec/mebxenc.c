/*
 * Metadata Boxed (mebx) encoder
 * Copyright (c) 2025 Lukas Holliger
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

#include "avcodec.h"
#include "codec_internal.h"
#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/bprint.h"

typedef struct {
    AVDictionary *metadata;
} MebxContext;

/**
 * Main mebx encoder function.
 * For transparent round-trip transcoding, preserves original packet data
 * that was stored during decoding via frame side-data.
 */
static int mebx_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *frame, int *got_packet)
{
    if (!frame || !frame->metadata || av_dict_count(frame->metadata) == 0) {
        // something invalid here
        *got_packet = 0;
        return 0;
    } else {
        // Check if we have the original packet data stored in frame side-data
        AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_MEBX_PACKET);
        if (sd && sd->buf && sd->size > 0) {
            // Use the original packet data
            pkt->buf = av_buffer_ref(sd->buf);
            if (!pkt->buf)
                return AVERROR(ENOMEM);

            pkt->data = sd->data;
            pkt->size = sd->size;

            av_log(avctx, AV_LOG_DEBUG, "mebx_encode_frame: using original packet data from side-data, size=%d\n",
                   pkt->size);
        } else {
            // The data wasn't given to us
            av_log(avctx, AV_LOG_WARNING, "mebx_encode_frame: no original packet data, discarding frame\n");
            *got_packet = 0;
            return 0;
        }
    }

    *got_packet = 1;
    return 0;
}

static int mebx_encode_init(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_DEBUG, "mebx_encode_init: encoder initialized\n");
    avctx->pkt_timebase = avctx->time_base;

    return 0;
}

static int mebx_encode_close(AVCodecContext *avctx)
{
    MebxContext *ctx = avctx->priv_data;

    if (ctx->metadata) {
        av_dict_free(&ctx->metadata);
        ctx->metadata = NULL;
    }

    return 0;
}

const FFCodec ff_mebx_encoder = {
    .p.name         = "mebx",
    CODEC_LONG_NAME("Metadata Boxed"),
    .p.type         = AVMEDIA_TYPE_DATA,
    .p.id           = AV_CODEC_ID_MEBX,
    .priv_data_size = sizeof(MebxContext),
    .init           = mebx_encode_init,
    .close          = mebx_encode_close,
    FF_CODEC_ENCODE_CB(mebx_encode_frame),
};
