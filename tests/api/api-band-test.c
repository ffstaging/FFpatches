/*
 * Copyright (c) 2015 Ludmila Glinskih
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * draw_horiz_band test.
 */

#include "libavutil/adler32.h"
#include "libavutil/mem.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"

uint8_t *slice_byte_buffer;
int draw_horiz_band_called;

static void draw_horiz_band(AVCodecContext *ctx, const AVFrame *fr, int offset[4],
                            int slice_position, int type, int height)
{
    int i;
    const AVPixFmtDescriptor *pix_fmt_desc;
    int chroma_w, chroma_h;
    int shift_slice_position;
    int shift_height;

    draw_horiz_band_called = 1;

    pix_fmt_desc = av_pix_fmt_desc_get(ctx->pix_fmt);
    chroma_w = -((-ctx->width) >> pix_fmt_desc->log2_chroma_w);
    chroma_h = -((-height) >> pix_fmt_desc->log2_chroma_h);
    shift_slice_position = -((-slice_position) >> pix_fmt_desc->log2_chroma_h);
    shift_height = -((-ctx->height) >> pix_fmt_desc->log2_chroma_h);

    for (i = 0; i < height; i++) {
        memcpy(slice_byte_buffer + ctx->width * slice_position + i * ctx->width,
               fr->data[0] + offset[0] + i * fr->linesize[0], ctx->width);
    }
    for (i = 0; i < chroma_h; i++) {
        memcpy(slice_byte_buffer + ctx->width * ctx->height + chroma_w * shift_slice_position + i * chroma_w,
               fr->data[1] + offset[1] + i * fr->linesize[1], chroma_w);
    }
    for (i = 0; i < chroma_h; i++) {
        memcpy(slice_byte_buffer + ctx->width * ctx->height + chroma_w * shift_height + chroma_w * shift_slice_position + i * chroma_w,
               fr->data[2] + offset[2] + i * fr->linesize[2], chroma_w);
    }
}

static int video_decode(const char *input_filename)
{
    const AVCodec *codec = NULL;
    AVCodecContext *ctx= NULL;
    AVCodecParameters *origin_par = NULL;
    uint8_t *byte_buffer = NULL;
    AVFrame *fr = NULL;
    AVPacket *pkt;
    AVFormatContext *fmt_ctx = NULL;
    int number_of_written_bytes;
    int video_stream;
    int byte_buffer_size;
    int result;

    draw_horiz_band_called = 0;

    result = avformat_open_input(&fmt_ctx, input_filename, NULL, NULL);
    if (result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't open file\n");
        return result;
    }

    result = avformat_find_stream_info(fmt_ctx, NULL);
    if (result < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't get stream info\n");
        goto close_input;
    }

    video_stream = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream < 0) {
      av_log(NULL, AV_LOG_ERROR, "Can't find video stream in input file\n");
      result = -1;
      goto close_input;
    }

    origin_par = fmt_ctx->streams[video_stream]->codecpar;

    codec = avcodec_find_decoder(origin_par->codec_id);
    if (!codec) {
        av_log(NULL, AV_LOG_ERROR, "Can't find decoder\n");
        result = -1;
        goto close_input;
    }

    if (!(codec->capabilities & AV_CODEC_CAP_DRAW_HORIZ_BAND)) {
        av_log(NULL, AV_LOG_ERROR, "Codec does not support draw_horiz_band\n");
        result = -1;
        goto close_input;
    }

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate decoder context\n");
        result = AVERROR(ENOMEM);
        goto close_input;
    }

    result = avcodec_parameters_to_context(ctx, origin_par);
    if (result) {
        av_log(NULL, AV_LOG_ERROR, "Can't copy decoder context\n");
        goto free_ctx;
    }

    ctx->draw_horiz_band = draw_horiz_band;
    ctx->thread_count = 1;

    result = avcodec_open2(ctx, codec, NULL);
    if (result < 0) {
        av_log(ctx, AV_LOG_ERROR, "Can't open decoder\n");
        goto free_ctx;
    }

    fr = av_frame_alloc();
    if (!fr) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate frame\n");
        result = AVERROR(ENOMEM);
        goto free_ctx;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        av_log(NULL, AV_LOG_ERROR, "Cannot allocate packet\n");
        result = AVERROR(ENOMEM);
        goto free_fr;
    }

    byte_buffer_size = av_image_get_buffer_size(ctx->pix_fmt, ctx->width, ctx->height, 32);
    byte_buffer = av_malloc(byte_buffer_size);
    if (!byte_buffer) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate buffer\n");
        result = AVERROR(ENOMEM);
        goto free_pkt;
    }

    slice_byte_buffer = av_malloc(byte_buffer_size);
    if (!slice_byte_buffer) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate buffer\n");
        result = AVERROR(ENOMEM);
        goto free_byte_buffer;
    }
    memset(slice_byte_buffer, 0, byte_buffer_size);

    result = 0;
    while (result >= 0) {
        result = av_read_frame(fmt_ctx, pkt);
        if (result >= 0 && pkt->stream_index != video_stream) {
            av_packet_unref(pkt);
            continue;
        }

        // pkt will be empty on read error/EOF
        result = avcodec_send_packet(ctx, pkt);

        av_packet_unref(pkt);

        if (result < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error submitting a packet for decoding\n");
            goto free_slice_byte_buffer;
        }

        while (result >= 0) {
            result = avcodec_receive_frame(ctx, fr);
            if (result == AVERROR_EOF)
                goto free_slice_byte_buffer;
            else if (result == AVERROR(EAGAIN)) {
                result = 0;
                break;
            } else if (result < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error decoding frame\n");
                goto free_slice_byte_buffer;
            }

            number_of_written_bytes = av_image_copy_to_buffer(byte_buffer, byte_buffer_size,
                                    (const uint8_t* const *)fr->data, (const int*) fr->linesize,
                                    ctx->pix_fmt, ctx->width, ctx->height, 1);
            if (number_of_written_bytes < 0) {
                av_log(NULL, AV_LOG_ERROR, "Can't copy image to buffer\n");
                result = number_of_written_bytes;
                goto free_slice_byte_buffer;
            }
            if (draw_horiz_band_called == 0) {
                av_log(NULL, AV_LOG_ERROR, "draw_horiz_band haven't been called!\n");
                result = -1;
                goto free_slice_byte_buffer;
            }
            if (av_adler32_update(0, (const uint8_t*)byte_buffer, number_of_written_bytes) !=
                av_adler32_update(0, (const uint8_t*)slice_byte_buffer, number_of_written_bytes)) {
                av_log(NULL, AV_LOG_ERROR, "Decoded frames with and without draw_horiz_band are not the same!\n");
                result = -1;
                goto free_slice_byte_buffer;
            }
            av_frame_unref(fr);
        }
    }

    result = 0;

free_slice_byte_buffer:
    av_freep(&slice_byte_buffer);
free_byte_buffer:
    av_freep(&byte_buffer);
free_pkt:
    av_packet_free(&pkt);
free_fr:
    av_frame_free(&fr);
free_ctx:
    avcodec_free_context(&ctx);
close_input:
    avformat_close_input(&fmt_ctx);
    return result;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        av_log(NULL, AV_LOG_ERROR, "Incorrect input: expected %s <name of a video file>\n", argv[0]);
        return 1;
    }

    if (video_decode(argv[1]) != 0)
        return 1;

    return 0;
}
