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
 * NPP video padding filter
 */

#include <float.h>

#include <nppi.h>

#include "filters.h"
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/cuda_check.h"
#include "libavutil/eval.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, device_hwctx->internal->cuda_dl, x)

static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_NV12,
};

typedef struct NPPPadContext {
    const AVClass *class;

    AVBufferRef *frames_ctx;

    int w, h;       ///< output dimensions, a value of 0 will result in the input size
    int x, y;       ///< offsets of the input area with respect to the padded area
    int in_w, in_h; ///< width and height for the padded input video

    char *w_expr;   ///< width expression
    char *h_expr;   ///< height expression
    char *x_expr;   ///< x offset expression
    char *y_expr;   ///< y offset expression

    uint8_t rgba_color[4];    ///< color for the padding area
    uint8_t parsed_color[4];  ///< parsed color for use in npp functions
    AVRational aspect;

    int eval_mode;

    int last_out_w, last_out_h; ///< used to evaluate the prior output width and height with the incoming frame
} NPPPadContext;

static const char *const var_names[] = {
    "in_w",  "iw",
    "in_h",  "ih",
    "out_w", "ow",
    "out_h", "oh",
    "x",
    "y",
    "a",
    "sar",
    "dar",
    "hsub",
    "vsub",
    NULL
};

enum {
    VAR_IN_W,
    VAR_IW,
    VAR_IN_H,
    VAR_IH,
    VAR_OUT_W,
    VAR_OW,
    VAR_OUT_H,
    VAR_OH,
    VAR_X,
    VAR_Y,
    VAR_A,
    VAR_SAR,
    VAR_DAR,
    VAR_HSUB,
    VAR_VSUB,
    VARS_NB
};

enum EvalMode {
    EVAL_MODE_INIT,
    EVAL_MODE_FRAME,
    EVAL_MODE_NB
};

static int eval_expr(AVFilterContext *ctx)
{
    NPPPadContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    double var_values[VARS_NB], res;
    char *expr;
    int ret;

    var_values[VAR_IN_W]   = var_values[VAR_IW]   = s->in_w;
    var_values[VAR_IN_H]   = var_values[VAR_IH]   = s->in_h;
    var_values[VAR_OUT_W]  = var_values[VAR_OW]  = NAN;
    var_values[VAR_OUT_H]  = var_values[VAR_OH]  = NAN;
    var_values[VAR_A]      = (double)s->in_w / s->in_h;
    var_values[VAR_SAR]    = inlink->sample_aspect_ratio.num ?
                           (double)inlink->sample_aspect_ratio.num /
                           inlink->sample_aspect_ratio.den : 1;
    var_values[VAR_DAR]    = var_values[VAR_A] * var_values[VAR_SAR];
    var_values[VAR_HSUB]   = 1 << desc->log2_chroma_w;
    var_values[VAR_VSUB]   = 1 << desc->log2_chroma_h;

    expr = s->w_expr;
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values, NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto fail;

    s->w = res;
    if (s->w < 0) {
        av_log(ctx, AV_LOG_ERROR, "Width expression is negative.\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    var_values[VAR_OUT_W] = var_values[VAR_OW] = s->w;

    expr = s->h_expr;
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values, NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto fail;

    s->h = res;
    if (s->h < 0) {
        av_log(ctx, AV_LOG_ERROR, "Height expression is negative.\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }
    var_values[VAR_OUT_H] = var_values[VAR_OH] = s->h;

    if (!s->h)
        s->h = s->in_h;

    var_values[VAR_OUT_H] = var_values[VAR_OH] = s->h;


    expr = s->w_expr;
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values, NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto fail;

    s->w = res;
    if (s->w < 0) {
        av_log(ctx, AV_LOG_ERROR, "Width expression is negative.\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }
    if (!s->w)
        s->w = s->in_w;

    var_values[VAR_OUT_W] = var_values[VAR_OW] = s->w;


    expr = s->x_expr;
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values, NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto fail;

    s->x = res;


    expr = s->y_expr;
    ret = av_expr_parse_and_eval(&res, expr, var_names, var_values, NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (ret < 0)
        goto fail;

    s->y = res;

    if (s->x < 0 || s->x + s->in_w > s->w) {
        s->x = (s->w - s->in_w) / 2;
        av_log(ctx, AV_LOG_VERBOSE, "centering X offset.\n");
    }

    if (s->y < 0 || s->y + s->in_h > s->h) {
        s->y = (s->h - s->in_h) / 2;
        av_log(ctx, AV_LOG_VERBOSE, "centering Y offset.\n");
    }

    s->w = av_clip(s->w, 1, INT_MAX);
    s->h = av_clip(s->h, 1, INT_MAX);

    if (s->w < s->in_w || s->h < s->in_h) {
        av_log(ctx, AV_LOG_ERROR, "Padded size < input size.\n");
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_DEBUG,
           "w:%d h:%d -> w:%d h:%d x:%d y:%d color:0x%02X%02X%02X%02X\n",
           inlink->w, inlink->h, s->w, s->h, s->x, s->y, s->rgba_color[0],
           s->rgba_color[1], s->rgba_color[2], s->rgba_color[3]);

    return 0;

fail:
    av_log(ctx, AV_LOG_ERROR, "Error evaluating '%s'\n", expr);
    return ret;
}

static int npppad_alloc_out_frames_ctx(AVFilterContext *ctx, AVBufferRef **out_frames_ctx, const int width, const int height)
{
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink *inl = ff_filter_link(inlink);
    AVHWFramesContext *in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
    int ret;

    *out_frames_ctx = av_hwframe_ctx_alloc(in_frames_ctx->device_ref);
    if (!*out_frames_ctx) {
        return AVERROR(ENOMEM);
    }

    AVHWFramesContext *out_fc = (AVHWFramesContext *)(*out_frames_ctx)->data;
    out_fc->format    = AV_PIX_FMT_CUDA;
    out_fc->sw_format = in_frames_ctx->sw_format;

    out_fc->width     = FFALIGN(width, 32);
    out_fc->height    = FFALIGN(height, 32);

    ret = av_hwframe_ctx_init(*out_frames_ctx);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to init output ctx\n");
        av_buffer_unref(out_frames_ctx);
        return ret;
    }

    return 0;
}

static int npppad_init(AVFilterContext *ctx)
{
    NPPPadContext *s = ctx->priv;
    if (!s) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate NPPPadContext.\n");
        return AVERROR(ENOMEM);
    }

    s->last_out_w = -1;
    s->last_out_h = -1;

    return 0;
}

static void npppad_uninit(AVFilterContext *ctx)
{
    NPPPadContext *s = ctx->priv;
    av_buffer_unref(&s->frames_ctx);
}

static int npppad_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    NPPPadContext *s = ctx->priv;

    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink *inl = ff_filter_link(inlink);

    FilterLink *ol = ff_filter_link(outlink);

    AVHWFramesContext *in_frames_ctx;
    int format_supported = 0;
    int ret;

    s->in_w = inlink->w;
    s->in_h = inlink->h;
    ret = eval_expr(ctx);
    if (ret < 0)
        return ret;

    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }

    in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;

    for (int i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++) {
        if (in_frames_ctx->sw_format == supported_formats[i]) {
            format_supported = 1;
            break;
        }
    }
    if (!format_supported) {
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format.\n");
        return AVERROR(EINVAL);
    }

    uint8_t R = s->rgba_color[0];
    uint8_t G = s->rgba_color[1];
    uint8_t B = s->rgba_color[2];

    int Y = (( 66 * R + 129 * G +  25 * B + 128) >> 8) + 16;
    int U = ((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128;
    int V = ((112 * R -  94 * G -  18 * B + 128) >> 8) + 128;
    s->parsed_color[0] = av_clip_uint8(Y);
    s->parsed_color[1] = av_clip_uint8(U);
    s->parsed_color[2] = av_clip_uint8(V);
    s->parsed_color[3] = s->rgba_color[3];

    ret = npppad_alloc_out_frames_ctx(ctx, &s->frames_ctx, s->w, s->h);
    if (ret < 0)
        return ret;

    ol->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
    if (!ol->hw_frames_ctx)
        return AVERROR(ENOMEM);

    outlink->w         = s->w;
    outlink->h         = s->h;
    outlink->time_base = inlink->time_base;
    outlink->format    = AV_PIX_FMT_CUDA;

    s->last_out_w = s->w;
    s->last_out_h = s->h;

    return 0;
}

static int npppad_pad(AVFilterContext *ctx, AVFrame *out, const AVFrame *in)
{
    NPPPadContext *s = ctx->priv;
    FilterLink *inl = ff_filter_link(ctx->inputs[0]);

    AVHWFramesContext *in_frames_ctx =
        (AVHWFramesContext *)inl->hw_frames_ctx->data;
    const AVPixFmtDescriptor *desc_in =
        av_pix_fmt_desc_get(in_frames_ctx->sw_format);


    const int nb_planes = av_pix_fmt_count_planes(in_frames_ctx->sw_format);
    for (int plane = 0; plane < nb_planes; plane++) {

        int hsub = (plane == 1 || plane == 2) ? desc_in->log2_chroma_w : 0;
        int vsub = (plane == 1 || plane == 2) ? desc_in->log2_chroma_h : 0;

        if (in_frames_ctx->sw_format == AV_PIX_FMT_NV12 && plane == 1) {
            hsub = desc_in->log2_chroma_w;
            vsub = desc_in->log2_chroma_h;
        }

        int src_w = AV_CEIL_RSHIFT(s->in_w, hsub);
        int src_h = AV_CEIL_RSHIFT(s->in_h, vsub);

        int dst_w = AV_CEIL_RSHIFT(s->w, hsub);
        int dst_h = AV_CEIL_RSHIFT(s->h, vsub);

        int y_plane_offset = AV_CEIL_RSHIFT(s->y, vsub);
        int x_plane_offset = AV_CEIL_RSHIFT(s->x, hsub);

        if (x_plane_offset + src_w > dst_w || y_plane_offset + src_h > dst_h) {
            av_log(ctx, AV_LOG_ERROR,
                   "ROI out of bounds in plane %d: offset=(%d,%d) in=(%dx%d) "
                   "out=(%dx%d)\n",
                   plane, x_plane_offset, y_plane_offset, src_w, src_h, dst_w, dst_h);
            return AVERROR(EINVAL);
        }

        NppStatus st;

        if (in_frames_ctx->sw_format == AV_PIX_FMT_NV12 && plane == 1) {
            /**
             * There is no nppiCopyConstBorder function that can handle a UV pair so instead
             * we create the color plane with nppiSet_8u_C2R and then copy over the existing UV plane to our newly created plane
             */
            Npp8u fill_val[2] = { s->parsed_color[1], s->parsed_color[2] };
            NppiSize dst_plane_size = { dst_w, dst_h };

            st = nppiSet_8u_C2R(fill_val, out->data[plane], out->linesize[plane], dst_plane_size);
            if (st != NPP_SUCCESS) {
                av_log(ctx, AV_LOG_ERROR,
                       "nppiSet_8u_C2R plane=%d error=%d\n", plane,
                       st);
                return AVERROR_EXTERNAL;
            }


            if (src_w > 0 && src_h > 0) {
                NppiSize src_roi_size_bytes = { src_w * 2, src_h };
                Npp8u *dst_roi_start = out->data[plane] + (y_plane_offset * out->linesize[plane]) + (x_plane_offset * 2);

                st = nppiCopy_8u_C1R(in->data[plane],
                    in->linesize[plane],
                    dst_roi_start,
                    out->linesize[plane],
                    src_roi_size_bytes);
                if (st != NPP_SUCCESS) {
                    av_log(ctx, AV_LOG_ERROR,
                           "nppiCopy_8u_C1R plane=%d error=%d\n", plane,
                           st);
                    return AVERROR_EXTERNAL;
                }
            }
        } else {
            Npp8u fill_val = s->parsed_color[plane];
            NppiSize src_size_roi = { src_w, src_h };
            NppiSize dst_size_roi = { dst_w, dst_h };

            st = nppiCopyConstBorder_8u_C1R(
                in->data[plane], in->linesize[plane], src_size_roi,
                out->data[plane], out->linesize[plane], dst_size_roi,
                y_plane_offset, x_plane_offset, fill_val);

            if (st != NPP_SUCCESS) {
                av_log(ctx, AV_LOG_ERROR,
                       "nppiCopyConstBorder_8u_C1R plane=%d error=%d\n", plane,
                       st);
                return AVERROR_EXTERNAL;
            }
        }
    }

    return 0;
}

static int npppad_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    NPPPadContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    FilterLink *outl = ff_filter_link(outlink);

    AVHWFramesContext *out_frames_ctx =
        (AVHWFramesContext *)outl->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = out_frames_ctx->device_ctx->hwctx;

    int ret;

    if (s->eval_mode == EVAL_MODE_FRAME) {
        s->in_w   = in->width;
        s->in_h   = in->height;
        s->aspect = in->sample_aspect_ratio;

        ret = eval_expr(ctx);
        if (ret < 0) {
            av_frame_free(&in);
            return ret;
        }
    }


    if (s->x == 0 && s->y == 0 &&
        s->w == in->width && s->h == in->height) {
        av_log(ctx, AV_LOG_DEBUG, "No border. Passing the frame unmodified.\n");
        s->last_out_w = s->w;
        s->last_out_h = s->h;
        return ff_filter_frame(outlink, in);
    }


    if (s->w != s->last_out_w || s->h != s->last_out_h) {

        av_buffer_unref(&s->frames_ctx);

        ret = npppad_alloc_out_frames_ctx(ctx, &s->frames_ctx, s->w, s->h);
        if (ret < 0)
            return ret;

        av_buffer_unref(&outl->hw_frames_ctx);
        outl->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
        if (!outl->hw_frames_ctx) {
            av_frame_free(&in);
            av_log(ctx, AV_LOG_ERROR, "Failed to allocate output frame context.\n");
            return AVERROR(ENOMEM);
        }
        outlink->w = s->w;
        outlink->h = s->h;

        s->last_out_w = s->w;
        s->last_out_h = s->h;
    }

    AVFrame *out = av_frame_alloc();
    if (!out) {
        av_frame_free(&in);
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate output AVFrame.\n");
        return AVERROR(ENOMEM);
    }
    ret = av_hwframe_get_buffer(outl->hw_frames_ctx, out, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Unable to get output buffer: %s\n",
               av_err2str(ret));
        av_frame_free(&out);
        av_frame_free(&in);
        return ret;
    }

    CUcontext dummy;
    ret = CHECK_CU(device_hwctx->internal->cuda_dl->cuCtxPushCurrent(
        device_hwctx->cuda_ctx));
    if (ret < 0) {
        av_frame_free(&out);
        av_frame_free(&in);
        return ret;
    }

    ret = npppad_pad(ctx, out, in);

    CHECK_CU(device_hwctx->internal->cuda_dl->cuCtxPopCurrent(&dummy));

    if (ret < 0) {
        av_frame_free(&out);
        av_frame_free(&in);
        return ret;
    }

    av_frame_copy_props(out, in);
    out->width  = s->w;
    out->height = s->h;


    av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
              (int64_t)in->sample_aspect_ratio.num * out->height * in->width,
              (int64_t)in->sample_aspect_ratio.den * out->width * in->height,
              INT_MAX);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(NPPPadContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption npppad_options[] = {
    { "width",  "set the pad area width expression",                             OFFSET(w_expr),     AV_OPT_TYPE_STRING,   {.str = "iw"},       0, 0,        FLAGS },
    { "w",      "set the pad area width expression",                             OFFSET(w_expr),     AV_OPT_TYPE_STRING,   {.str = "iw"},       0, 0,        FLAGS },
    { "height", "set the pad area height expression",                            OFFSET(h_expr),     AV_OPT_TYPE_STRING,   {.str = "ih"},       0, 0,        FLAGS },
    { "h",      "set the pad area height expression",                            OFFSET(h_expr),     AV_OPT_TYPE_STRING,   {.str = "ih"},       0, 0,        FLAGS },
    { "x",      "set the x offset expression for the input image position",      OFFSET(x_expr),     AV_OPT_TYPE_STRING,   {.str = "0"},        0, 0,        FLAGS },
    { "y",      "set the y offset expression for the input image position",      OFFSET(y_expr),     AV_OPT_TYPE_STRING,   {.str = "0"},        0, 0,        FLAGS },
    { "color",  "set the color of the padded area border",                       OFFSET(rgba_color), AV_OPT_TYPE_COLOR,    {.str = "black"},    .flags =      FLAGS },
    { "eval",   "specify when to evaluate expressions",                          OFFSET(eval_mode),  AV_OPT_TYPE_INT,      {.i64 = EVAL_MODE_INIT}, 0, EVAL_MODE_NB-1, FLAGS, .unit = "eval" },
         { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_INIT},  .flags = FLAGS, .unit = "eval" },
         { "frame", "eval expressions during initialization and per-frame", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = FLAGS, .unit = "eval" },
    { "aspect", "pad to fit an aspect instead of a resolution",                  OFFSET(aspect),     AV_OPT_TYPE_RATIONAL, {.dbl = 0},        0, DBL_MAX,    FLAGS },
    { NULL }
};

static const AVClass npppad_class = {
    .class_name = "pad_npp",
    .item_name  = av_default_item_name,
    .option     = npppad_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad npppad_inputs[] = {{
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = npppad_filter_frame
}};

static const AVFilterPad npppad_outputs[] = {{
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = npppad_config_props,
}};

const FFFilter ff_vf_pad_npp = {
    .p.name         = "pad_npp",
    .p.description  = NULL_IF_CONFIG_SMALL("NPP-based GPU padding filter"),
    .init           = npppad_init,
    .uninit         = npppad_uninit,

    .p.priv_class   = &npppad_class,

    FILTER_INPUTS(npppad_inputs),
    FILTER_OUTPUTS(npppad_outputs),

    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),

    .priv_size      = sizeof(NPPPadContext),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};