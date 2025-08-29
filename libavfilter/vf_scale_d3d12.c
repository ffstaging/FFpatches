/*
 * Copyright (c) 2025 Jianfeng.Zheng <jianfeng.zheng@mthreads.com>
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

#include "libavutil/opt.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/scale_eval.h"
#include "libavutil/pixdesc.h"
#include "video.h"
#include "compat/w32dlfcn.h"
#include "libavcodec/mf_utils.h"
#include "libavutil/hwcontext_d3d12va_internal.h"
#include "libavutil/hwcontext_d3d12va.h"

#define DXHR_CHECK(hr, fmt, ...)    \
    if (FAILED(hr)) {               \
        av_log(avctx, AV_LOG_ERROR, "[WinErr:0x%lX] ", hr);     \
        av_log(avctx, AV_LOG_ERROR, fmt, ##__VA_ARGS__);        \
        goto fail;                  \
    }

#define COND_CHECK(cond, fmt, ...)  \
    if (cond) {                     \
        av_log(avctx, AV_LOG_ERROR, fmt, ##__VA_ARGS__);        \
        goto fail;                  \
    }

#define DX_RELEASE(p, rls)          \
    if (p) {                        \
        rls(p);                     \
        p = NULL;                   \
    }

typedef struct ScaleD3d12Context {
    const AVClass                   *classCtx;

    /**
     * input device reference
     */
    AVBufferRef                     *av_device_ref;
    AVD3D12VADeviceContext          *input_hwctx;
    ID3D12Device                    *d3d_device_ref;

    /**
     * filter's device context
     */
    ID3D12VideoDevice               *video_dev;
    ID3D12VideoProcessor            *vp;
    ID3D12CommandQueue              *vp_command_queue;
    ID3D12CommandAllocator          *vp_command_allocator;
    ID3D12VideoProcessCommandList   *vp_command_list;
    AVD3D12VASyncContext            vp_sync;
    int                             gpu_mask;
    

    char                            *w_expr;
    char                            *h_expr;
    int                             force_original_aspect_ratio;
    int                             force_divisible_by;
} ScaleD3d12Context;


static int scale_d3d12_init(AVFilterContext* avctx)
{
    ScaleD3d12Context *ctx = avctx->priv;
    ctx->gpu_mask = 1;
    return 0;
}

static HRESULT mtvsr_fence_completion(AVD3D12VASyncContext *psync_ctx)
{
    uint64_t completion = ID3D12Fence_GetCompletedValue(psync_ctx->fence);
    if (completion < psync_ctx->fence_value) {
        HRESULT hr = ID3D12Fence_SetEventOnCompletion(psync_ctx->fence, psync_ctx->fence_value, psync_ctx->event);
        if (FAILED(hr))
            return hr;

        WaitForSingleObjectEx(psync_ctx->event, INFINITE, FALSE);
    }

    return 0;
}

static HRESULT wait_queue_idle(AVD3D12VASyncContext *psync_ctx, ID3D12CommandQueue *cmd_queue)
{
    ID3D12CommandQueue_Signal(cmd_queue, psync_ctx->fence, ++psync_ctx->fence_value);
    return mtvsr_fence_completion(psync_ctx);
}

static DXGI_COLOR_SPACE_TYPE get_dxgi_color_space(DXGI_FORMAT dxgi_fmt)
{
    DXGI_COLOR_SPACE_TYPE cspace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    switch (dxgi_fmt)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        cspace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        break;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        cspace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        break;
    case DXGI_FORMAT_NV12:
        cspace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        break;
    case DXGI_FORMAT_P010:
        cspace = DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020;
        break;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        cspace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        break;
    }

    return cspace;
}

static HRESULT create_video_processor(AVFilterContext *avctx, ID3D12VideoProcessor **pp_vp,
                                      DXGI_FORMAT ifmt, DXGI_FORMAT ofmt)
{
    ScaleD3d12Context *ctx = avctx->priv;
    AVFilterLink   *inlink = avctx->inputs[0];
    AVFilterLink  *outlink = avctx->outputs[0];
    HRESULT             hr = 0;

    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC outdesc= {
        .Format     = ofmt,
        .ColorSpace = get_dxgi_color_space(ofmt),
        .FrameRate  = {60, 1},
    };

    D3D12_VIDEO_SIZE_RANGE size_range = {
        .MinWidth   = 64,
        .MinHeight  = 64,
        .MaxWidth   = 3840,
        .MaxHeight  = 3840,
    };
    D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC indesc = {
        .Format     = ifmt,
        .ColorSpace = get_dxgi_color_space(ifmt),
        .FrameRate  = {60, 1},
        .SourceAspectRatio      = {1, 1},
        .DestinationAspectRatio = {1, 1},
        .SourceSizeRange        = size_range,
        .DestinationSizeRange   = size_range,
    };

    hr = ID3D12VideoDevice_CreateVideoProcessor(ctx->video_dev, ctx->gpu_mask, 
                                                &outdesc, 1, &indesc, 
                                                &IID_ID3D12VideoProcessor, pp_vp);
    DXHR_CHECK(hr, "Failed to create D3D12 VP (fmt %d->%d).\n", (int)ifmt, (int)ofmt);

fail:
    return hr;
}

static int filter_frame(AVFilterLink* inlink, AVFrame* in) 
{
    AVFilterContext *avctx = inlink->dst;
    ScaleD3d12Context *ctx = avctx->priv;
    AVFilterLink  *outlink = avctx->outputs[0];

    AVFrame           *out = NULL;
    HRESULT             hr = 0;
    int                ret = 0;

    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS out_stream = {};
    D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS   in_stream = {};

    if (in->format != AV_PIX_FMT_D3D12) {
        av_log(avctx, AV_LOG_ERROR, "Not D3D12 hardware inputs.\n");
        return AVERROR(EINVAL);
    }

    // Allocate output frame， see d3d12va_get_buffer()
    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate output frame.\n");
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    hr = ID3D12CommandAllocator_Reset(ctx->vp_command_allocator);
    DXHR_CHECK(hr, "Failed to reset command allocator.\n");
    hr = ID3D12VideoProcessCommandList_Reset(ctx->vp_command_list, ctx->vp_command_allocator);
    DXHR_CHECK(hr, "Failed to reset vp command list.\n");

    out_stream.OutputStream[0].pTexture2D   = ((AVD3D12VAFrame *)out->data[0])->texture;
    out_stream.OutputStream[0].Subresource  = 0;
    SetRect(&out_stream.TargetRectangle, 0, 0, outlink->w, outlink->h);

    in_stream.InputStream[0].pTexture2D     = ((AVD3D12VAFrame *)in->data[0])->texture;
    in_stream.InputStream[0].Subresource    = (int)(intptr_t)in->data[1];
    SetRect(&in_stream.Transform.SourceRectangle, 0, 0, inlink->w, inlink->h);
    SetRect(&in_stream.Transform.DestinationRectangle, 0, 0, outlink->w, outlink->h);

    ID3D12VideoProcessCommandList_ProcessFrames(ctx->vp_command_list, ctx->vp, &out_stream, 1, &in_stream);

    hr = ID3D12VideoProcessCommandList_Close(ctx->vp_command_list);
    DXHR_CHECK(hr, "Failed to close vp command list.\n");
    ID3D12CommandQueue_ExecuteCommandLists(ctx->vp_command_queue, 1, &ctx->vp_command_list);
    hr = wait_queue_idle(&ctx->vp_sync, ctx->vp_command_queue);
    DXHR_CHECK(hr, "Failed to sync VP command queue.\n");

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return AVERROR_EXTERNAL;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *avctx = inlink->dst;
    ScaleD3d12Context *ctx = avctx->priv;
    AVHWFramesContext *frames_ctx;
    AVHWDeviceContext *device_ctx;
    FilterLink        *inl = ff_filter_link(inlink);

    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "Not HW frame inputs for vf_scale_d3d12.\n");
        return AVERROR(EINVAL);
    }

    frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
    device_ctx = (AVHWDeviceContext *)frames_ctx->device_ref->data;

    if (device_ctx->type != AV_HWDEVICE_TYPE_D3D12VA) {
        av_log(ctx, AV_LOG_ERROR, "Not D3D12VA inputs for vf_scale_d3d12.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int config_output(AVFilterLink* outlink) 
{
    AVFilterContext *avctx = outlink->src;
    ScaleD3d12Context *ctx = avctx->priv;
    AVFilterLink   *inlink = avctx->inputs[0];
    FilterLink        *inl = ff_filter_link(inlink);
    FilterLink       *outl = ff_filter_link(outlink);
    AVHWFramesContext       *in_frames_ctx;
    AVHWDeviceContext       *in_device_ctx;
    AVHWFramesContext       *out_frames_ctx;
    DXGI_FORMAT             dxgi_format;
    HRESULT                  hr;
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    double           w_adj = 1.0;

    // Evaluate output dimensions
    int w, h;
    int ret = ff_scale_eval_dimensions(ctx, ctx->w_expr, ctx->h_expr, inlink, outlink, &w, &h);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to evaluate dimensions.\n");
        return AVERROR(EINVAL);
    }

    ff_scale_adjust_dimensions(inlink, &w, &h, ctx->force_original_aspect_ratio, ctx->force_divisible_by, w_adj);

    outlink->w = w;
    outlink->h = h;

    // Reference to input device 
    in_frames_ctx       = (AVHWFramesContext *)inl->hw_frames_ctx->data;
    ctx->av_device_ref  = av_buffer_ref(in_frames_ctx->device_ref);
    in_device_ctx       = (AVHWDeviceContext *)ctx->av_device_ref->data;
    ctx->input_hwctx    = (AVD3D12VADeviceContext *)in_device_ctx->hwctx;
    if (!ctx->input_hwctx->device) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialize filter device or context in config_props.\n");
        return AVERROR(EINVAL);
    }
    ID3D12Device_AddRef(ctx->input_hwctx->device);
    ctx->d3d_device_ref = ctx->input_hwctx->device;

    // Create output hw frame context
    outl->hw_frames_ctx = av_hwframe_ctx_alloc(ctx->av_device_ref);
    if (!outl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "Failed to alloc HW frame context for output.\n");
        return AVERROR(ENOMEM);
    }
    out_frames_ctx = (AVHWFramesContext*)outl->hw_frames_ctx->data;
    out_frames_ctx->format    = in_frames_ctx->format;
    out_frames_ctx->sw_format = in_frames_ctx->sw_format;
    out_frames_ctx->width     = w;
    out_frames_ctx->height    = h;
    out_frames_ctx->initial_pool_size = 0;  // Dynamic allocation
    ff_filter_init_hw_frames(avctx, outlink, 10);
    ret = av_hwframe_ctx_init(outl->hw_frames_ctx);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to int HW frame context for output.\n");
        return AVERROR(ENOMEM);
    }

    av_log(avctx, AV_LOG_VERBOSE, "format=%s, %dx%d -> %dx%d.\n",
            av_get_pix_fmt_name(in_frames_ctx->sw_format),
            inlink->w, inlink->h, outlink->w, outlink->h);

    hr = ID3D12Device_QueryInterface(ctx->d3d_device_ref, &IID_ID3D12VideoDevice, (void **)&ctx->video_dev);
    DXHR_CHECK(hr,  "Failed to create D3D12VideoDevice.\n")

    queue_desc = (D3D12_COMMAND_QUEUE_DESC){
        .Type = D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS,
        .NodeMask = ctx->gpu_mask,
    };
    hr = ID3D12Device_CreateCommandQueue(ctx->d3d_device_ref, &queue_desc,
                                        &IID_ID3D12CommandQueue, &ctx->vp_command_queue);
    DXHR_CHECK(hr, "Frailed to create command queue.\n");

    hr = ID3D12Device_CreateFence(ctx->d3d_device_ref, (ctx->vp_sync.fence_value = 0), 
                                D3D12_FENCE_FLAG_NONE, 
                                &IID_ID3D12Fence, &ctx->vp_sync.fence);
    DXHR_CHECK(hr, "Failed to create vp fence.\n");
    ctx->vp_sync.event = CreateEvent(NULL, FALSE, FALSE, NULL);
    COND_CHECK(!ctx->vp_sync.event, "Failed to create vp sync event.\n");

    hr = ID3D12Device_CreateCommandAllocator(ctx->d3d_device_ref, D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS,
                                            &IID_ID3D12CommandAllocator, &ctx->vp_command_allocator);
    DXHR_CHECK(hr, "Frailed to create command allocator.\n");

    hr = ID3D12Device_CreateCommandList(ctx->d3d_device_ref, ctx->gpu_mask, D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS,
                                        ctx->vp_command_allocator, NULL,
                                        &IID_ID3D12VideoProcessCommandList, &ctx->vp_command_list);
    DXHR_CHECK(hr, "Frailed to create VP command list.\n");

    hr = ID3D12VideoProcessCommandList_Close(ctx->vp_command_list);
    DXHR_CHECK(hr, "Frailed to close VP command list.\n");

    ID3D12CommandQueue_ExecuteCommandLists(ctx->vp_command_queue, 1, &ctx->vp_command_list);
    hr = wait_queue_idle(&ctx->vp_sync, ctx->vp_command_queue);
    DXHR_CHECK(hr, "Failed to sync VP command queue.\n");

    dxgi_format = ((AVD3D12VAFramesContext*)(in_frames_ctx->hwctx))->format;
    if (create_video_processor(avctx, &ctx->vp, dxgi_format, dxgi_format) < 0) {
        goto fail;
    }

    return 0;

fail:
    return AVERROR_EXTERNAL;
}

static void scale_d3d12_uninit(AVFilterContext* avctx)
{
    ScaleD3d12Context *ctx = avctx->priv;

    DX_RELEASE(ctx->vp,                 ID3D12VideoProcessor_Release);
    DX_RELEASE(ctx->vp_command_list,    ID3D12VideoProcessCommandList_Release);
    DX_RELEASE(ctx->vp_command_allocator,   ID3D12CommandAllocator_Release);
    DX_RELEASE(ctx->vp_command_queue,   ID3D12CommandQueue_Release);
    DX_RELEASE(ctx->vp_sync.fence,      ID3D12Fence_Release);
    DX_RELEASE(ctx->vp_sync.event,      CloseHandle);
    DX_RELEASE(ctx->video_dev,          ID3D12VideoDevice_Release);
    DX_RELEASE(ctx->d3d_device_ref,     ID3D12Device_Release);

    av_buffer_unref(&ctx->av_device_ref);
}

static const AVFilterPad scale_d3d12_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &filter_frame,
        .config_props = &config_input,
    },
};

static const AVFilterPad scale_d3d12_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &config_output,
    },
};

#define OFFSET(x) offsetof(ScaleD3d12Context, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption scale_d3d12_options[] = {
    { "w", "Output video output_width",
            OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, .flags = FLAGS },
    { "h", "Output video output_height",
            OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, .flags = FLAGS },
    { "force_original_aspect_ratio", "decrease or increase w/h if necessary to keep the original AR",
            OFFSET(force_original_aspect_ratio), AV_OPT_TYPE_INT, { .i64 = 0}, 0, 2, FLAGS, .unit = "force_oar" },
    { "disable",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "decrease", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "increase", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 2 }, 0, 0, FLAGS, .unit = "force_oar" },
    { "force_divisible_by", "enforce that the output resolution is divisible by a defined integer when force_original_aspect_ratio is used",
            OFFSET(force_divisible_by), AV_OPT_TYPE_INT, { .i64 = 1}, 1, 256, FLAGS },
    { NULL }
};


AVFILTER_DEFINE_CLASS(scale_d3d12);

const FFFilter ff_vf_scale_d3d12 = {
    .p.name         = "scale_d3d12",
    .p.description  = NULL_IF_CONFIG_SMALL("Scale video using D3D12 VPP"),
    .priv_size      = sizeof(ScaleD3d12Context),
    .p.priv_class   = &scale_d3d12_class,
    .init           = scale_d3d12_init,
    .uninit         = scale_d3d12_uninit,
    FILTER_INPUTS(scale_d3d12_inputs),
    FILTER_OUTPUTS(scale_d3d12_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_D3D12),
    .p.flags        = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
