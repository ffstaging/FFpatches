/*
* Copyright (c) 2014 9mmilly
*
* This file is part of FFmpeg.
*
* Permission to use, copy, modify, and/or distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
* WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
* ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
* WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
* ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
* OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <stdint.h>
#include <string.h>

#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "libavcodec/codec_id.h"
#include "mux.h"
#include "pcm.h"
#include "rawenc.h"

#define ROT_IDENTIFIER "frot"

typedef enum {
    ROT_FORMAT_S8,
    ROT_FORMAT_S16,
    ROT_FORMAT_S24,
    ROT_FORMAT_S32,
    ROT_FORMAT_FLOAT,
    ROT_FORMAT_DOUBLE
} rot_format;

/* demuxer */

static int rot_probe(const AVProbeData *probe) {
    if (probe->buf_size <= 32)
        return 0;

    if (probe->buf_size >= 36) {
        if (memcmp(probe->buf, ROT_IDENTIFIER, 4) == 0)
            return AVPROBE_SCORE_MAX;
    }
    return 0;
}

static int rot_read_header(AVFormatContext *context) {
    uint8_t header_buffer[8];

    if (avio_read(context->pb, header_buffer, sizeof(header_buffer)) != 8)
        return AVERROR_INVALIDDATA;

    uint16_t sample_rate;
    memcpy(&sample_rate, (header_buffer + 4), 2);

    uint8_t channels;
    memcpy(&channels, (header_buffer + 6), 1);

    uint8_t format;
    memcpy(&format, (header_buffer + 7), 1);

    if (sample_rate <= 0 || channels <= 0)
        return AVERROR_INVALIDDATA;

    AVStream *stream = avformat_new_stream(context, NULL);
    if (!stream) {
        av_log(stream, AV_LOG_ERROR, "invalid audio parameters\n");
        return AVERROR(ENOMEM);
    }

    stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    stream->codecpar->ch_layout.nb_channels = channels;
    stream->codecpar->sample_rate = sample_rate;

    switch (format) {
    case ROT_FORMAT_S8:
        stream->codecpar->codec_id = AV_CODEC_ID_PCM_S8;
        stream->codecpar->bits_per_coded_sample = 8;
        stream->codecpar->block_align = 8 * channels / 8;
        stream->codecpar->bit_rate = (int64_t)sample_rate * channels * 8;
        break;

    case ROT_FORMAT_S16:
        stream->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
        stream->codecpar->bits_per_coded_sample = 16;
        stream->codecpar->block_align = 16 * channels / 8;
        stream->codecpar->bit_rate = (int64_t)sample_rate * channels * 16;
        break;

    case ROT_FORMAT_S24:
        stream->codecpar->codec_id = AV_CODEC_ID_PCM_S24LE;
        stream->codecpar->bits_per_coded_sample = 24;
        stream->codecpar->block_align = 24 * channels / 8;
        stream->codecpar->bit_rate = (int64_t)sample_rate * channels * 24;
        break;

    case ROT_FORMAT_S32:
        stream->codecpar->codec_id = AV_CODEC_ID_PCM_S32LE;
        stream->codecpar->bits_per_coded_sample = 32;
        stream->codecpar->block_align = 32 * channels / 8;
        stream->codecpar->bit_rate = (int64_t)sample_rate * channels * 32;
        break;

    case ROT_FORMAT_FLOAT:
        stream->codecpar->codec_id = AV_CODEC_ID_PCM_F32LE;
        stream->codecpar->bits_per_coded_sample = 32;
        stream->codecpar->block_align = 32 * channels / 8;
        stream->codecpar->bit_rate = (int64_t)sample_rate * channels * 32;
        break;

    case ROT_FORMAT_DOUBLE:
        stream->codecpar->codec_id = AV_CODEC_ID_PCM_F64LE;
        stream->codecpar->bits_per_coded_sample = 64;
        stream->codecpar->block_align = 64 * channels / 8;
        stream->codecpar->bit_rate = (int64_t)sample_rate * channels * 64;
        break;

    default:
        return AVERROR_INVALIDDATA;
    }
    avpriv_set_pts_info(stream, 64, 1, sample_rate);

    return 0;
}

const FFInputFormat ff_rot_demuxer = {
    .p.name = "rot",
    .p.long_name = NULL_IF_CONFIG_SMALL("rot pcm header"),
    .priv_data_size = 0,
    .p.extensions = "rot",
    .read_probe = rot_probe,
    .read_header = rot_read_header,
    .read_packet = ff_pcm_read_packet};

/* muxer */

static int rot_write_header(AVFormatContext *context) {

    const FFOutputFormat *ofmt = ffofmt(context->oformat);
    av_log(context, AV_LOG_INFO, "flags_internal: 0x%x\n",
           ofmt->flags_internal);

    if (context->nb_streams <= 0)
        return AVERROR(EINVAL);

    AVStream *stream = context->streams[0];
    if (stream == NULL)
        return AVERROR(EINVAL);

    uint8_t header_buffer[8];
    uint16_t sample_rate = stream->codecpar->sample_rate;
    uint8_t channels = stream->codecpar->ch_layout.nb_channels;

    if (sample_rate <= 0 || channels <= 0)
        return AVERROR(EINVAL);

    uint8_t format;
    switch (stream->codecpar->codec_id) {
    case AV_CODEC_ID_PCM_S8:
        format = ROT_FORMAT_S8;
        break;

    case AV_CODEC_ID_PCM_S16LE:
        format = ROT_FORMAT_S16;
        break;

    case AV_CODEC_ID_PCM_S24LE:
        format = ROT_FORMAT_S24;
        break;

    case AV_CODEC_ID_PCM_S32LE:
        format = ROT_FORMAT_S32;
        break;

    case AV_CODEC_ID_PCM_F32LE:
        format = ROT_FORMAT_FLOAT;
        break;

    case AV_CODEC_ID_PCM_F64LE:
        format = ROT_FORMAT_DOUBLE;
        break;
    default:
        return AVERROR(EINVAL);
    }

    memcpy(header_buffer, ROT_IDENTIFIER, 4);
    memcpy(header_buffer + 4, &sample_rate, 2);
    memcpy(header_buffer + 6, &channels, 1);
    memcpy(header_buffer + 7, &format, 1);

    avio_write(context->pb, header_buffer, sizeof(header_buffer));
    return 0;
}

static int rot_write_packet(AVFormatContext *context, AVPacket *packet) {
    avio_write(context->pb, packet->data, packet->size);
    return 0;
}

static int rot_query_codec(enum AVCodecID codec_id, int std_compliance) {
    switch (codec_id) {
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_F32LE:
    case AV_CODEC_ID_PCM_F64LE:
        return 1;
    default:
        return 0;
    }
}

const FFOutputFormat ff_rot_muxer = {
    .p.name = "rot",
    .p.long_name = NULL_IF_CONFIG_SMALL("rot pcm header"),
    .priv_data_size = 0,
    .p.extensions = "rot",
    .p.audio_codec = AV_CODEC_ID_PCM_S16LE,
    .query_codec = rot_query_codec,
    .write_header = rot_write_header,
    .write_packet = rot_write_packet,
};
