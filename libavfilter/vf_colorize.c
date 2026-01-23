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

#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"


static const char *const var_names[] = {
    "TB",                ///< timebase

    "pts",               ///< original pts in the file of the frame
    "start_pts",         ///< first PTS in the stream, expressed in TB units
    "prev_pts",          ///< previous frame PTS

    "t",                 ///< timestamp expressed in seconds
    "start_t",           ///< first PTS in the stream, expressed in seconds
    "prev_t",            ///< previous frame time

    "n",                 ///< frame number (starting from zero)

    "ih",                ///< ih: Represents the height of the input video frame.
    "iw",                ///< iw: Represents the width of the input video frame.

    NULL
};

enum var_name {
    VAR_TB,

    VAR_PTS,
    VAR_START_PTS,
    VAR_PREV_PTS,

    VAR_T,
    VAR_START_T,
    VAR_PREV_T,

    VAR_N,

    VAR_IH,
    VAR_IW,

    VAR_VARS_NB
};

typedef struct ColorizeContext {
    const AVClass *class;

    char *hue_str;
    char *saturation_str;
    char *lightness_str;
    char *mix_str;

    AVExpr *hue_expr;
    AVExpr *saturation_expr;
    AVExpr *lightness_expr;
    AVExpr *mix_expr;

    float mix;

    double var_values[VAR_VARS_NB];

    int depth;
    int c[3];
    int planewidth[4];
    int planeheight[4];

    int (*do_plane_slice[2])(AVFilterContext *s, void *arg,
                             int jobnr, int nb_jobs);
} ColorizeContext;

static inline float lerpf(float v0, float v1, float f)
{
    return v0 + (v1 - v0) * f;
}

static int colorizey_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorizeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width = s->planewidth[0];
    const int height = s->planeheight[0];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ylinesize = frame->linesize[0];
    uint8_t *yptr = frame->data[0] + slice_start * ylinesize;
    const int yv = s->c[0];
    const float mix = s->mix;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++)
            yptr[x] = lerpf(yv, yptr[x], mix);

        yptr += ylinesize;
    }

    return 0;
}

static int colorizey_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorizeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width = s->planewidth[0];
    const int height = s->planeheight[0];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ylinesize = frame->linesize[0] / 2;
    uint16_t *yptr = (uint16_t *)frame->data[0] + slice_start * ylinesize;
    const int yv = s->c[0];
    const float mix = s->mix;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++)
            yptr[x] = lerpf(yv, yptr[x], mix);

        yptr += ylinesize;
    }

    return 0;
}

static int colorize_slice8(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorizeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ulinesize = frame->linesize[1];
    const ptrdiff_t vlinesize = frame->linesize[2];
    uint8_t *uptr = frame->data[1] + slice_start * ulinesize;
    uint8_t *vptr = frame->data[2] + slice_start * vlinesize;
    const int u = s->c[1];
    const int v = s->c[2];

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            uptr[x] = u;
            vptr[x] = v;
        }

        uptr += ulinesize;
        vptr += vlinesize;
    }

    return 0;
}

static int colorize_slice16(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorizeContext *s = ctx->priv;
    AVFrame *frame = arg;
    const int width = s->planewidth[1];
    const int height = s->planeheight[1];
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const ptrdiff_t ulinesize = frame->linesize[1] / 2;
    const ptrdiff_t vlinesize = frame->linesize[2] / 2;
    uint16_t *uptr = (uint16_t *)frame->data[1] + slice_start * ulinesize;
    uint16_t *vptr = (uint16_t *)frame->data[2] + slice_start * vlinesize;
    const int u = s->c[1];
    const int v = s->c[2];

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < width; x++) {
            uptr[x] = u;
            vptr[x] = v;
        }

        uptr += ulinesize;
        vptr += vlinesize;
    }

    return 0;
}

static int do_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorizeContext *s = ctx->priv;

    s->do_plane_slice[0](ctx, arg, jobnr, nb_jobs);
    s->do_plane_slice[1](ctx, arg, jobnr, nb_jobs);

    return 0;
}

static float hue2rgb(float p, float q, float t)
{
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f/6.f) return p + (q - p) * 6.f * t;
    if (t < 1.f/2.f) return q;
    if (t < 2.f/3.f) return p + (q - p) * (2.f/3.f - t) * 6.f;

    return p;
}

static void hsl2rgb(float h, float s, float l, float *r, float *g, float *b)
{
    h /= 360.f;

    if (s == 0.f) {
        *r = *g = *b = l;
    } else {
        const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
        const float p = 2.f * l - q;

        *r = hue2rgb(p, q, h + 1.f / 3.f);
        *g = hue2rgb(p, q, h);
        *b = hue2rgb(p, q, h - 1.f / 3.f);
    }
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

#define PARSE_EXPR(NAME) \
    if ((ret = av_expr_parse(&colorize->NAME ## _expr, colorize->NAME ## _str, \
                             var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) { \
        av_log(ctx, AV_LOG_ERROR, "Error while parsing " #NAME " expression '%s'\n", \
               colorize->NAME ## _str); \
        return ret; \
    }


static av_cold int init(AVFilterContext *ctx)
{
    ColorizeContext *colorize = ctx->priv;
    int ret;

    PARSE_EXPR(hue);
    PARSE_EXPR(saturation);
    PARSE_EXPR(lightness);
    PARSE_EXPR(mix);

    return 0;
}

#undef PARSE_EXPR

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    ColorizeContext *colorize = ctx->priv;
    FilterLink *inl = ff_filter_link(inlink);
    float hue;
    float saturation;
    float lightness;
    float c[3];

    // prepare vars
    if (isnan(colorize->var_values[VAR_START_PTS]))
        colorize->var_values[VAR_START_PTS] = TS2D(frame->pts);
    if (isnan(colorize->var_values[VAR_START_T]))
        colorize->var_values[VAR_START_T] = TS2D(frame->pts) * av_q2d(inlink->time_base);

    colorize->var_values[VAR_N  ] = inl->frame_count_out - 1;
    colorize->var_values[VAR_PTS] = TS2D(frame->pts);
    colorize->var_values[VAR_T  ] = TS2D(frame->pts) * av_q2d(inlink->time_base);

    // eval expr
    hue           = av_expr_eval(colorize->hue_expr,        colorize->var_values, NULL);
    saturation    = av_expr_eval(colorize->saturation_expr, colorize->var_values, NULL);
    lightness     = av_expr_eval(colorize->lightness_expr,  colorize->var_values, NULL);
    colorize->mix = av_expr_eval(colorize->mix_expr,        colorize->var_values, NULL);

    hsl2rgb(hue, saturation, lightness, &c[0], &c[1], &c[2]);
    rgb2yuv(c[0], c[1], c[2], &colorize->c[0], &colorize->c[1], &colorize->c[2], colorize->depth);

    ff_filter_execute(ctx, do_slice, frame, NULL,
                      FFMIN(colorize->planeheight[1], ff_filter_get_nb_threads(ctx)));

    colorize->var_values[VAR_PREV_PTS] = colorize->var_values[VAR_PTS];
    colorize->var_values[VAR_PREV_T]   = colorize->var_values[VAR_T];

    return ff_filter_frame(ctx->outputs[0], frame);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ColorizeContext *colorize = ctx->priv;

    av_expr_free(colorize->hue_expr);
    colorize->hue_expr = NULL;
    av_expr_free(colorize->saturation_expr);
    colorize->saturation_expr = NULL;
    av_expr_free(colorize->lightness_expr);
    colorize->lightness_expr = NULL;
    av_expr_free(colorize->mix_expr);
    colorize->mix_expr = NULL;
}

static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P,  AV_PIX_FMT_YUVA422P,   AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_NONE
};

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ColorizeContext *colorize = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int depth;

    colorize->depth = depth = desc->comp[0].depth;

    colorize->planewidth[1] = colorize->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    colorize->planewidth[0] = colorize->planewidth[3] = inlink->w;
    colorize->planeheight[1] = colorize->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    colorize->planeheight[0] = colorize->planeheight[3] = inlink->h;

    colorize->do_plane_slice[0] = depth <= 8 ? colorizey_slice8 : colorizey_slice16;
    colorize->do_plane_slice[1] = depth <= 8 ? colorize_slice8 : colorize_slice16;

    // set expression variables
    colorize->var_values[VAR_TB]        = av_q2d(inlink->time_base);

    colorize->var_values[VAR_PREV_PTS]  = NAN;
    colorize->var_values[VAR_PREV_T]    = NAN;
    colorize->var_values[VAR_START_PTS] = NAN;
    colorize->var_values[VAR_START_T]   = NAN;

    colorize->var_values[VAR_N]         = 0.0;

    colorize->var_values[VAR_IH]        = NAN;
    colorize->var_values[VAR_IW]        = NAN;

    return 0;
}

static const AVFilterPad colorize_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

#define OFFSET(x) offsetof(ColorizeContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption colorize_options[] = {
    { "hue",        "set the hue",                     OFFSET(hue_str),        AV_OPT_TYPE_STRING, {.str="0"},   0, 0, .flags=VF },
    { "saturation", "set the saturation",              OFFSET(saturation_str), AV_OPT_TYPE_STRING, {.str="0.5"}, 0, 0, .flags=VF },
    { "lightness",  "set the lightness",               OFFSET(lightness_str),  AV_OPT_TYPE_STRING, {.str="0.5"}, 0, 0, .flags=VF },
    { "mix",        "set the mix of source lightness", OFFSET(mix_str),        AV_OPT_TYPE_STRING, {.str="1"},   0, 0, .flags=VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorize);

const FFFilter ff_vf_colorize = {
    .p.name        = "colorize",
    .p.description = NULL_IF_CONFIG_SMALL("Overlay a solid color on the video stream."),
    .p.priv_class  = &colorize_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .priv_size     = sizeof(ColorizeContext),
    FILTER_INPUTS(colorize_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .process_command = ff_filter_process_command,
    .init = init,
    .uninit = uninit,
};
