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
 * Hardware accelerated hstack, vstack and xstack filters based on CUDA
 */

#include "config_components.h"

#include "libavutil/opt.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/colorspace.h"
#include "libavutil/mem.h"

#include "filters.h"
#include "formats.h"
#include "video.h"

#include "framesync.h"
#include "cuda/load_helper.h"

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

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

typedef struct CUDAStackContext {
    AVCUDADeviceContext *hwctx;
    CudaFunctions *cuda_dl;

    CUcontext   cu_ctx;
    CUmodule    cu_module;
    CUstream    cu_stream;

    CUfunction  cu_func_copy;
    CUfunction  cu_func_copy_uv;

    CUfunction  cu_func_color;
    CUfunction  cu_func_color_uv;

    enum AVPixelFormat in_fmt;
    const AVPixFmtDescriptor *in_desc;
    int in_planes;
    int in_plane_depths[4];
    int in_plane_channels[4];

    int fillcolor_yuv[4];
} CUDAStackContext;

#define HSTACK_NAME             "hstack_cuda"
#define VSTACK_NAME             "vstack_cuda"
#define XSTACK_NAME             "xstack_cuda"
#define HWContext               CUDAStackContext
#define StackHWContext          StackCudaContext
#include "stack_internal.h"

typedef struct StackCudaContext {
    StackBaseContext base;
    CUDAStackContext cuda;
} StackCudaContext;

static int format_is_supported(enum AVPixelFormat fmt)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static void rgb2yuv(float r, float g, float b, int *y, int *u, int *v, int depth)
{
    *y = ((0.21260*219.0/255.0) * r + (0.71520*219.0/255.0) * g +
         (0.07220*219.0/255.0) * b) * ((1 << depth) - 1);
    *u = (-(0.11457*224.0/255.0) * r - (0.38543*224.0/255.0) * g +
         (0.50000*224.0/255.0) * b + 0.5) * ((1 << depth) - 1);
    *v = ((0.50000*224.0/255.0) * r - (0.45415*224.0/255.0) * g -
         (0.04585*224.0/255.0) * b + 0.5) * ((1 << depth) - 1);
}

static int conv_8to16(int val, int mask)
{
    return (val | (val << 8)) & mask;
}

static void get_func_name(char *buf, size_t buf_size,
                          const char *prefix, int depth, int channels)
{
    const char *suffix;

    if (channels == 4 && depth <= 8)
        suffix = "uchar4";
    else if (channels == 2 && depth <= 8)
        suffix = "uchar2";
    else if (channels == 2 && depth > 8)
        suffix = "ushort2";
    else if (depth > 8)
        suffix = "ushort";
    else
        suffix = "uchar";

    snprintf(buf, buf_size, "%s_%s", prefix, suffix);
}

static av_cold int cuda_stack_load_functions(AVFilterContext *ctx)
{
    StackCudaContext *sctx = ctx->priv;
    CUDAStackContext *s = &sctx->cuda;
    CUcontext dummy, cuda_ctx = s->hwctx->cuda_ctx;
    CudaFunctions *cu = s->cuda_dl;
    int ret;
    char buf[128];

    extern const unsigned char ff_vf_stack_cuda_ptx_data[];
    extern const unsigned int ff_vf_stack_cuda_ptx_len;

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, s->hwctx, &s->cu_module,
                              ff_vf_stack_cuda_ptx_data, ff_vf_stack_cuda_ptx_len);
    if (ret < 0)
        goto fail;

    get_func_name(buf, sizeof(buf), "StackCopy",
                  s->in_plane_depths[0], s->in_plane_channels[0]);
    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_copy, s->cu_module, buf));
    if (ret < 0) {
        av_log(ctx, AV_LOG_FATAL, "Failed to load copy function: %s\n", buf);
        ret = AVERROR(ENOSYS);
        goto fail;
    }

    get_func_name(buf, sizeof(buf), "SetColor",
                  s->in_plane_depths[0], s->in_plane_channels[0]);
    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_color, s->cu_module, buf));
    if (ret < 0) {
        av_log(ctx, AV_LOG_FATAL, "Failed to load color function: %s\n", buf);
        ret = AVERROR(ENOSYS);
        goto fail;
    }

    if (s->in_planes > 1) {
        if (s->in_plane_channels[1] > 1) {
            get_func_name(buf, sizeof(buf), "StackCopy",
                          s->in_plane_depths[1], s->in_plane_channels[1]);
        } else {
            get_func_name(buf, sizeof(buf), "StackCopy",
                          s->in_plane_depths[1], s->in_plane_channels[1]);
            av_strlcat(buf, "_uv", sizeof(buf));
        }
        ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_copy_uv, s->cu_module, buf));
        if (ret < 0) {
            av_log(ctx, AV_LOG_FATAL, "Failed to load copy UV function: %s\n", buf);
            ret = AVERROR(ENOSYS);
            goto fail;
        }

        if (s->in_plane_channels[1] > 1) {
            get_func_name(buf, sizeof(buf), "SetColor",
                          s->in_plane_depths[1], s->in_plane_channels[1]);
        } else {
            get_func_name(buf, sizeof(buf), "SetColor",
                          s->in_plane_depths[1], s->in_plane_channels[1]);
            av_strlcat(buf, "_uv", sizeof(buf));
        }
        ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func_color_uv, s->cu_module, buf));
        if (ret < 0) {
            av_log(ctx, AV_LOG_FATAL, "Failed to load color UV function: %s\n", buf);
            ret = AVERROR(ENOSYS);
            goto fail;
        }
    }

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ret;
}

static int cuda_stack_color_kernel(AVFilterContext *ctx, CUfunction func,
                            AVFrame *out_frame, const int *color,
                            int width, int height,
                            int dst_x, int dst_y,
                            int dst_width, int dst_height, int dst_pitch)
{
    StackCudaContext *sctx = ctx->priv;
    CUDAStackContext *s = &sctx->cuda;
    CudaFunctions *cu = s->cuda_dl;

    CUdeviceptr dst_devptr[4] = {
        (CUdeviceptr)out_frame->data[0], (CUdeviceptr)out_frame->data[1],
        (CUdeviceptr)out_frame->data[2], (CUdeviceptr)out_frame->data[3]
    };

    void *args[] = {
        &dst_devptr[0], &dst_devptr[1], &dst_devptr[2], &dst_devptr[3],
        &width, &height, &dst_pitch,
        &dst_x, &dst_y,
        (void *)&color[0], (void *)&color[1], (void *)&color[2], (void *)&color[3],
        &dst_width, &dst_height,
    };

    return CHECK_CU(cu->cuLaunchKernel(func,
                                     DIV_UP(width, BLOCKX), DIV_UP(height, BLOCKY), 1,
                                     BLOCKX, BLOCKY, 1,
                                     0, s->cu_stream, args, NULL));
}

static int cuda_stack_copy_kernel(AVFilterContext *ctx, CUfunction func,
                            CUtexObject src_tex[4],
                            AVFrame *out_frame,
                            int width, int height,
                            int dst_x, int dst_y, int dst_pitch,
                            int src_width, int src_height)
{
    StackCudaContext *sctx = ctx->priv;
    CUDAStackContext *s = &sctx->cuda;
    CudaFunctions *cu = s->cuda_dl;

    CUdeviceptr dst_devptr[4] = {
        (CUdeviceptr)out_frame->data[0], (CUdeviceptr)out_frame->data[1],
        (CUdeviceptr)out_frame->data[2], (CUdeviceptr)out_frame->data[3]
    };

    void *args[] = {
        &src_tex[0], &src_tex[1], &src_tex[2], &src_tex[3],
        &dst_devptr[0], &dst_devptr[1], &dst_devptr[2], &dst_devptr[3],
        &width, &height, &dst_pitch,
        &dst_x, &dst_y,
        &src_width, &src_height,
        &out_frame->width, &out_frame->height
    };

    return CHECK_CU(cu->cuLaunchKernel(func,
                                     DIV_UP(width, BLOCKX), DIV_UP(height, BLOCKY), 1,
                                     BLOCKX, BLOCKY, 1,
                                     0, s->cu_stream, args, NULL));
}

static int cuda_stack_color_op(AVFilterContext *ctx, StackItemRegion *region,
                               AVFrame *out, const int *color)
{
    StackCudaContext *sctx = ctx->priv;
    CUDAStackContext *s = &sctx->cuda;
    CudaFunctions *cu = s->cuda_dl;
    int ret = 0;
    CUcontext dummy;

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
    if (ret < 0)
        return ret;

    ret = cuda_stack_color_kernel(ctx, s->cu_func_color,
                                out, color, region->width, region->height,
                                region->x, region->y,
                                out->width, out->height,
                                out->linesize[0]);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error during color operation: %d\n", ret);
        goto fail;
    }

    if (s->in_planes > 1) {
        ret = cuda_stack_color_kernel(ctx, s->cu_func_color_uv,
                                    out, color,
                                    AV_CEIL_RSHIFT(region->width, s->in_desc->log2_chroma_w),
                                    AV_CEIL_RSHIFT(region->height, s->in_desc->log2_chroma_h),
                                    AV_CEIL_RSHIFT(region->x, s->in_desc->log2_chroma_w),
                                    AV_CEIL_RSHIFT(region->y, s->in_desc->log2_chroma_h),
                                    out->width, out->height,
                                    out->linesize[1]);
        if (ret < 0)
            av_log(ctx, AV_LOG_ERROR, "Error during color UV operation: %d\n", ret);
    }

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ret;
}

static int cuda_stack_copy_op(AVFilterContext *ctx, StackItemRegion *region,
                              AVFrame *in, AVFrame *out)
{
    StackCudaContext *sctx = ctx->priv;
    CUDAStackContext *s = &sctx->cuda;
    CudaFunctions *cu = s->cuda_dl;
    CUtexObject tex[4] = { 0, 0, 0, 0 };
    int ret = 0;
    int i;
    CUcontext dummy;

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
    if (ret < 0)
        return ret;

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
            res_desc.res.pitch2D.width = AV_CEIL_RSHIFT(in->width, s->in_desc->log2_chroma_w);
            res_desc.res.pitch2D.height = AV_CEIL_RSHIFT(in->height, s->in_desc->log2_chroma_h);
        } else {
            res_desc.res.pitch2D.width = in->width;
            res_desc.res.pitch2D.height = in->height;
        }

        ret = CHECK_CU(cu->cuTexObjectCreate(&tex[i], &res_desc, &tex_desc, NULL));
        if (ret < 0)
            goto fail;
    }

    ret = cuda_stack_copy_kernel(ctx, s->cu_func_copy,
                             tex, out, region->width, region->height,
                                region->x, region->y, out->linesize[0],
                                in->width, in->height);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error during copy operation: %d\n", ret);
        goto fail;
    }

    if (s->in_planes > 1) {
        ret = cuda_stack_copy_kernel(ctx, s->cu_func_copy_uv, tex, out,
                                    AV_CEIL_RSHIFT(region->width, s->in_desc->log2_chroma_w),
                                    AV_CEIL_RSHIFT(region->height, s->in_desc->log2_chroma_h),
                                    AV_CEIL_RSHIFT(region->x, s->in_desc->log2_chroma_w),
                                    AV_CEIL_RSHIFT(region->y, s->in_desc->log2_chroma_h),
                                    out->linesize[1],
                                    AV_CEIL_RSHIFT(in->width, s->in_desc->log2_chroma_w),
                                    AV_CEIL_RSHIFT(in->height, s->in_desc->log2_chroma_h));
        if (ret < 0)
            av_log(ctx, AV_LOG_ERROR, "Error during copy UV operation: %d\n", ret);
    }

fail:
    for (i = 0; i < FF_ARRAY_ELEMS(tex); i++)
        if (tex[i])
            CHECK_CU(cu->cuTexObjectDestroy(tex[i]));

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ret;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    StackCudaContext *sctx = fs->opaque;
    CUDAStackContext *s = &sctx->cuda;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out_frame = NULL;
    AVFrame *in_frame = NULL;
    int ret = 0;

    out_frame = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out_frame)
        return AVERROR(ENOMEM);

    if (sctx->base.fillcolor_enable) {
        StackItemRegion full_region = {
            .x = 0,
            .y = 0,
            .width = outlink->w,
            .height = outlink->h
        };

        ret = cuda_stack_color_op(ctx, &full_region, out_frame, s->fillcolor_yuv);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed to fill background color\n");
            goto fail;
        }
    }

    for (int i = 0; i < ctx->nb_inputs; i++) {
        ret = ff_framesync_get_frame(fs, i, &in_frame, 0);
        if (ret)
            goto fail;

        if (!i) {
            ret = av_frame_copy_props(out_frame, in_frame);
            if (ret < 0)
                goto fail;
        }

        ret = cuda_stack_copy_op(ctx, &sctx->base.regions[i], in_frame, out_frame);
        if (ret < 0)
            goto fail;
    }

    out_frame->pts = av_rescale_q(sctx->base.fs.pts, sctx->base.fs.time_base, outlink->time_base);
    out_frame->sample_aspect_ratio = outlink->sample_aspect_ratio;

    return ff_filter_frame(outlink, out_frame);

fail:
    av_frame_free(&out_frame);
    return ret;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    StackCudaContext *sctx = ctx->priv;
    CUDAStackContext *s = &sctx->cuda;
    AVFilterLink *inlink0 = ctx->inputs[0];
    FilterLink      *inl0 = ff_filter_link(inlink0);
    FilterLink      *outl = ff_filter_link(outlink);
    enum AVPixelFormat in_format;
    int ret;
    AVHWFramesContext *in_frames_ctx;
    AVBufferRef *hw_frames_ctx;
    AVHWFramesContext *out_frames_ctx;

    if (inlink0->format != AV_PIX_FMT_CUDA || !inl0->hw_frames_ctx || !inl0->hw_frames_ctx->data) {
        av_log(ctx, AV_LOG_ERROR, "Software pixel format is not supported.\n");
        return AVERROR(EINVAL);
    }

    in_frames_ctx = (AVHWFramesContext*)inl0->hw_frames_ctx->data;
    in_format = in_frames_ctx->sw_format;

    if (!format_is_supported(in_format)) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n",
               av_get_pix_fmt_name(in_format));
        return AVERROR(ENOSYS);
    }

    s->in_fmt = in_format;
    s->in_desc = av_pix_fmt_desc_get(s->in_fmt);
    s->in_planes = av_pix_fmt_count_planes(s->in_fmt);

    for (int i = 0; i < s->in_desc->nb_components; i++) {
        int d = (s->in_desc->comp[i].depth + 7) / 8;
        int p = s->in_desc->comp[i].plane;
        s->in_plane_channels[p] = FFMAX(s->in_plane_channels[p], s->in_desc->comp[i].step / d);
        s->in_plane_depths[p] = s->in_desc->comp[i].depth;
    }

    s->hwctx = in_frames_ctx->device_ctx->hwctx;
    s->cuda_dl = s->hwctx->internal->cuda_dl;
    s->cu_stream = s->hwctx->stream;

    for (int i = 1; i < sctx->base.nb_inputs; i++) {
        AVFilterLink *inlink = ctx->inputs[i];
        FilterLink      *inl = ff_filter_link(inlink);
        AVHWFramesContext *hwfc = NULL;

        if (inlink->format != AV_PIX_FMT_CUDA || !inl->hw_frames_ctx || !inl->hw_frames_ctx->data) {
            av_log(ctx, AV_LOG_ERROR, "Software pixel format is not supported.\n");
            return AVERROR(EINVAL);
        }

        hwfc = (AVHWFramesContext *)inl->hw_frames_ctx->data;

        if (in_frames_ctx->sw_format != hwfc->sw_format) {
            av_log(ctx, AV_LOG_ERROR, "All inputs should have the same underlying software pixel format.\n");
            return AVERROR(EINVAL);
        }
    }

    if (sctx->base.fillcolor_enable) {
        if (s->in_desc->flags & AV_PIX_FMT_FLAG_RGB) {
            s->fillcolor_yuv[0] = sctx->base.fillcolor[0];
            s->fillcolor_yuv[1] = sctx->base.fillcolor[1];
            s->fillcolor_yuv[2] = sctx->base.fillcolor[2];
            s->fillcolor_yuv[3] = sctx->base.fillcolor[3];
        } else {
            int Y, U, V;

            rgb2yuv(sctx->base.fillcolor[0] / 255.0, sctx->base.fillcolor[1] / 255.0,
                    sctx->base.fillcolor[2] / 255.0, &Y, &U, &V, 8);

            if (s->in_plane_depths[0] > 8) {
                int mask = (s->in_plane_depths[0] <= 10) ? 0xFFC0 : 0xFFFF;
                s->fillcolor_yuv[0] = conv_8to16(Y, mask);
                s->fillcolor_yuv[1] = conv_8to16(U, mask);
                s->fillcolor_yuv[2] = conv_8to16(V, mask);
            } else {
                s->fillcolor_yuv[0] = Y;
                s->fillcolor_yuv[1] = U;
                s->fillcolor_yuv[2] = V;
            }
            s->fillcolor_yuv[3] = sctx->base.fillcolor[3];
        }
    }

    ret = config_comm_output(outlink);
    if (ret < 0)
        return ret;

    ret = cuda_stack_load_functions(ctx);
    if (ret < 0)
        return ret;

    hw_frames_ctx = av_hwframe_ctx_alloc(in_frames_ctx->device_ref);
    if (!hw_frames_ctx)
        return AVERROR(ENOMEM);

    out_frames_ctx = (AVHWFramesContext*)hw_frames_ctx->data;
    out_frames_ctx->format = AV_PIX_FMT_CUDA;
    out_frames_ctx->sw_format = in_format;
    out_frames_ctx->width = outlink->w;
    out_frames_ctx->height = outlink->h;

    ret = av_hwframe_ctx_init(hw_frames_ctx);
    if (ret < 0) {
        av_buffer_unref(&hw_frames_ctx);
        return ret;
    }

    av_buffer_unref(&outl->hw_frames_ctx);
    outl->hw_frames_ctx = hw_frames_ctx;

    return 0;
}

static av_cold int cuda_stack_init(AVFilterContext *ctx)
{
    return stack_init(ctx);
}

static av_cold void cuda_stack_uninit(AVFilterContext *ctx)
{
    StackCudaContext *sctx = ctx->priv;
    CUDAStackContext *s = &sctx->cuda;

    if (s->hwctx && s->cu_module) {
        CudaFunctions *cu = s->cuda_dl;
        CUcontext dummy;

        CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
        CHECK_CU(cu->cuModuleUnload(s->cu_module));
        s->cu_module = NULL;
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    }

    stack_uninit(ctx);
}

static const enum AVPixelFormat cuda_stack_pix_fmts[] = {
    AV_PIX_FMT_CUDA,
    AV_PIX_FMT_NONE,
};

#include "stack_internal.c"

#if CONFIG_HSTACK_CUDA_FILTER

DEFINE_HSTACK_OPTIONS(cuda);
DEFINE_STACK_FILTER(hstack, cuda, "CUDA", 0);

#endif

#if CONFIG_VSTACK_CUDA_FILTER

DEFINE_VSTACK_OPTIONS(cuda);
DEFINE_STACK_FILTER(vstack, cuda, "CUDA", 0);

#endif

#if CONFIG_XSTACK_CUDA_FILTER

DEFINE_XSTACK_OPTIONS(cuda);
DEFINE_STACK_FILTER(xstack, cuda, "CUDA", 0);

#endif
