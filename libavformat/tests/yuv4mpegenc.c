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

#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavcodec/codec_par.h"
#include "libavcodec/packet.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/pixfmt.h"

static int mux_once(enum AVPixelFormat pix_fmt,
                    AVRational sar, enum AVFieldOrder field_order,
                    enum AVColorRange color_range,
                    int expect_fail)
{
    AVFormatContext   *oc  = NULL;
    AVStream          *st;
    uint8_t           *buf = NULL;
    int                ret;

    ret = avformat_alloc_output_context2(&oc, NULL, "yuv4mpegpipe", NULL);
    if (ret < 0)
        return ret;

    st = avformat_new_stream(oc, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    st->codecpar->codec_type  = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id    = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->format      = pix_fmt;
    st->codecpar->width       = 16;
    st->codecpar->height      = 16;
    st->codecpar->field_order = field_order;
    st->codecpar->color_range = color_range;

    st->sample_aspect_ratio   = sar;
    st->time_base             = (AVRational){ 1, 25 };
    st->avg_frame_rate        = (AVRational){ 25, 1 };

    ret = avio_open_dyn_buf(&oc->pb);
    if (ret < 0)
        goto end;

    ret = avformat_write_header(oc, NULL);
    if (ret < 0) {
        avio_close_dyn_buf(oc->pb, &buf);
        oc->pb = NULL;
        av_free(buf);
        goto end;
    }

    /* Exercise write_packet with a valid raw YUV420P frame */
    if (pix_fmt == AV_PIX_FMT_YUV420P) {
        AVPacket *pkt = av_packet_alloc();
        if (pkt) {
            /* YUV420P 16x16: Y=256, U=64, V=64 = 384 bytes */
            if (av_new_packet(pkt, 384) == 0) {
                pkt->stream_index = 0;
                pkt->pts = pkt->dts = 0;
                pkt->duration = 1;
                av_write_frame(oc, pkt);
            }
            av_packet_free(&pkt);
        }
    }

    av_write_trailer(oc);
    avio_close_dyn_buf(oc->pb, &buf);
    oc->pb = NULL;
    av_free(buf);

end:
    if (oc->pb) {
        avio_close_dyn_buf(oc->pb, &buf);
        av_free(buf);
        oc->pb = NULL;
    }
    avformat_free_context(oc);

    if (expect_fail)
        return (ret < 0) ? 0 : AVERROR(EINVAL);
    return ret;
}

int main(void)
{
    int ret;
    const AVRational sar11 = { 1, 1 };
    const AVRational sar43 = { 4, 3 };
    const AVRational sar01 = { 0, 1 };

    av_log_set_level(AV_LOG_QUIET);

    ret = mux_once(AV_PIX_FMT_YUV420P, sar11, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 1;

    ret = mux_once(AV_PIX_FMT_YUV422P, sar11, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 2;

    ret = mux_once(AV_PIX_FMT_YUV444P, sar11, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 3;

    ret = mux_once(AV_PIX_FMT_GRAY8, sar11, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 4;

    ret = mux_once(AV_PIX_FMT_YUV411P, sar11, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 5;

    ret = mux_once(AV_PIX_FMT_YUVJ420P, sar11, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 6;

    ret = mux_once(AV_PIX_FMT_YUVJ422P, sar11, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 7;

    ret = mux_once(AV_PIX_FMT_YUVJ444P, sar11, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 8;

    ret = mux_once(AV_PIX_FMT_YUV420P, sar43, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 9;

    ret = mux_once(AV_PIX_FMT_YUV420P, sar01, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 10;

    ret = mux_once(AV_PIX_FMT_YUV420P, sar11, AV_FIELD_TT, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 11;

    ret = mux_once(AV_PIX_FMT_YUV420P, sar11, AV_FIELD_BB, AVCOL_RANGE_UNSPECIFIED, 0);
    if (ret < 0) return 12;

    ret = mux_once(AV_PIX_FMT_YUV420P, sar11, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_MPEG, 0);
    if (ret < 0) return 13;

    ret = mux_once(AV_PIX_FMT_YUV420P, sar11, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_JPEG, 0);
    if (ret < 0) return 14;

    /* unsupported pixel format must fail */
    ret = mux_once(AV_PIX_FMT_RGB24, sar11, AV_FIELD_PROGRESSIVE, AVCOL_RANGE_UNSPECIFIED, 1);
    if (ret < 0) return 15;

    return 0;
}
