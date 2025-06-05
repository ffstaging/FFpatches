/*
 * Copyright (c) 2025, Faeez Kadiri < f1k2faeez at gmail dot com>
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


#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"

#include "libavfilter/filters.h"
#include "libavfilter/formats.h"
#include "cuda_vpp.h"
#include "load_helper.h"

int ff_cuda_vpp_query_formats(const AVFilterContext *avctx,
                              AVFilterFormatsConfig **cfg_in,
                              AVFilterFormatsConfig **cfg_out)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_CUDA, AV_PIX_FMT_NONE,
    };
    int err;

    err = ff_set_common_formats_from_list2(avctx, cfg_in, cfg_out, pix_fmts);
    if (err < 0)
        return err;

    return 0;
}

int ff_cuda_vpp_config_input(AVFilterLink *inlink)
{
    FilterLink          *l = ff_filter_link(inlink);
    AVFilterContext *avctx = inlink->dst;
    CUDAVPPContext *ctx    = avctx->priv;

    if (ctx->pipeline_uninit)
        ctx->pipeline_uninit(avctx);

    if (!l->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "A hardware frames reference is "
               "required to associate the processing device.\n");
        return AVERROR(EINVAL);
    }

    ctx->input_frames_ref = av_buffer_ref(l->hw_frames_ctx);
    if (!ctx->input_frames_ref) {
        av_log(avctx, AV_LOG_ERROR, "A input frames reference create "
               "failed.\n");
        return AVERROR(ENOMEM);
    }
    ctx->input_frames = (AVHWFramesContext*)ctx->input_frames_ref->data;

    return 0;
}

int ff_cuda_vpp_config_output(AVFilterLink *outlink)
{
    FilterLink       *outl = ff_filter_link(outlink);
    AVFilterContext *avctx = outlink->src;
    AVFilterLink   *inlink = avctx->inputs[0];
    FilterLink        *inl = ff_filter_link(inlink);
    CUDAVPPContext *ctx    = avctx->priv;
    AVHWFramesContext *input_frames;
    AVBufferRef *hw_frames_ctx;
    AVHWFramesContext *output_frames;
    enum AVPixelFormat in_format;
    int err;

    if (ctx->pipeline_uninit)
        ctx->pipeline_uninit(avctx);

    if (!ctx->output_width)
        ctx->output_width  = avctx->inputs[0]->w;
    if (!ctx->output_height)
        ctx->output_height = avctx->inputs[0]->h;

    outlink->w = ctx->output_width;
    outlink->h = ctx->output_height;

    if (ctx->passthrough) {
        if (inl->hw_frames_ctx)
            outl->hw_frames_ctx = av_buffer_ref(inl->hw_frames_ctx);
        av_log(ctx, AV_LOG_VERBOSE, "Using CUDA filter passthrough mode.\n");
        return 0;
    }

    av_assert0(ctx->input_frames);
    ctx->device_ref = av_buffer_ref(ctx->input_frames->device_ref);
    if (!ctx->device_ref) {
        av_log(avctx, AV_LOG_ERROR, "A device reference create "
               "failed.\n");
        return AVERROR(ENOMEM);
    }

    input_frames = (AVHWFramesContext*)ctx->input_frames_ref->data;
    in_format = input_frames->sw_format;

    ctx->hwctx = input_frames->device_ctx->hwctx;
    ctx->cuda_dl = ctx->hwctx->internal->cuda_dl;
    ctx->cu_stream = ctx->hwctx->stream;

    if (ctx->output_format == AV_PIX_FMT_NONE)
        ctx->output_format = input_frames->sw_format;

    // Setup format information
    err = ff_cuda_vpp_setup_planes(ctx, in_format);
    if (err < 0)
        return err;

    // Load filter-specific functions
    if (ctx->load_functions) {
        err = ctx->load_functions(avctx, in_format);
        if (err < 0)
            return err;
    }

    // Build filter parameters
    if (ctx->build_filter_params) {
        err = ctx->build_filter_params(avctx);
        if (err < 0)
            return err;
    }

    // Initialize hardware frames context for output
    hw_frames_ctx = av_hwframe_ctx_alloc(ctx->device_ref);
    if (!hw_frames_ctx)
        return AVERROR(ENOMEM);

    output_frames = (AVHWFramesContext*)hw_frames_ctx->data;
    output_frames->format = AV_PIX_FMT_CUDA;
    output_frames->sw_format = ctx->output_format;
    output_frames->width = ctx->output_width;
    output_frames->height = ctx->output_height;

    err = av_hwframe_ctx_init(hw_frames_ctx);
    if (err < 0) {
        av_buffer_unref(&hw_frames_ctx);
        return err;
    }

    av_buffer_unref(&outl->hw_frames_ctx);
    outl->hw_frames_ctx = hw_frames_ctx;

    return 0;
}

int ff_cuda_vpp_format_is_supported(enum AVPixelFormat fmt, const enum AVPixelFormat *supported_formats, int nb_formats)
{
    int i;

    for (i = 0; i < nb_formats; i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

int ff_cuda_vpp_setup_planes(CUDAVPPContext *s, enum AVPixelFormat format)
{
    s->in_fmt = format;
    s->in_desc = av_pix_fmt_desc_get(s->in_fmt);
    s->in_planes = av_pix_fmt_count_planes(s->in_fmt);

    // Clear plane information
    memset(s->in_plane_depths, 0, sizeof(s->in_plane_depths));
    memset(s->in_plane_channels, 0, sizeof(s->in_plane_channels));

    // Set up plane information
    for (int i = 0; i < s->in_desc->nb_components; i++) {
        int d = (s->in_desc->comp[i].depth + 7) / 8;
        int p = s->in_desc->comp[i].plane;
        s->in_plane_channels[p] = FFMAX(s->in_plane_channels[p], s->in_desc->comp[i].step / d);
        s->in_plane_depths[p] = s->in_desc->comp[i].depth;
    }

    return 0;
}

int ff_cuda_vpp_load_module(AVFilterContext *ctx, CUDAVPPContext *s,
                           const unsigned char *ptx_data, unsigned int ptx_len)
{
    CUcontext dummy, cuda_ctx = s->hwctx->cuda_ctx;
    CudaFunctions *cu = s->cuda_dl;
    int ret;

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, s->hwctx, &s->cu_module, ptx_data, ptx_len);
    if (ret < 0)
        goto fail;

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ret;
}

int ff_cuda_vpp_get_function(AVFilterContext *ctx, CUDAVPPContext *s,
                            CUfunction *func, const char *func_name)
{
    CudaFunctions *cu = s->cuda_dl;
    int ret;

    ret = CHECK_CU(cu->cuModuleGetFunction(func, s->cu_module, func_name));
    if (ret < 0) {
        av_log(ctx, AV_LOG_FATAL, "Failed to load function: %s\n", func_name);
        return AVERROR(ENOSYS);
    }

    return 0;
}

void ff_cuda_vpp_ctx_init(AVFilterContext *avctx)
{
    CUDAVPPContext *ctx = avctx->priv;

    ctx->cu_module = NULL;
    ctx->passthrough = 0;
}

void ff_cuda_vpp_ctx_uninit(AVFilterContext *avctx)
{
    CUDAVPPContext *ctx = avctx->priv;

    if (ctx->pipeline_uninit)
        ctx->pipeline_uninit(avctx);

    av_buffer_unref(&ctx->input_frames_ref);
    av_buffer_unref(&ctx->device_ref);
}
