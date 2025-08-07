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

/**
 * @file
 * Copy the luma value of the second input into the alpha channel of the first input using CUDA.
 */

#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/mem.h"

#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "framesync.h"

#include "cuda/load_helper.h"

#define CHECK_CU(call) FF_CUDA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, call)
#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )

#define BLOCK_X 32
#define BLOCK_Y 16

#define MAIN_INPUT 0
#define ALPHA_INPUT 1

#define ALPHA_PLANE_INDEX 3

static const enum AVPixelFormat supported_main_formats[] = {
    AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_NONE,
};

static const enum AVPixelFormat supported_alpha_mask_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NONE,
};

typedef struct AlphaMergeCUDAContext {
    const AVClass *class;

    enum AVPixelFormat sw_format_main;
    enum AVPixelFormat sw_format_alpha_mask;

    AVBufferRef *hw_device_ctx;
    AVCUDADeviceContext *hwctx;

    CUcontext cu_ctx;
    CUmodule cu_module;
    CUfunction cu_func_alphamerge_planar;
    CUstream cu_stream;

    FFFrameSync fs;

    int alpha_plane_idx;

} AlphaMergeCUDAContext;


static int format_is_supported(const enum AVPixelFormat supported_formats[], enum AVPixelFormat fmt)
{
    for (int i = 0; supported_formats[i] != AV_PIX_FMT_NONE; i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    static const int pix_fmts[] = { AV_PIX_FMT_CUDA, AV_PIX_FMT_NONE };
    static const int alpha_mask_ranges[] = { AVCOL_RANGE_JPEG };
    AVFilterFormats *formats = NULL;
    int ret = 0;

    formats = ff_make_format_list(pix_fmts);
    if (!formats)
        return AVERROR(ENOMEM);

    if ((ret = ff_set_common_formats2(ctx, cfg_in, cfg_out, ff_make_format_list(pix_fmts))) < 0)
        return ret;

    formats = ff_make_format_list(alpha_mask_ranges);
    if (!formats)
        return AVERROR(ENOMEM);

    ret = ff_formats_ref(formats, &cfg_in[ALPHA_INPUT]->color_ranges);
    ff_formats_unref(&formats);
    if (ret < 0)
        return ret;

    return 0;
}

static int do_alphamerge_cuda(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AlphaMergeCUDAContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *main_frame = NULL;
    AVFrame *alpha_mask_frame = NULL;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    CUcontext dummy_cu_ctx;
    int ret;

    ret = ff_framesync_dualinput_get_writable(fs, &main_frame, &alpha_mask_frame);
    if (ret < 0)
        return ret;

    if (!alpha_mask_frame)
        return ff_filter_frame(outlink, main_frame);

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->cu_ctx));
    if (ret < 0)
        return ret;

    void *kernel_args[] = {
        &main_frame->data[s->alpha_plane_idx],
        &main_frame->linesize[s->alpha_plane_idx],
        &alpha_mask_frame->data[0],
        &alpha_mask_frame->linesize[0],
        &main_frame->width,
        &main_frame->height
    };
    unsigned int grid_x = DIV_UP(main_frame->width, BLOCK_X);
    unsigned int grid_y = DIV_UP(main_frame->height, BLOCK_Y);

    ret = CHECK_CU(cu->cuLaunchKernel(s->cu_func_alphamerge_planar, grid_x, grid_y, 1,
                                      BLOCK_X, BLOCK_Y, 1,
                                      0, s->cu_stream, kernel_args, NULL));

    CHECK_CU(cu->cuCtxPopCurrent(&dummy_cu_ctx));

    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to launch CUDA kernel\n");
        return ret;
    }

    return ff_filter_frame(outlink, main_frame);
}


static int alphamerge_cuda_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AlphaMergeCUDAContext *s = ctx->priv;

    AVFilterLink *main_inlink = ctx->inputs[MAIN_INPUT];
    AVFilterLink *alpha_inlink = ctx->inputs[ALPHA_INPUT];

    FilterLink *main_inl = ff_filter_link(main_inlink);
    FilterLink *alpha_inl = ff_filter_link(alpha_inlink);

    AVHWFramesContext *main_frames_ctx = (AVHWFramesContext*)main_inl->hw_frames_ctx->data;
    AVHWFramesContext *alpha_frames_ctx = (AVHWFramesContext*)alpha_inl->hw_frames_ctx->data;

    const AVPixFmtDescriptor *main_desc;
    CUcontext dummy_cu_ctx;
    CudaFunctions *cu;
    int ret = 0;

    extern const unsigned char ff_vf_alphamerge_cuda_ptx_data[];
    extern const unsigned int ff_vf_alphamerge_cuda_ptx_len;

    s->sw_format_main = main_frames_ctx->sw_format;
    if (!format_is_supported(supported_main_formats, s->sw_format_main)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported main input software pixel format: %s\n",
               av_get_pix_fmt_name(s->sw_format_main));
        return AVERROR(ENOSYS);
    }

    s->sw_format_alpha_mask = alpha_frames_ctx->sw_format;
    if (!format_is_supported(supported_alpha_mask_formats, s->sw_format_alpha_mask)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported alpha mask input software pixel format: %s.\n",
               av_get_pix_fmt_name(s->sw_format_alpha_mask));
        return AVERROR(ENOSYS);
    }

    if (main_inlink->w != alpha_inlink->w || main_inlink->h != alpha_inlink->h) {
        av_log(ctx, AV_LOG_ERROR, "Input frame sizes do not match (%dx%d vs %dx%d).\n",
               main_inlink->w, main_inlink->h, alpha_inlink->w, alpha_inlink->h);
        return AVERROR(EINVAL);
    }

    s->hw_device_ctx = av_buffer_ref(main_frames_ctx->device_ref);
    if (!s->hw_device_ctx)
        return AVERROR(ENOMEM);

    s->hwctx = ((AVHWDeviceContext*)s->hw_device_ctx->data)->hwctx;
    s->cu_ctx = s->hwctx->cuda_ctx;
    s->cu_stream = s->hwctx->stream;
    cu = s->hwctx->internal->cuda_dl;

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->cu_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, s->hwctx, &s->cu_module,
                              ff_vf_alphamerge_cuda_ptx_data, ff_vf_alphamerge_cuda_ptx_len);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load CUDA module.\n");
        goto end;
    }

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_alphamerge_planar, s->cu_module, "alphamerge_planar"));
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get kernel function 'alphamerge_planar'.\n");
        goto end;
    }

    main_desc = av_pix_fmt_desc_get(s->sw_format_main);
    if (!main_desc || !(main_desc->flags & AV_PIX_FMT_FLAG_ALPHA)) {
        av_log(ctx, AV_LOG_ERROR, "Main input sw_format %s is not a supported format with an alpha channel.\n",
               av_get_pix_fmt_name(s->sw_format_main));
        ret = AVERROR(EINVAL);
        goto end;
    }
    s->alpha_plane_idx = main_desc->comp[ALPHA_PLANE_INDEX].plane;

    ff_filter_link(outlink)->hw_frames_ctx = av_buffer_ref(main_inl->hw_frames_ctx);
    if (!ff_filter_link(outlink)->hw_frames_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    s->fs.time_base = main_inlink->time_base;
    if ((ret = ff_framesync_init_dualinput(&s->fs, ctx)) < 0)
        goto end;

    outlink->w = main_inlink->w;
    outlink->h = main_inlink->h;
    outlink->time_base = main_inlink->time_base;
    outlink->sample_aspect_ratio = main_inlink->sample_aspect_ratio;
    ff_filter_link(outlink)->frame_rate = ff_filter_link(main_inlink)->frame_rate;

    ret = ff_framesync_configure(&s->fs);

end:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy_cu_ctx));
    return ret;
}

static av_cold int alphamerge_cuda_init(AVFilterContext *ctx)
{
    AlphaMergeCUDAContext *s = ctx->priv;
    s->fs.on_event = &do_alphamerge_cuda;
    return 0;
}

static av_cold void alphamerge_cuda_uninit(AVFilterContext *ctx)
{
    AlphaMergeCUDAContext *s = ctx->priv;
    CUcontext dummy;

    ff_framesync_uninit(&s->fs);

    if (s->cu_module) {
        CudaFunctions *cu = s->hwctx->internal->cuda_dl;
        CHECK_CU(cu->cuCtxPushCurrent(s->cu_ctx));

        if (s->cu_stream)
            CHECK_CU(cu->cuStreamSynchronize(s->cu_stream));

        CHECK_CU(cu->cuModuleUnload(s->cu_module));
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    }

    av_buffer_unref(&s->hw_device_ctx);
}

static int alphamerge_cuda_activate(AVFilterContext *ctx)
{
    AlphaMergeCUDAContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static const AVFilterPad alphamerge_cuda_inputs[] = {
    {
        .name = "main",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name = "alpha",
        .type = AVMEDIA_TYPE_VIDEO,
    }
};

static const AVFilterPad alphamerge_cuda_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &alphamerge_cuda_config_output,
    }
};

static const AVOption alphamerge_cuda_options[] = {
    { NULL },
};

FRAMESYNC_DEFINE_CLASS(alphamerge_cuda, AlphaMergeCUDAContext, fs);

const FFFilter ff_vf_alphamerge_cuda = {
    .p.name          = "alphamerge_cuda",
    .p.description   = NULL_IF_CONFIG_SMALL("Copy the luma value of the second input into the alpha channel of the first input using CUDA."),

    .priv_size     = sizeof(AlphaMergeCUDAContext),
    .p.priv_class    = &alphamerge_cuda_class,

    .init          = &alphamerge_cuda_init,
    .uninit        = &alphamerge_cuda_uninit,

    .activate      = &alphamerge_cuda_activate,
    FILTER_INPUTS(alphamerge_cuda_inputs),
    FILTER_OUTPUTS(alphamerge_cuda_outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .preinit       = alphamerge_cuda_framesync_preinit,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};