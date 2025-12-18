/*
 * Fuzzer for libavfilter
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
#include "libavutil/buffer.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void error(const char *err)
{
    fprintf(stderr, "%s", err);
    exit(1);
}

// Global filter to fuzz. Initialize once if possible or via define.
static const AVFilter *f = NULL;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int ret;
    AVFilterGraph *graph = NULL;
    AVFilterContext *filt_ctx = NULL, *src_ctx = NULL, *sink_ctx = NULL;
    AVFrame *frame = NULL;
    uint8_t *dummy_data = NULL;
    const uint8_t *end = data + size;
    char *options_str = NULL;
    int options_len = 0;

    // Ensure filter is set (compiled in)
    if (!f) {
#ifdef FFMPEG_FILTER
#define XSTR(s) STR(s)
#define STR(s) #s
        f = avfilter_get_by_name(XSTR(FFMPEG_FILTER));
#endif
        if (!f) {
             fprintf(stderr, "Filter not found: %s\n", XSTR(FFMPEG_FILTER));
             return 0; 
        }
        av_log_set_level(AV_LOG_PANIC);
    }

    graph = avfilter_graph_alloc();
    if (!graph) error("Failed to allocate graph");

    // We need at least some data for options and frame params
    // Let's reserve up to 1024 bytes for options if available
    if (size > 0) {
        options_len = size > 512 ? 512 : size;
        options_str = av_malloc(options_len + 1);
        if (!options_str) error("Failed to allocate options");
        memcpy(options_str, data, options_len);
        options_str[options_len] = 0;
        data += options_len;
        size -= options_len;
    }

    // Determine filter type to setup source/sink
    int is_audio = 0;
    if (f->inputs && avfilter_pad_get_type(f->inputs, 0) == AVMEDIA_TYPE_AUDIO) is_audio = 1;
    // Some filters might have no inputs (sources), handled separately?
    // For now assume 1 input 1 output or similar standard filters.
    
    const AVFilter *buffersrc = avfilter_get_by_name(is_audio ? "abuffer" : "buffer");
    const AVFilter *buffersink = avfilter_get_by_name(is_audio ? "abuffersink" : "buffersink");
    
    char src_args[256];
    if (is_audio) {
         snprintf(src_args, sizeof(src_args), "time_base=1/44100:sample_rate=44100:sample_fmt=flt:channel_layout=stereo");
    } else {
         snprintf(src_args, sizeof(src_args), "video_size=640x480:pix_fmt=yuv420p:time_base=1/25:pixel_aspect=1/1");
    }

    ret = avfilter_graph_create_filter(&src_ctx, buffersrc, "in", src_args, NULL, graph);
    if (ret < 0) goto end;

    ret = avfilter_graph_create_filter(&filt_ctx, f, "filter", options_str, NULL, graph);
    if (ret < 0) goto end;

    ret = avfilter_graph_create_filter(&sink_ctx, buffersink, "out", NULL, NULL, graph);
    if (ret < 0) goto end;

    // Link
    if (src_ctx && filt_ctx) {
        ret = avfilter_link(src_ctx, 0, filt_ctx, 0);
        if (ret < 0) goto end;
    }
    if (filt_ctx && sink_ctx) {
        ret = avfilter_link(filt_ctx, 0, sink_ctx, 0);
        if (ret < 0) goto end;
    }

    ret = avfilter_graph_config(graph, NULL);
    if (ret < 0) goto end;

    // Send a frame
    frame = av_frame_alloc();
    if (!frame) error("Failed to allocate frame");
    
    if (is_audio) {
        frame->nb_samples = 1024;
        frame->format = AV_SAMPLE_FMT_FLT;
        av_channel_layout_default(&frame->ch_layout, 2);
        frame->sample_rate = 44100;
    } else {
        frame->width = 640;
        frame->height = 480;
        frame->format = AV_PIX_FMT_YUV420P;
    }
    
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) goto end;

    // Fill frame with fuzz data if available
    if (size > 0) {
        int copy_size = size;
        // Naive fill, just copy into first plane
        if (copy_size > frame->buf[0]->size) copy_size = frame->buf[0]->size;
        memcpy(frame->data[0], data, copy_size);
    }

    ret = av_buffersrc_add_frame(src_ctx, frame);
    if (ret < 0) goto end;

    // Receive output
    while (1) {
        AVFrame *out = av_frame_alloc();
        ret = av_buffersink_get_frame(sink_ctx, out);
        av_frame_free(&out);
        if (ret < 0) break;
    }

end:
    if (options_str) av_free(options_str);
    if (frame) av_frame_free(&frame);
    if (graph) avfilter_graph_free(&graph);
    
    return 0;
}
