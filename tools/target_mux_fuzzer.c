/*
 * Fuzzer for libavformat muxers
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

#include "config.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavformat/avformat.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void error(const char *err)
{
    fprintf(stderr, "%s", err);
    exit(1);
}

// Global muxer to fuzz.
static const AVOutputFormat *fmt = NULL;

// Mock IO to discard output
static int write_packet(void *opaque, const uint8_t *buf, int buf_size)
{
    return buf_size;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    AVFormatContext *oc = NULL;
    AVPacket *pkt = NULL;
    AVIOContext *pb = NULL;
    uint8_t *io_buffer = NULL;
    int io_buffer_size = 32768;
    int ret;
    const uint8_t *end = data + size;

    if (!fmt) {
#ifdef FFMPEG_MUXER
#define XSTR(s) STR(s)
#define STR(s) #s
        fmt = av_guess_format(XSTR(FFMPEG_MUXER), NULL, NULL);
#endif
        // Fallback to a common default if not specified
        if (!fmt) fmt = av_guess_format("mp4", NULL, NULL);
        if (!fmt) return 0;
        av_log_set_level(AV_LOG_PANIC);
    }

    if (size < 16) return 0;

    // Allocate IO buffer
    io_buffer = av_malloc(io_buffer_size);
    if (!io_buffer) return 0;

    pb = avio_alloc_context(io_buffer, io_buffer_size, 1, NULL, NULL, write_packet, NULL);
    if (!pb) {
        av_free(io_buffer);
        return 0;
    }

    ret = avformat_alloc_output_context2(&oc, fmt, NULL, NULL);
    if (ret < 0 || !oc) {
        avio_context_free(&pb);
        return 0;
    }
    oc->pb = pb;

    // Parse fuzz data to create streams
    // First byte: number of streams (cap at 10)
    int nb_streams = data[0] % 10;
    if (nb_streams == 0) nb_streams = 1;
    data++; 
    size--;

    for (int i = 0; i < nb_streams; i++) {
        AVStream *st = avformat_new_stream(oc, NULL);
        if (!st) break;
        
        // Randomize stream codec parameters
        if (size < 4) break;
        uint32_t codec_tag = AV_RL32(data);
        data += 4; size -= 4;
        
        st->codecpar->codec_type = (i % 2 == 0) ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = AV_CODEC_ID_H264; // Default to something common
        st->codecpar->codec_tag = codec_tag;
        st->time_base = (AVRational){1, 25};
        
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            st->codecpar->width = 640;
            st->codecpar->height = 480;
        } else {
            st->codecpar->sample_rate = 44100;
            av_channel_layout_default(&st->codecpar->ch_layout, 2);
        }
    }

    // Write header
    if (avformat_write_header(oc, NULL) < 0) {
        goto end;
    }

    pkt = av_packet_alloc();
    if (!pkt) goto end;

    // Packet loop
    while (size > 16) {
        // [stream_idx 1b][pts 8b][dts 8b][flags 1b][size 2b][data ...]
        uint8_t stream_idx = data[0];
        int64_t pts = AV_RL64(data + 1);
        int64_t dts = AV_RL64(data + 9);
        int flags = data[17];
        int payload_size = AV_RL16(data + 18);
        data += 20; size -= 20;

        if (oc->nb_streams == 0) break;
        stream_idx %= oc->nb_streams;

        if (payload_size > size) payload_size = size;

        // Populate packet
        av_new_packet(pkt, payload_size);
        memcpy(pkt->data, data, payload_size);
        pkt->stream_index = stream_idx;
        pkt->pts = pts;
        pkt->dts = dts;
        pkt->flags = flags;

        // Mux
        av_interleaved_write_frame(oc, pkt);
        av_packet_unref(pkt);

        data += payload_size;
        size -= payload_size;
    }

    av_write_trailer(oc);

end:
    av_packet_free(&pkt);
    // free output context before IO because it might flush
    if (oc) avformat_free_context(oc);
    if (pb) avio_context_free(&pb); 
    // io_buffer is freed by avio_context_free
    
    return 0;
}
