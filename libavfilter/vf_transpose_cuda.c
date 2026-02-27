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

#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"
#include "transpose.h"

#include "cuda/load_helper.h"

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_P016,
    AV_PIX_FMT_0RGB32,
    AV_PIX_FMT_0BGR32,
    AV_PIX_FMT_RGB32,
    AV_PIX_FMT_BGR32,
};

#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define BLOCKX 32
#define BLOCKY 16

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

typedef struct TransposeCUDAContext {
    const AVClass *class;

    AVCUDADeviceContext *hwctx;
    AVBufferRef *device_ref;

    const AVPixFmtDescriptor *in_desc;
    int in_planes;
    int in_plane_depths[4];
    int in_plane_channels[4];

    CUmodule    cu_module;
    CUstream    cu_stream;

    CUfunction  cu_func;
    CUfunction  cu_func_uv;

    int passthrough_mode;
    int dir;
} TransposeCUDAContext;

static int format_is_supported(enum AVPixelFormat fmt)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static av_cold void set_format_info(AVFilterContext *ctx, enum AVPixelFormat format)
{
    TransposeCUDAContext *s = ctx->priv;

    s->in_desc = av_pix_fmt_desc_get(format);
    s->in_planes = av_pix_fmt_count_planes(format);

    for (int i = 0; i < s->in_desc->nb_components; i++) {
        int d = (s->in_desc->comp[i].depth + 7) / 8;
        int p = s->in_desc->comp[i].plane;
        s->in_plane_channels[p] = FFMAX(s->in_plane_channels[p],
                                        s->in_desc->comp[i].step / d);
        s->in_plane_depths[p] = s->in_desc->comp[i].depth;
    }
}

static const char *get_func_name(int depth, int channels)
{
    if (channels == 4 && depth <= 8)
        return "Transpose_uchar4";
    if (channels == 2 && depth <= 8)
        return "Transpose_uchar2";
    if (channels == 2 && depth > 8)
        return "Transpose_ushort2";
    if (depth > 8)
        return "Transpose_ushort";
    return "Transpose_uchar";
}

static const char *get_uv_func_name(int depth, int channels)
{
    if (channels >= 2 && depth <= 8)
        return "Transpose_uchar2";
    if (channels >= 2 && depth > 8)
        return "Transpose_ushort2";
    if (depth > 8)
        return "Transpose_ushort_uv";
    return "Transpose_uchar_uv";
}

static av_cold int transpose_cuda_load_functions(AVFilterContext *ctx)
{
    TransposeCUDAContext *s = ctx->priv;
    CUcontext dummy, cuda_ctx = s->hwctx->cuda_ctx;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    const char *func_name;
    int ret;

    extern const unsigned char ff_vf_transpose_cuda_ptx_data[];
    extern const unsigned int  ff_vf_transpose_cuda_ptx_len;

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, s->hwctx, &s->cu_module,
                              ff_vf_transpose_cuda_ptx_data,
                              ff_vf_transpose_cuda_ptx_len);
    if (ret < 0)
        goto fail;

    func_name = get_func_name(s->in_plane_depths[0], s->in_plane_channels[0]);
    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func, s->cu_module, func_name));
    if (ret < 0) {
        av_log(ctx, AV_LOG_FATAL, "Failed loading %s\n", func_name);
        goto fail;
    }

    if (s->in_planes > 1) {
        func_name = get_uv_func_name(s->in_plane_depths[1], s->in_plane_channels[1]);
        ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_uv, s->cu_module, func_name));
        if (ret < 0) {
            av_log(ctx, AV_LOG_FATAL, "Failed loading %s\n", func_name);
            goto fail;
        }
    }

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ret;
}

static av_cold int init_processing_chain(AVFilterContext *ctx,
                                         int out_width, int out_height)
{
    TransposeCUDAContext *s = ctx->priv;
    FilterLink *inl  = ff_filter_link(ctx->inputs[0]);
    FilterLink *outl = ff_filter_link(ctx->outputs[0]);
    AVHWFramesContext *in_frames_ctx;
    AVBufferRef *hw_frames_ctx;
    AVHWFramesContext *out_frames_ctx;
    int ret;

    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }
    in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;

    if (!format_is_supported(in_frames_ctx->sw_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported format: %s\n",
               av_get_pix_fmt_name(in_frames_ctx->sw_format));
        return AVERROR(ENOSYS);
    }

    set_format_info(ctx, in_frames_ctx->sw_format);

    s->device_ref = av_buffer_ref(in_frames_ctx->device_ref);
    if (!s->device_ref)
        return AVERROR(ENOMEM);

    s->hwctx     = in_frames_ctx->device_ctx->hwctx;
    s->cu_stream = s->hwctx->stream;

    hw_frames_ctx = av_hwframe_ctx_alloc(s->device_ref);
    if (!hw_frames_ctx)
        return AVERROR(ENOMEM);

    out_frames_ctx = (AVHWFramesContext *)hw_frames_ctx->data;
    out_frames_ctx->format    = AV_PIX_FMT_CUDA;
    out_frames_ctx->sw_format = in_frames_ctx->sw_format;
    out_frames_ctx->width     = out_width;
    out_frames_ctx->height    = out_height;

    ret = av_hwframe_ctx_init(hw_frames_ctx);
    if (ret < 0) {
        av_buffer_unref(&hw_frames_ctx);
        return ret;
    }

    av_buffer_unref(&outl->hw_frames_ctx);
    outl->hw_frames_ctx = hw_frames_ctx;

    return 0;
}

static av_cold void transpose_cuda_uninit(AVFilterContext *ctx)
{
    TransposeCUDAContext *s = ctx->priv;

    if (s->hwctx && s->cu_module) {
        CudaFunctions *cu = s->hwctx->internal->cuda_dl;
        CUcontext dummy;

        CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
        CHECK_CU(cu->cuModuleUnload(s->cu_module));
        s->cu_module = NULL;
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    }

    av_buffer_unref(&s->device_ref);
    s->hwctx = NULL;
}

static int transpose_cuda_call_kernel(AVFilterContext *ctx, CUfunction func,
                                      CUtexObject src_tex[4],
                                      AVFrame *out_frame,
                                      int width, int height,
                                      int dst_width, int dst_height,
                                      int dst_pitch,
                                      int src_width, int src_height,
                                      int dir)
{
    TransposeCUDAContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;

    CUdeviceptr dst_devptr[4] = {
        (CUdeviceptr)out_frame->data[0], (CUdeviceptr)out_frame->data[1],
        (CUdeviceptr)out_frame->data[2], (CUdeviceptr)out_frame->data[3]
    };

    void *args[] = {
        &src_tex[0], &src_tex[1], &src_tex[2], &src_tex[3],
        &dst_devptr[0], &dst_devptr[1], &dst_devptr[2], &dst_devptr[3],
        &width, &height, &dst_pitch,
        &dst_width, &dst_height,
        &src_width, &src_height,
        &dir
    };

    return CHECK_CU(cu->cuLaunchKernel(func,
                                       DIV_UP(width, BLOCKX), DIV_UP(height, BLOCKY), 1,
                                       BLOCKX, BLOCKY, 1,
                                       0, s->cu_stream, args, NULL));
}

static int transpose_cuda_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx    = inlink->dst;
    AVFilterLink *outlink  = ctx->outputs[0];
    TransposeCUDAContext *s = ctx->priv;
    AVFrame *out = NULL;
    CUtexObject tex[4] = { 0, 0, 0, 0 };
    CUcontext dummy;
    CudaFunctions *cu;
    int ret, i;

    if (s->passthrough_mode)
        return ff_filter_frame(outlink, in);

    cu = s->hwctx->internal->cuda_dl;

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
    if (ret < 0)
        return ret;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        goto fail;

    for (i = 0; i < s->in_planes; i++) {
        CUDA_TEXTURE_DESC tex_desc = {
            .filterMode = CU_TR_FILTER_MODE_POINT,
            .flags = CU_TRSF_READ_AS_INTEGER,
        };

        CUDA_RESOURCE_DESC res_desc = {
            .resType = CU_RESOURCE_TYPE_PITCH2D,
            .res.pitch2D.format = s->in_plane_depths[i] <= 8 ?
                                  CU_AD_FORMAT_UNSIGNED_INT8 :
                                  CU_AD_FORMAT_UNSIGNED_INT16,
            .res.pitch2D.numChannels = s->in_plane_channels[i],
            .res.pitch2D.pitchInBytes = in->linesize[i],
            .res.pitch2D.devPtr = (CUdeviceptr)in->data[i],
        };

        if (i == 1 || i == 2) {
            res_desc.res.pitch2D.width  = AV_CEIL_RSHIFT(in->width, s->in_desc->log2_chroma_w);
            res_desc.res.pitch2D.height = AV_CEIL_RSHIFT(in->height, s->in_desc->log2_chroma_h);
        } else {
            res_desc.res.pitch2D.width  = in->width;
            res_desc.res.pitch2D.height = in->height;
        }

        ret = CHECK_CU(cu->cuTexObjectCreate(&tex[i], &res_desc, &tex_desc, NULL));
        if (ret < 0)
            goto fail;
    }

    ret = transpose_cuda_call_kernel(ctx, s->cu_func, tex, out,
                                     out->width, out->height,
                                     out->width, out->height,
                                     out->linesize[0],
                                     in->width, in->height, s->dir);
    if (ret < 0)
        goto fail;

    if (s->in_planes > 1) {
        ret = transpose_cuda_call_kernel(ctx, s->cu_func_uv, tex, out,
                                         AV_CEIL_RSHIFT(out->width, s->in_desc->log2_chroma_w),
                                         AV_CEIL_RSHIFT(out->height, s->in_desc->log2_chroma_h),
                                         out->width, out->height,
                                         out->linesize[1],
                                         AV_CEIL_RSHIFT(in->width, s->in_desc->log2_chroma_w),
                                         AV_CEIL_RSHIFT(in->height, s->in_desc->log2_chroma_h),
                                         s->dir);
        if (ret < 0)
            goto fail;
    }

    switch (s->dir) {
    case TRANSPOSE_CCLOCK_FLIP:
    case TRANSPOSE_CLOCK:
    case TRANSPOSE_CCLOCK:
    case TRANSPOSE_CLOCK_FLIP:
        if (in->sample_aspect_ratio.num == 0) {
            out->sample_aspect_ratio = in->sample_aspect_ratio;
        } else {
            out->sample_aspect_ratio.num = in->sample_aspect_ratio.den;
            out->sample_aspect_ratio.den = in->sample_aspect_ratio.num;
        }
        break;
    default:
        out->sample_aspect_ratio = in->sample_aspect_ratio;
        break;
    }

    for (i = 0; i < FF_ARRAY_ELEMS(tex); i++)
        if (tex[i])
            CHECK_CU(cu->cuTexObjectDestroy(tex[i]));

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);

fail:
    for (i = 0; i < FF_ARRAY_ELEMS(tex); i++)
        if (tex[i])
            CHECK_CU(cu->cuTexObjectDestroy(tex[i]));

    av_frame_free(&in);
    av_frame_free(&out);
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ret;
}

static av_cold int transpose_cuda_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx    = outlink->src;
    TransposeCUDAContext *s = ctx->priv;
    AVFilterLink *inlink    = ctx->inputs[0];
    int out_w, out_h;
    int ret;

    if ((inlink->w >= inlink->h && s->passthrough_mode == TRANSPOSE_PT_TYPE_LANDSCAPE) ||
        (inlink->w <= inlink->h && s->passthrough_mode == TRANSPOSE_PT_TYPE_PORTRAIT)) {
        FilterLink *inl  = ff_filter_link(inlink);
        FilterLink *outl = ff_filter_link(outlink);
        outlink->w = inlink->w;
        outlink->h = inlink->h;
        if (inl->hw_frames_ctx)
            outl->hw_frames_ctx = av_buffer_ref(inl->hw_frames_ctx);
        av_log(ctx, AV_LOG_VERBOSE,
               "w:%d h:%d -> w:%d h:%d (passthrough mode)\n",
               inlink->w, inlink->h, inlink->w, inlink->h);
        return 0;
    }
    s->passthrough_mode = TRANSPOSE_PT_TYPE_NONE;

    switch (s->dir) {
    case TRANSPOSE_CCLOCK_FLIP:
    case TRANSPOSE_CCLOCK:
    case TRANSPOSE_CLOCK:
    case TRANSPOSE_CLOCK_FLIP:
        out_w = inlink->h;
        out_h = inlink->w;
        break;
    default:
        out_w = inlink->w;
        out_h = inlink->h;
        break;
    }

    outlink->w = out_w;
    outlink->h = out_h;

    ret = init_processing_chain(ctx, out_w, out_h);
    if (ret < 0)
        return ret;

    ret = transpose_cuda_load_functions(ctx);
    if (ret < 0)
        return ret;

    av_log(ctx, AV_LOG_VERBOSE,
           "w:%d h:%d dir:%d -> w:%d h:%d rotation:%s vflip:%d\n",
           inlink->w, inlink->h, s->dir, out_w, out_h,
           s->dir == 1 || s->dir == 3 ? "clockwise" : "counterclockwise",
           s->dir == 0 || s->dir == 3);

    return 0;
}

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    TransposeCUDAContext *s = inlink->dst->priv;

    return s->passthrough_mode ?
        ff_null_get_video_buffer(inlink, w, h) :
        ff_default_get_video_buffer(inlink, w, h);
}

#define OFFSET(x) offsetof(TransposeCUDAContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM)
static const AVOption transpose_cuda_options[] = {
    { "dir", "set transpose direction", OFFSET(dir), AV_OPT_TYPE_INT, { .i64 = TRANSPOSE_CCLOCK_FLIP }, 0, 6, FLAGS, .unit = "dir" },
        { "cclock_flip",   "rotate counter-clockwise with vertical flip", 0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK_FLIP }, .flags=FLAGS, .unit = "dir" },
        { "clock",         "rotate clockwise",                            0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK       }, .flags=FLAGS, .unit = "dir" },
        { "cclock",        "rotate counter-clockwise",                    0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CCLOCK      }, .flags=FLAGS, .unit = "dir" },
        { "clock_flip",    "rotate clockwise with vertical flip",         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_CLOCK_FLIP  }, .flags=FLAGS, .unit = "dir" },
        { "reversal",      "rotate by half-turn",                         0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_REVERSAL    }, .flags=FLAGS, .unit = "dir" },
        { "hflip",         "flip horizontally",                           0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_HFLIP       }, .flags=FLAGS, .unit = "dir" },
        { "vflip",         "flip vertically",                             0, AV_OPT_TYPE_CONST, { .i64 = TRANSPOSE_VFLIP       }, .flags=FLAGS, .unit = "dir" },

    { "passthrough", "do not apply transposition if the input matches the specified geometry",
      OFFSET(passthrough_mode), AV_OPT_TYPE_INT, {.i64=TRANSPOSE_PT_TYPE_NONE},  0, INT_MAX, FLAGS, .unit = "passthrough" },
        { "none",      "always apply transposition",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_NONE},      INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },
        { "portrait",  "preserve portrait geometry",   0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_PORTRAIT},  INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },
        { "landscape", "preserve landscape geometry",  0, AV_OPT_TYPE_CONST, {.i64=TRANSPOSE_PT_TYPE_LANDSCAPE}, INT_MIN, INT_MAX, FLAGS, .unit = "passthrough" },

    { NULL }
};

AVFILTER_DEFINE_CLASS(transpose_cuda);

static const AVFilterPad transpose_cuda_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = transpose_cuda_filter_frame,
        .get_buffer.video = get_video_buffer,
    },
};

static const AVFilterPad transpose_cuda_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = transpose_cuda_config_output,
    },
};

const FFFilter ff_vf_transpose_cuda = {
    .p.name         = "transpose_cuda",
    .p.description  = NULL_IF_CONFIG_SMALL("CUDA accelerated video transpose"),
    .p.priv_class   = &transpose_cuda_class,
    .priv_size      = sizeof(TransposeCUDAContext),
    .uninit         = transpose_cuda_uninit,
    FILTER_INPUTS(transpose_cuda_inputs),
    FILTER_OUTPUTS(transpose_cuda_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
