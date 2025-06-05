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

/**
 * @file
 * Hardware accelerated transpose filter based on CUDA
 */

#include "libavutil/opt.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"

#include "filters.h"
#include "formats.h"
#include "video.h"
#include "transpose.h"
#include "cuda/cuda_vpp.h"

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_P016,
    AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_0RGB32,
    AV_PIX_FMT_0BGR32,
    AV_PIX_FMT_RGB32,
    AV_PIX_FMT_BGR32,
};

#define DIV_UP(a, b) ( ((a) + (b) - 1) / (b) )
#define BLOCKX 32
#define BLOCKY 16

typedef struct TransposeCUDAContext {
    CUDAVPPContext vpp_ctx; // must be the first field

    int passthrough;         // PassthroughType, landscape passthrough mode enabled
    int dir;                 // TransposeDir

    // CUDA functions for different operations
    CUfunction cu_func_transpose;
    CUfunction cu_func_transpose_uv;
} TransposeCUDAContext;

static int format_is_supported(enum AVPixelFormat fmt)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static av_cold int transpose_cuda_load_functions(AVFilterContext *avctx, enum AVPixelFormat format)
{
    TransposeCUDAContext *ctx = avctx->priv;
    CUDAVPPContext *vpp_ctx = &ctx->vpp_ctx;
    int ret;
    char buf[128];

    const char *fmt_name = av_get_pix_fmt_name(format);

    extern const unsigned char ff_vf_transpose_cuda_ptx_data[];
    extern const unsigned int ff_vf_transpose_cuda_ptx_len;

    ret = ff_cuda_vpp_load_module(avctx, vpp_ctx,
                                  ff_vf_transpose_cuda_ptx_data, ff_vf_transpose_cuda_ptx_len);
    if (ret < 0)
        return ret;

    // Load transpose functions
    snprintf(buf, sizeof(buf), "Transpose_%s", fmt_name);
    ret = ff_cuda_vpp_get_function(avctx, vpp_ctx, &ctx->cu_func_transpose, buf);
    if (ret < 0) {
        av_log(avctx, AV_LOG_FATAL, "Unsupported format for transpose: %s\n", fmt_name);
        return AVERROR(ENOSYS);
    }

    snprintf(buf, sizeof(buf), "Transpose_%s_uv", fmt_name);
    ret = ff_cuda_vpp_get_function(avctx, vpp_ctx, &ctx->cu_func_transpose_uv, buf);
    if (ret < 0 && vpp_ctx->in_planes > 1) {
        av_log(avctx, AV_LOG_WARNING, "UV function not found for format: %s\n", fmt_name);
    }

    return 0;
}

static int transpose_cuda_build_filter_params(AVFilterContext *avctx)
{
    TransposeCUDAContext *ctx = avctx->priv;
    CUDAVPPContext *vpp_ctx = &ctx->vpp_ctx;

    if (!format_is_supported(vpp_ctx->in_fmt)) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported input format: %s\n",
               av_get_pix_fmt_name(vpp_ctx->in_fmt));
        return AVERROR(ENOSYS);
    }

    return 0;
}

static av_cold int transpose_cuda_kernel(AVFilterContext *avctx, CUfunction func,
                                        CUtexObject src_tex[4],
                                        AVFrame *out_frame,
                                        int width, int height,
                                        int dst_width, int dst_height, int dst_pitch,
                                        int src_width, int src_height, int dir)
{
    TransposeCUDAContext *ctx = avctx->priv;
    CUDAVPPContext *s = &ctx->vpp_ctx;
    CudaFunctions *cu = s->cuda_dl;

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

static int transpose_cuda_filter_frame(AVFilterLink *inlink, AVFrame *input_frame)
{
    AVFilterContext *avctx     = inlink->dst;
    AVFilterLink *outlink      = avctx->outputs[0];
    TransposeCUDAContext *ctx  = avctx->priv;
    CUDAVPPContext *s          = &ctx->vpp_ctx;
    CudaFunctions *cu          = s->cuda_dl;
    AVFrame *output_frame      = NULL;
    CUtexObject tex[4] = { 0, 0, 0, 0 };
    int ret = 0;
    int i;
    CUcontext dummy;

    if (ctx->passthrough)
        return ff_filter_frame(outlink, input_frame);

    av_log(avctx, AV_LOG_DEBUG, "Filter input: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(input_frame->format),
           input_frame->width, input_frame->height, input_frame->pts);

    // Push CUDA context
    ret = CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
    if (ret < 0)
        return ret;

    output_frame = ff_get_video_buffer(outlink, s->output_width,
                                       s->output_height);
    if (!output_frame) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = av_frame_copy_props(output_frame, input_frame);
    if (ret < 0)
        goto fail;

    // Create texture objects for input
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
            .res.pitch2D.pitchInBytes = input_frame->linesize[i],
            .res.pitch2D.devPtr = (CUdeviceptr)input_frame->data[i],
        };

        if (i == 1 || i == 2) {
            res_desc.res.pitch2D.width = AV_CEIL_RSHIFT(input_frame->width, s->in_desc->log2_chroma_w);
            res_desc.res.pitch2D.height = AV_CEIL_RSHIFT(input_frame->height, s->in_desc->log2_chroma_h);
        } else {
            res_desc.res.pitch2D.width = input_frame->width;
            res_desc.res.pitch2D.height = input_frame->height;
        }

        ret = CHECK_CU(cu->cuTexObjectCreate(&tex[i], &res_desc, &tex_desc, NULL));
        if (ret < 0)
            goto fail;
    }

    // Process luma plane
    ret = transpose_cuda_kernel(avctx, ctx->cu_func_transpose, tex, output_frame,
                               output_frame->width, output_frame->height,
                               output_frame->width, output_frame->height,
                               output_frame->linesize[0],
                               input_frame->width, input_frame->height, ctx->dir);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error during luma transpose: %d\n", ret);
        goto fail;
    }

    // Process chroma planes if present
    if (s->in_planes > 1) {
        ret = transpose_cuda_kernel(avctx, ctx->cu_func_transpose_uv, tex, output_frame,
                                   AV_CEIL_RSHIFT(output_frame->width, s->in_desc->log2_chroma_w),
                                   AV_CEIL_RSHIFT(output_frame->height, s->in_desc->log2_chroma_h),
                                   output_frame->width, output_frame->height,
                                   output_frame->linesize[1],
                                   AV_CEIL_RSHIFT(input_frame->width, s->in_desc->log2_chroma_w),
                                   AV_CEIL_RSHIFT(input_frame->height, s->in_desc->log2_chroma_h),
                                   ctx->dir);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error during chroma transpose: %d\n", ret);
            goto fail;
        }
    }

    // Handle sample aspect ratio
    if (input_frame->sample_aspect_ratio.num == 0) {
        output_frame->sample_aspect_ratio = input_frame->sample_aspect_ratio;
    } else {
        output_frame->sample_aspect_ratio.num = input_frame->sample_aspect_ratio.den;
        output_frame->sample_aspect_ratio.den = input_frame->sample_aspect_ratio.num;
    }

    av_frame_free(&input_frame);

    av_log(avctx, AV_LOG_DEBUG, "Filter output: %s, %ux%u (%"PRId64").\n",
           av_get_pix_fmt_name(output_frame->format),
           output_frame->width, output_frame->height, output_frame->pts);

    // Cleanup texture objects
    for (i = 0; i < FF_ARRAY_ELEMS(tex); i++)
        if (tex[i])
            CHECK_CU(cu->cuTexObjectDestroy(tex[i]));

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ff_filter_frame(outlink, output_frame);

fail:
    for (i = 0; i < FF_ARRAY_ELEMS(tex); i++)
        if (tex[i])
            CHECK_CU(cu->cuTexObjectDestroy(tex[i]));

    av_frame_free(&input_frame);
    av_frame_free(&output_frame);
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ret;
}

static void transpose_cuda_uninit(AVFilterContext *avctx)
{
    TransposeCUDAContext *ctx  = avctx->priv;
    CUDAVPPContext *s          = &ctx->vpp_ctx;

    if (s->cu_module) {
        CudaFunctions *cu = s->cuda_dl;
        CUcontext dummy;

        if (s->hwctx) {
            CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
            CHECK_CU(cu->cuModuleUnload(s->cu_module));
            s->cu_module = NULL;
            CHECK_CU(cu->cuCtxPopCurrent(&dummy));
        }
    }

    av_buffer_unref(&s->device_ref);
    s->hwctx = NULL;
}

static av_cold int transpose_cuda_init(AVFilterContext *avctx)
{
    TransposeCUDAContext *ctx = avctx->priv;
    CUDAVPPContext *vpp_ctx = &ctx->vpp_ctx;

    ff_cuda_vpp_ctx_init(avctx);
    vpp_ctx->load_functions = transpose_cuda_load_functions;
    vpp_ctx->build_filter_params = transpose_cuda_build_filter_params;
    vpp_ctx->pipeline_uninit = transpose_cuda_uninit;
    vpp_ctx->output_format = AV_PIX_FMT_NONE;

    return 0;
}

static int transpose_cuda_config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx     = outlink->src;
    TransposeCUDAContext *ctx  = avctx->priv;
    CUDAVPPContext *vpp_ctx    = &ctx->vpp_ctx;
    AVFilterLink *inlink       = avctx->inputs[0];

    if ((inlink->w >= inlink->h && ctx->passthrough == TRANSPOSE_PT_TYPE_LANDSCAPE) ||
        (inlink->w <= inlink->h && ctx->passthrough == TRANSPOSE_PT_TYPE_PORTRAIT)) {
        vpp_ctx->passthrough = 1;
        av_log(avctx, AV_LOG_VERBOSE,
               "w:%d h:%d -> w:%d h:%d (passthrough mode)\n",
               inlink->w, inlink->h, inlink->w, inlink->h);
        return ff_cuda_vpp_config_output(outlink);
    }
    ctx->passthrough = TRANSPOSE_PT_TYPE_NONE;

    // For transpose operations that swap dimensions
    switch (ctx->dir) {
    case TRANSPOSE_CCLOCK_FLIP:
    case TRANSPOSE_CCLOCK:
    case TRANSPOSE_CLOCK:
    case TRANSPOSE_CLOCK_FLIP:
        vpp_ctx->output_width  = avctx->inputs[0]->h;
        vpp_ctx->output_height = avctx->inputs[0]->w;
        av_log(avctx, AV_LOG_DEBUG, "swap width and height for clock/cclock rotation\n");
        break;
    default:
        vpp_ctx->output_width  = avctx->inputs[0]->w;
        vpp_ctx->output_height = avctx->inputs[0]->h;
        break;
    }

    av_log(avctx, AV_LOG_VERBOSE,
           "w:%d h:%d dir:%d -> w:%d h:%d rotation:%s vflip:%d\n",
           inlink->w, inlink->h, ctx->dir, vpp_ctx->output_width, vpp_ctx->output_height,
           ctx->dir == 1 || ctx->dir == 3 ? "clockwise" : "counterclockwise",
           ctx->dir == 0 || ctx->dir == 3);

    return ff_cuda_vpp_config_output(outlink);
}

static AVFrame *get_video_buffer(AVFilterLink *inlink, int w, int h)
{
    TransposeCUDAContext *ctx = inlink->dst->priv;

    return ctx->passthrough ?
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
      OFFSET(passthrough), AV_OPT_TYPE_INT, {.i64=TRANSPOSE_PT_TYPE_NONE},  0, INT_MAX, FLAGS, .unit = "passthrough" },
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
        .config_props = ff_cuda_vpp_config_input,
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
    .init           = transpose_cuda_init,
    .uninit         = ff_cuda_vpp_ctx_uninit,
    FILTER_INPUTS(transpose_cuda_inputs),
    FILTER_OUTPUTS(transpose_cuda_outputs),
    FILTER_QUERY_FUNC2(ff_cuda_vpp_query_formats),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
