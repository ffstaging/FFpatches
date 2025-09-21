/*
 * Animated WebP into non-compliant WebP bitstream filter
 * Copyright (c) 2024 Thilo Borgmann <thilo.borgmann _at_ mail.de>
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

/**
 * @file
 * Animated WebP into non-compliant WebP bitstream filter
 * Splits a packet containing a webp animations into
 * one non-compliant packet per frame of the animation.
 * Skips RIFF and WEBP chunks for those packets except
 * for the first. Copyies ICC, EXIF and XMP chunks first
 * into each of the packets except for the first.
 * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
 */

#include <stdio.h>
#include <string.h>

#include "codec_id.h"
#include "bytestream.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "packet.h"

#define VP8X_FLAG_ANIMATION             0x02
#define VP8X_FLAG_XMP_METADATA          0x04
#define VP8X_FLAG_EXIF_METADATA         0x08
#define VP8X_FLAG_ALPHA                 0x10
#define VP8X_FLAG_ICC                   0x20

typedef struct WEBPBSFContext {
    const AVClass *class;
    GetByteContext gb;

    AVPacket *last_pkt;
    uint8_t *last_iccp;
    uint8_t *last_exif;
    uint8_t *last_xmp;

    int iccp_size;
    int exif_size;
    int xmp_size;

    int add_iccp;
    int add_exif;
    int add_xmp;

    uint64_t last_pts;
} WEBPBSFContext;

static int save_chunk(WEBPBSFContext *ctx, uint8_t **buf, int *buf_size, uint32_t chunk_size)
{
    if (*buf || !buf_size || !chunk_size)
        return 0;

    *buf = av_malloc(chunk_size + 8);
    if (!*buf)
        return AVERROR(ENOMEM);

    *buf_size = chunk_size + 8;

    bytestream2_seek(&ctx->gb, -8, SEEK_CUR);
    bytestream2_get_buffer(&ctx->gb, *buf, chunk_size + 8);

    return 0;
}

static int awebp2webp_filter(AVBSFContext *ctx, AVPacket *out)
{
    WEBPBSFContext *s = ctx->priv_data;
    AVPacket *in;
    uint32_t chunk_type;
    uint32_t chunk_size;
    int64_t packet_start;
    int64_t packet_end;
    int64_t out_off;
    int ret       = 0;
    int is_frame  = 0;
    int key_frame = 0;
    int delay     = 0;
    int out_size  = 0;
    int has_anim  = 0;

    // initialize for new packet
    if (!bytestream2_size(&s->gb)) {
        if (s->last_pkt)
            av_packet_unref(s->last_pkt);

        ret = ff_bsf_get_packet(ctx, &s->last_pkt);
        if (ret < 0)
            goto fail;

        bytestream2_init(&s->gb, s->last_pkt->data, s->last_pkt->size);

        av_freep(&s->last_iccp);
        av_freep(&s->last_exif);
        av_freep(&s->last_xmp);

        // read packet scanning for metadata && animation
        while (bytestream2_get_bytes_left(&s->gb) > 0) {
            chunk_type = bytestream2_get_le32(&s->gb);
            chunk_size = bytestream2_get_le32(&s->gb);

            if (chunk_size == UINT32_MAX)
                return AVERROR_INVALIDDATA;
            chunk_size += chunk_size & 1;

            if (!bytestream2_get_bytes_left(&s->gb) ||
                 bytestream2_get_bytes_left(&s->gb) < chunk_size)
                break;

            if (chunk_type == MKTAG('R', 'I', 'F', 'F') && chunk_size > 4) {
                chunk_size = 4;
            }

            switch (chunk_type) {
            case MKTAG('I', 'C', 'C', 'P'):
                if (!s->last_iccp) {
                    ret = save_chunk(s, &s->last_iccp, &s->iccp_size, chunk_size);
                    if (ret < 0)
                        goto fail;
                } else {
                    bytestream2_skip(&s->gb, chunk_size);
                }
                break;

            case MKTAG('E', 'X', 'I', 'F'):
                if (!s->last_exif) {
                    ret = save_chunk(s, &s->last_exif, &s->exif_size, chunk_size);
                    if (ret < 0)
                        goto fail;
                } else {
                    bytestream2_skip(&s->gb, chunk_size);
                }
                break;

            case MKTAG('X', 'M', 'P', ' '):
                if (!s->last_xmp) {
                    ret = save_chunk(s, &s->last_xmp, &s->xmp_size, chunk_size);
                    if (ret < 0)
                        goto fail;
                } else {
                    bytestream2_skip(&s->gb, chunk_size);
                }
                break;

            case MKTAG('A', 'N', 'M', 'F'):
                has_anim = 1;
                bytestream2_skip(&s->gb, chunk_size);
                break;

            default:
                bytestream2_skip(&s->gb, chunk_size);
                break;
            }
        }

        // if no animation is found, pass-through the packet
        if (!has_anim) {
            av_packet_move_ref(out, s->last_pkt);
            return 0;
        }

        // reset bytestream to beginning of packet
        bytestream2_init(&s->gb, s->last_pkt->data, s->last_pkt->size);
    }

    // packet read completely, reset and ask for next packet
    if (!bytestream2_get_bytes_left(&s->gb)) {
        if (s->last_pkt)
            av_packet_free(&s->last_pkt);
        // reset to empty buffer for reinit with next real packet
        bytestream2_init(&s->gb, NULL, 0);
        return AVERROR(EAGAIN);
    }

    // start reading from packet until sub packet ready
    packet_start = bytestream2_tell(&s->gb);
    s->add_iccp  = 1;
    s->add_exif  = 1;
    s->add_xmp   = 1;

    while (bytestream2_get_bytes_left(&s->gb) > 0) {
        chunk_type = bytestream2_get_le32(&s->gb);
        chunk_size = bytestream2_get_le32(&s->gb);

        if (chunk_size == UINT32_MAX)
            return AVERROR_INVALIDDATA;
        chunk_size += chunk_size & 1;

        if (!bytestream2_get_bytes_left(&s->gb) ||
             bytestream2_get_bytes_left(&s->gb) < chunk_size)
            break;

        if (chunk_type == MKTAG('R', 'I', 'F', 'F') && chunk_size > 4) {
            chunk_size = 4;
            key_frame = 1;
        }

        switch (chunk_type) {
        case MKTAG('I', 'C', 'C', 'P'):
            s->add_iccp = 0;
            bytestream2_skip(&s->gb, chunk_size);
            break;

        case MKTAG('E', 'X', 'I', 'F'):
            s->add_exif = 0;
            bytestream2_skip(&s->gb, chunk_size);
            break;

        case MKTAG('X', 'M', 'P', ' '):
            s->add_xmp = 0;
            bytestream2_skip(&s->gb, chunk_size);
            break;

        case MKTAG('V', 'P', '8', ' '):
            if (is_frame) {
                bytestream2_seek(&s->gb, -8, SEEK_CUR);
                goto flush;
            }
            bytestream2_skip(&s->gb, chunk_size);
            is_frame = 1;
            break;

        case MKTAG('V', 'P', '8', 'L'):
            if (is_frame) {
                bytestream2_seek(&s->gb, -8, SEEK_CUR);
                goto flush;
            }
            bytestream2_skip(&s->gb, chunk_size);
            is_frame = 1;
            break;

        case MKTAG('A', 'N', 'M', 'F'):
            if (is_frame) {
                bytestream2_seek(&s->gb, -8, SEEK_CUR);
                goto flush;
            }
            bytestream2_skip(&s->gb, 12);
            delay = bytestream2_get_le24(&s->gb);
            if (!delay)
                delay = s->last_pkt->duration;
            bytestream2_skip(&s->gb, 1);
            break;

        default:
            bytestream2_skip(&s->gb, chunk_size);
            break;
        }

        packet_end = bytestream2_tell(&s->gb);
    }

flush:
    // generate packet from data read so far
    out_size = packet_end - packet_start;
    out_off  = 0;

    if (s->add_iccp && s->last_iccp)
        out_size += s->iccp_size;
    if (s->add_exif && s->last_exif)
        out_size += s->exif_size;
    if (s->add_xmp && s->last_xmp)
        out_size += s->xmp_size;

    ret = av_new_packet(out, out_size);
    if (ret < 0)
        goto fail;

    // copy metadata
    if (s->add_iccp && s->last_iccp) {
        memcpy(out->data + out_off, s->last_iccp, s->iccp_size);
        out_off += s->iccp_size;
    }
    if (s->add_exif && s->last_exif) {
        memcpy(out->data + out_off, s->last_exif, s->exif_size);
        out_off += s->exif_size;
    }
    if (s->add_xmp && s->last_xmp) {
        memcpy(out->data + out_off, s->last_xmp, s->xmp_size);
        out_off += s->xmp_size;
    }

    // copy frame data
    memcpy(out->data + out_off, s->last_pkt->data + packet_start, packet_end - packet_start);

    if (key_frame)
        out->flags |= AV_PKT_FLAG_KEY;
    else
        out->flags &= ~AV_PKT_FLAG_KEY;

    out->pts          = s->last_pts;
    out->dts          = out->pts;
    out->pos          = packet_start;
    out->duration     = delay;
    out->stream_index = s->last_pkt->stream_index;
    out->time_base    = s->last_pkt->time_base;

    s->last_pts += (delay > 0) ? delay : 1;

    key_frame = 0;

    return 0;

fail:
    if (ret < 0) {
        av_packet_unref(out);
        return ret;
    }
    av_packet_free(&in);

    return ret;
}

static void awebp2webp_close(AVBSFContext *ctx)
{
    WEBPBSFContext *s = ctx->priv_data;
    av_freep(&s->last_iccp);
    av_freep(&s->last_exif);
    av_freep(&s->last_xmp);
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_WEBP, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_awebp2webp_bsf = {
    .p.name         = "awebp2webp",
    .p.codec_ids    = codec_ids,
    .priv_data_size = sizeof(WEBPBSFContext),
    .filter         = awebp2webp_filter,
    .close          = awebp2webp_close,
};
