/*
 * Copyright (c) 2026, Faeez Kadiri < f1k2faeez at gmail dot com>
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
 * CUDA accelerated HDR to SDR tonemapping filter
 */

#include <float.h>
#include <math.h>
#include <string.h>

#include "filters.h"
#include "libavutil/cuda_check.h"
#include "libavutil/csp.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "colorspace.h"
#include "cuda/load_helper.h"
#include "vf_tonemap_cuda.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, device_hwctx->internal->cuda_dl, x)
#define DIV_UP(a, b) (((a) + (b) - 1) / (b))
#define BLOCKX 32
#define BLOCKY 16

/** Private context for the tonemap_cuda filter. */
typedef struct TonemapCUDAContext {
    const AVClass *class;

    /** @name User-facing options (set via AVOption) */
    /**@{*/
    enum AVColorSpace colorspace;
    enum AVColorTransferCharacteristic trc;
    enum AVColorPrimaries primaries;
    enum AVColorRange range;
    int tonemap;                   ///< selected algorithm
    enum AVPixelFormat format;     ///< output pixel format
    double peak;                   ///< signal peak override (0 = auto)
    double param;                  ///< algorithm tuning knob
    double desat_param;            ///< highlight desaturation strength
    /**@}*/

    /** @name Resolved per-stream color properties */
    /**@{*/
    enum AVColorSpace colorspace_in, colorspace_out;
    enum AVColorTransferCharacteristic trc_in, trc_out;
    enum AVColorPrimaries primaries_in, primaries_out;
    enum AVColorRange range_in, range_out;
    enum AVChromaLocation chroma_loc;
    /**@}*/

    double target_peak;            ///< SDR target peak (normally 1.0)
    int initialised;               ///< set once CUDA module is loaded

    AVCUDADeviceContext *hwctx;    ///< CUDA device context
    CUmodule cu_module;            ///< loaded PTX module
    CUfunction cu_func;            ///< tonemap kernel entry point
    AVBufferRef *frames_ctx;       ///< output hw frames context

    /** @name Precomputed matrices uploaded to the GPU each frame */
    /**@{*/
    float rgb_matrix[9];           ///< YUV-to-RGB (source)
    float yuv_matrix[9];           ///< RGB-to-YUV (destination)
    float rgb2rgb_matrix[9];       ///< gamut conversion
    float luma_src[3];             ///< source luma coefficients
    float luma_dst[3];             ///< destination luma coefficients
    int rgb2rgb_passthrough;       ///< 1 if primaries match
    int src_trc;                   ///< TransferFuncCUDA value
    int dst_trc;                   ///< DelinearizeFuncCUDA value
    /**@}*/
} TonemapCUDAContext;

/**
 * Compute a 3x3 RGB-to-RGB colour-primary conversion matrix.
 *
 * @param in     source colour primaries
 * @param out    destination colour primaries
 * @param rgb2rgb output 3x3 matrix
 * @return 0 on success, negative AVERROR on failure
 */
static int get_rgb2rgb_matrix(enum AVColorPrimaries in,
                              enum AVColorPrimaries out,
                              double rgb2rgb[3][3])
{
    double rgb2xyz[3][3], xyz2rgb[3][3];
    const AVColorPrimariesDesc *in_primaries =
        av_csp_primaries_desc_from_id(in);
    const AVColorPrimariesDesc *out_primaries =
        av_csp_primaries_desc_from_id(out);

    if (!in_primaries || !out_primaries)
        return AVERROR(EINVAL);

    ff_fill_rgb2xyz_table(&out_primaries->prim, &out_primaries->wp, rgb2xyz);
    ff_matrix_invert_3x3(rgb2xyz, xyz2rgb);
    ff_fill_rgb2xyz_table(&in_primaries->prim, &in_primaries->wp, rgb2xyz);
    ff_matrix_mul_3x3(rgb2rgb, rgb2xyz, xyz2rgb);

    return 0;
}

/** Flatten a 3x3 double matrix into a row-major float[9] array. */
static void double_matrix_to_float9(const double m[3][3], float out[9])
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            out[i * 3 + j] = (float)m[i][j];
}

/**
 * Compute colour-space conversion matrices and transfer-function
 * mappings from the current stream colour properties.
 *
 * Called once on the first frame and again whenever input colour
 * metadata changes mid-stream.
 *
 * @return 0 on success, negative AVERROR on failure
 */
static int tonemap_cuda_setup(AVFilterContext *ctx)
{
    TonemapCUDAContext *s = ctx->priv;
    double rgb2yuv_src[3][3], yuv2rgb_src[3][3];
    double rgb2yuv_dst[3][3];
    double rgb2rgb[3][3];
    const AVLumaCoefficients *luma_src, *luma_dst;

    /* Tonemap param defaults (matching tonemap_opencl) */
    switch (s->tonemap) {
    case TONEMAP_GAMMA_CUDA:
        if (isnan(s->param))
            s->param = 1.8;
        break;
    case TONEMAP_REINHARD_CUDA:
        if (!isnan(s->param))
            s->param = (1.0 - s->param) / s->param;
        break;
    case TONEMAP_MOBIUS_CUDA:
        if (isnan(s->param))
            s->param = 0.3;
        break;
    }
    if (isnan(s->param))
        s->param = 1.0;

    s->target_peak = 1.0;

    av_log(ctx, AV_LOG_DEBUG, "tonemap transfer from %s to %s\n",
           av_color_transfer_name(s->trc_in),
           av_color_transfer_name(s->trc_out));
    av_log(ctx, AV_LOG_DEBUG, "mapping colorspace from %s to %s\n",
           av_color_space_name(s->colorspace_in),
           av_color_space_name(s->colorspace_out));
    av_log(ctx, AV_LOG_DEBUG, "mapping primaries from %s to %s\n",
           av_color_primaries_name(s->primaries_in),
           av_color_primaries_name(s->primaries_out));
    av_log(ctx, AV_LOG_DEBUG, "mapping range from %s to %s\n",
           av_color_range_name(s->range_in),
           av_color_range_name(s->range_out));

    if (s->trc_in != AVCOL_TRC_SMPTE2084 &&
        s->trc_in != AVCOL_TRC_ARIB_STD_B67) {
        av_log(ctx, AV_LOG_ERROR,
               "unsupported input transfer %s, expected PQ or HLG\n",
               av_color_transfer_name(s->trc_in));
        return AVERROR(EINVAL);
    }

    /* Map source HDR transfer to kernel enum */
    s->src_trc = (s->trc_in == AVCOL_TRC_ARIB_STD_B67) ? TRC_HLG_CUDA
                                                         : TRC_ST2084_CUDA;
    /*
     * Output delinearization: BT.2020-10 uses the BT.709 OETF
     * (they share the same curve).  For the default BT.709 SDR
     * target we use the BT.1886 inverse EOTF (pure gamma 2.4)
     * which produces display-referred output better suited to
     * tonemapped content than the BT.709 OETF linear toe.
     */
    s->dst_trc = (s->trc_out == AVCOL_TRC_BT2020_10) ? DELIN_BT709_CUDA
                                                       : DELIN_BT1886_CUDA;

    /* Compute YUV-to-RGB matrix (input colorspace) */
    luma_src = av_csp_luma_coeffs_from_avcsp(s->colorspace_in);
    if (!luma_src) {
        av_log(ctx, AV_LOG_ERROR, "unsupported input colorspace %d (%s)\n",
               s->colorspace_in, av_color_space_name(s->colorspace_in));
        return AVERROR(EINVAL);
    }
    ff_fill_rgb2yuv_table(luma_src, rgb2yuv_src);
    ff_matrix_invert_3x3(rgb2yuv_src, yuv2rgb_src);
    double_matrix_to_float9(yuv2rgb_src, s->rgb_matrix);

    /* Compute RGB-to-YUV matrix (output colorspace) */
    luma_dst = av_csp_luma_coeffs_from_avcsp(s->colorspace_out);
    if (!luma_dst) {
        av_log(ctx, AV_LOG_ERROR, "unsupported output colorspace %d (%s)\n",
               s->colorspace_out, av_color_space_name(s->colorspace_out));
        return AVERROR(EINVAL);
    }
    ff_fill_rgb2yuv_table(luma_dst, rgb2yuv_dst);
    double_matrix_to_float9(rgb2yuv_dst, s->yuv_matrix);

    /* Luma coefficients */
    s->luma_src[0] = (float)av_q2d(luma_src->cr);
    s->luma_src[1] = (float)av_q2d(luma_src->cg);
    s->luma_src[2] = (float)av_q2d(luma_src->cb);
    s->luma_dst[0] = (float)av_q2d(luma_dst->cr);
    s->luma_dst[1] = (float)av_q2d(luma_dst->cg);
    s->luma_dst[2] = (float)av_q2d(luma_dst->cb);

    /* Primaries conversion matrix */
    s->rgb2rgb_passthrough = 1;
    if (s->primaries_out != s->primaries_in) {
        int ret = get_rgb2rgb_matrix(s->primaries_in,
                                     s->primaries_out,
                                     rgb2rgb);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "failed to compute primaries matrix\n");
            return ret;
        }
        double_matrix_to_float9(rgb2rgb, s->rgb2rgb_matrix);
        s->rgb2rgb_passthrough = 0;
    }

    return 0;
}

/**
 * Load the compiled PTX module and resolve the tonemap kernel.
 *
 * @return 0 on success, negative AVERROR on failure
 */
static av_cold int tonemap_cuda_load_functions(AVFilterContext *ctx)
{
    TonemapCUDAContext *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    AVCUDADeviceContext *device_hwctx = s->hwctx;
    CUcontext dummy;
    int ret;

    extern const unsigned char ff_vf_tonemap_cuda_ptx_data[];
    extern const unsigned int  ff_vf_tonemap_cuda_ptx_len;

    ret = CHECK_CU(cu->cuCtxPushCurrent(device_hwctx->cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, device_hwctx, &s->cu_module,
                              ff_vf_tonemap_cuda_ptx_data,
                              ff_vf_tonemap_cuda_ptx_len);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load CUDA module\n");
        goto fail;
    }

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_func,
                                           s->cu_module,
                                           "tonemap"));
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load tonemap kernel\n");
        goto fail;
    }

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ret;
}

/**
 * Validate input format, allocate output CUDA hw-frames context,
 * and propagate link properties.
 */
static int tonemap_cuda_config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TonemapCUDAContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink *inl = ff_filter_link(inlink);
    FilterLink *ol  = ff_filter_link(outlink);
    AVHWFramesContext *in_frames_ctx;
    enum AVPixelFormat out_sw_format;
    int ret;

    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }

    in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
    s->hwctx = in_frames_ctx->device_ctx->hwctx;

    if (in_frames_ctx->sw_format != AV_PIX_FMT_P010) {
        av_log(ctx, AV_LOG_ERROR,
               "Unsupported input format %s, only p010 is supported\n",
               av_get_pix_fmt_name(in_frames_ctx->sw_format));
        return AVERROR(EINVAL);
    }

    if (s->format == AV_PIX_FMT_NONE) {
        av_log(ctx, AV_LOG_WARNING,
               "Output format not set, defaulting to nv12\n");
        out_sw_format = AV_PIX_FMT_NV12;
    } else if (s->format != AV_PIX_FMT_NV12 && s->format != AV_PIX_FMT_P010) {
        av_log(ctx, AV_LOG_ERROR,
               "Unsupported output format %s, only nv12 and p010 are supported\n",
               av_get_pix_fmt_name(s->format));
        return AVERROR(EINVAL);
    } else {
        out_sw_format = s->format;
    }

    s->frames_ctx = av_hwframe_ctx_alloc(in_frames_ctx->device_ref);
    if (!s->frames_ctx)
        return AVERROR(ENOMEM);

    AVHWFramesContext *out_frames = (AVHWFramesContext *)s->frames_ctx->data;
    out_frames->format    = AV_PIX_FMT_CUDA;
    out_frames->sw_format = out_sw_format;
    out_frames->width     = FFALIGN(inlink->w, 32);
    out_frames->height    = FFALIGN(inlink->h, 32);

    ret = av_hwframe_ctx_init(s->frames_ctx);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to init output hw frames ctx\n");
        av_buffer_unref(&s->frames_ctx);
        return ret;
    }

    ol->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
    if (!ol->hw_frames_ctx)
        return AVERROR(ENOMEM);

    outlink->w = inlink->w;
    outlink->h = inlink->h;
    outlink->time_base = inlink->time_base;
    outlink->format = AV_PIX_FMT_CUDA;

    return 0;
}

/**
 * Process one input frame: resolve colour metadata, (re-)initialise
 * matrices if needed, launch the CUDA tonemap kernel, and forward
 * the resulting SDR frame downstream.
 */
static int tonemap_cuda_filter_frame(AVFilterLink *inlink, AVFrame *input)
{
    AVFilterContext *ctx = inlink->dst;
    TonemapCUDAContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    FilterLink *ol = ff_filter_link(outlink);
    AVHWFramesContext *out_frames_ctx =
        (AVHWFramesContext *)ol->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = out_frames_ctx->device_ctx->hwctx;
    CudaFunctions *cu = device_hwctx->internal->cuda_dl;
    CUcontext dummy;
    AVFrame *output = NULL;
    double peak;
    int ret;

    if (!input->hw_frames_ctx)
        return AVERROR(EINVAL);

    if (input->width % 2 || input->height % 2) {
        av_log(ctx, AV_LOG_ERROR,
               "Input dimensions %dx%d must be even for 4:2:0\n",
               input->width, input->height);
        av_frame_free(&input);
        return AVERROR(EINVAL);
    }

    output = av_frame_alloc();
    if (!output) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = av_hwframe_get_buffer(ol->hw_frames_ctx, output, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to get output buffer: %s\n",
               av_err2str(ret));
        goto fail;
    }

    ret = av_frame_copy_props(output, input);
    if (ret < 0)
        goto fail;

    /* Determine signal peak */
    peak = s->peak;
    if (peak <= 0.0)
        peak = ff_determine_signal_peak(input);

    /* Set output color properties */
    if (s->trc != -1)
        output->color_trc = s->trc;
    if (s->primaries != -1)
        output->color_primaries = s->primaries;
    if (s->colorspace != -1)
        output->colorspace = s->colorspace;
    if (s->range != -1)
        output->color_range = s->range;

    {
        int props_changed =
            s->trc_in        != input->color_trc       ||
            s->trc_out       != output->color_trc      ||
            s->colorspace_in != input->colorspace      ||
            s->colorspace_out != output->colorspace    ||
            s->primaries_in  != input->color_primaries ||
            s->primaries_out != output->color_primaries;

        s->trc_in        = input->color_trc;
        s->trc_out       = output->color_trc;
        s->colorspace_in = input->colorspace;
        s->colorspace_out = output->colorspace;
        s->primaries_in  = input->color_primaries;
        s->primaries_out = output->color_primaries;
        s->range_in      = input->color_range;
        s->range_out     = output->color_range;
        s->chroma_loc    = output->chroma_location;

        if (!s->initialised || props_changed) {
            if (s->initialised)
                av_log(ctx, AV_LOG_INFO,
                       "Color properties changed, "
                       "recomputing matrices\n");

            ret = tonemap_cuda_setup(ctx);
            if (ret < 0)
                goto fail;

            if (!s->initialised) {
                ret = tonemap_cuda_load_functions(ctx);
                if (ret < 0)
                    goto fail;
            }

            s->initialised = 1;
        }
    }

    /* Push CUDA context */
    ret = CHECK_CU(cu->cuCtxPushCurrent(device_hwctx->cuda_ctx));
    if (ret < 0)
        goto fail;

    {
        CUDATonemapParams params = {0};

        params.dst_y  = (CUdeviceptr)output->data[0];
        params.dst_uv = (CUdeviceptr)output->data[1];
        params.src_y  = (CUdeviceptr)input->data[0];
        params.src_uv = (CUdeviceptr)input->data[1];

        params.width     = input->width;
        params.height    = input->height;
        params.src_pitch = input->linesize[0];
        params.dst_pitch = output->linesize[0];

        memcpy(params.rgb_matrix,     s->rgb_matrix,     sizeof(s->rgb_matrix));
        memcpy(params.yuv_matrix,     s->yuv_matrix,     sizeof(s->yuv_matrix));
        memcpy(params.rgb2rgb_matrix, s->rgb2rgb_matrix,
               sizeof(s->rgb2rgb_matrix));
        memcpy(params.luma_src,       s->luma_src,       sizeof(s->luma_src));
        memcpy(params.luma_dst,       s->luma_dst,       sizeof(s->luma_dst));

        params.tonemap_func       = s->tonemap;
        params.param              = (float)s->param;
        params.desat_param        = (float)s->desat_param;
        params.signal_peak        = (float)peak;
        params.target_peak        = (float)s->target_peak;

        params.src_trc            = s->src_trc;
        params.dst_trc            = s->dst_trc;
        params.src_range_full     = (s->range_in == AVCOL_RANGE_JPEG);
        params.dst_range_full     = (s->range_out == AVCOL_RANGE_JPEG);
        params.rgb2rgb_passthrough = s->rgb2rgb_passthrough;
        params.chroma_loc         = (int)s->chroma_loc;
        params.out_depth =
            (out_frames_ctx->sw_format == AV_PIX_FMT_P010)
            ? 10 : 8;

        void *args[] = { &params };
        ret = CHECK_CU(cu->cuLaunchKernel(s->cu_func,
                                           DIV_UP(input->width / 2, BLOCKX),
                                           DIV_UP(input->height / 2, BLOCKY), 1,
                                           BLOCKX, BLOCKY, 1,
                                           0, device_hwctx->stream, args, NULL));
        if (ret < 0)
            goto pop_ctx;
    }

pop_ctx:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    if (ret < 0)
        goto fail;

    output->width  = input->width;
    output->height = input->height;

    ff_update_hdr_metadata(output, s->target_peak);

    av_frame_free(&input);

    av_log(ctx, AV_LOG_DEBUG,
           "Tonemap output: %s, %ux%u (%"PRId64")\n",
           av_get_pix_fmt_name(output->format),
           output->width, output->height, output->pts);

    return ff_filter_frame(outlink, output);

fail:
    av_frame_free(&input);
    av_frame_free(&output);
    return ret;
}

/** Release the CUDA module and output frames context. */
static av_cold void tonemap_cuda_uninit(AVFilterContext *ctx)
{
    TonemapCUDAContext *s = ctx->priv;
    CUcontext dummy;

    av_buffer_unref(&s->frames_ctx);

    if (s->hwctx && s->cu_module) {
        CudaFunctions *cu = s->hwctx->internal->cuda_dl;
        AVCUDADeviceContext *device_hwctx = s->hwctx;
        CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
        CHECK_CU(cu->cuModuleUnload(s->cu_module));
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    }

    s->cu_module = NULL;
    s->hwctx = NULL;
}

#define OFFSET(x) offsetof(TonemapCUDAContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption tonemap_cuda_options[] = {
    { "tonemap", "tonemap algorithm selection",
      OFFSET(tonemap), AV_OPT_TYPE_INT,
      {.i64 = TONEMAP_NONE_CUDA},
      TONEMAP_NONE_CUDA, TONEMAP_MAX_CUDA - 1,
      FLAGS, .unit = "tonemap" },
    {     "none",     0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_NONE_CUDA},     0, 0, FLAGS, .unit = "tonemap" },
    {     "linear",   0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_LINEAR_CUDA},   0, 0, FLAGS, .unit = "tonemap" },
    {     "gamma",    0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_GAMMA_CUDA},    0, 0, FLAGS, .unit = "tonemap" },
    {     "clip",     0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_CLIP_CUDA},     0, 0, FLAGS, .unit = "tonemap" },
    {     "reinhard", 0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_REINHARD_CUDA}, 0, 0, FLAGS, .unit = "tonemap" },
    {     "hable",    0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_HABLE_CUDA},    0, 0, FLAGS, .unit = "tonemap" },
    {     "mobius",   0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_MOBIUS_CUDA},    0, 0, FLAGS, .unit = "tonemap" },
    { "transfer", "set transfer characteristic",
      OFFSET(trc), AV_OPT_TYPE_INT, {.i64 = AVCOL_TRC_BT709}, -1, INT_MAX, FLAGS, .unit = "transfer" },
    { "t",        "set transfer characteristic",
      OFFSET(trc), AV_OPT_TYPE_INT, {.i64 = AVCOL_TRC_BT709}, -1, INT_MAX, FLAGS, .unit = "transfer" },
    {     "bt709",  0, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT709},     0, 0, FLAGS, .unit = "transfer" },
    {     "bt2020", 0, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_TRC_BT2020_10}, 0, 0, FLAGS, .unit = "transfer" },
    { "matrix", "set colorspace matrix",
      OFFSET(colorspace), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, .unit = "matrix" },
    { "m",      "set colorspace matrix",
      OFFSET(colorspace), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, .unit = "matrix" },
    {     "bt709",  0, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_SPC_BT709},      0, 0, FLAGS, .unit = "matrix" },
    {     "bt2020", 0, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_SPC_BT2020_NCL}, 0, 0, FLAGS, .unit = "matrix" },
    { "primaries", "set color primaries",
      OFFSET(primaries), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, .unit = "primaries" },
    { "p",         "set color primaries",
      OFFSET(primaries), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, .unit = "primaries" },
    {     "bt709",  0, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_PRI_BT709},  0, 0, FLAGS, .unit = "primaries" },
    {     "bt2020", 0, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_PRI_BT2020}, 0, 0, FLAGS, .unit = "primaries" },
    { "range", "set color range",
      OFFSET(range), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, .unit = "range" },
    { "r",     "set color range",
      OFFSET(range), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS, .unit = "range" },
    {     "tv",      0, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_MPEG}, 0, 0, FLAGS, .unit = "range" },
    {     "pc",      0, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_JPEG}, 0, 0, FLAGS, .unit = "range" },
    {     "limited", 0, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_MPEG}, 0, 0, FLAGS, .unit = "range" },
    {     "full",    0, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_JPEG}, 0, 0, FLAGS, .unit = "range" },
    { "format", "output pixel format",
      OFFSET(format), AV_OPT_TYPE_PIXEL_FMT, {.i64 = AV_PIX_FMT_NONE},
      AV_PIX_FMT_NONE, INT_MAX, FLAGS, .unit = "fmt" },
    { "peak", "signal peak override",
      OFFSET(peak), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, 0, DBL_MAX, FLAGS },
    { "param", "tonemap parameter",
      OFFSET(param), AV_OPT_TYPE_DOUBLE, {.dbl = NAN}, DBL_MIN, DBL_MAX, FLAGS },
    { "desat", "desaturation parameter",
      OFFSET(desat_param), AV_OPT_TYPE_DOUBLE, {.dbl = 0.5}, 0, DBL_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tonemap_cuda);

static const AVFilterPad tonemap_cuda_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = tonemap_cuda_filter_frame,
    },
};

static const AVFilterPad tonemap_cuda_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = tonemap_cuda_config_output,
    },
};

const FFFilter ff_vf_tonemap_cuda = {
    .p.name         = "tonemap_cuda",
    .p.description  = NULL_IF_CONFIG_SMALL("CUDA accelerated HDR to SDR tonemapping"),
    .p.priv_class   = &tonemap_cuda_class,
    .priv_size      = sizeof(TonemapCUDAContext),
    .uninit         = tonemap_cuda_uninit,
    FILTER_INPUTS(tonemap_cuda_inputs),
    FILTER_OUTPUTS(tonemap_cuda_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
