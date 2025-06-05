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

#ifndef AVFILTER_CUDA_CUDA_VPP_H
#define AVFILTER_CUDA_CUDA_VPP_H

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/cuda_check.h"
#include "libavfilter/avfilter.h"

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

typedef struct CUDAVPPContext {
    const AVClass *class;

    AVCUDADeviceContext *hwctx;
    CudaFunctions *cuda_dl;
    AVBufferRef *device_ref;

    CUcontext   cu_ctx;
    CUmodule    cu_module;
    CUstream    cu_stream;

    AVBufferRef       *input_frames_ref;
    AVHWFramesContext *input_frames;

    enum AVPixelFormat output_format;
    int output_width;   // computed width
    int output_height;  // computed height

    int passthrough;

    // Format information
    enum AVPixelFormat in_fmt;
    const AVPixFmtDescriptor *in_desc;
    int in_planes;
    int in_plane_depths[4];
    int in_plane_channels[4];

    // Function pointers for filter-specific operations
    int (*load_functions)(AVFilterContext *avctx, enum AVPixelFormat format);
    int (*build_filter_params)(AVFilterContext *avctx);
    void (*pipeline_uninit)(AVFilterContext *avctx);
} CUDAVPPContext;

/**
 * Initialize CUDA VPP context
 */
void ff_cuda_vpp_ctx_init(AVFilterContext *avctx);

/**
 * Uninitialize CUDA VPP context
 */
void ff_cuda_vpp_ctx_uninit(AVFilterContext *avctx);

/**
 * Query supported formats for CUDA VPP
 */
int ff_cuda_vpp_query_formats(const AVFilterContext *avctx,
                              AVFilterFormatsConfig **cfg_in,
                              AVFilterFormatsConfig **cfg_out);

/**
 * Configure input for CUDA VPP
 */
int ff_cuda_vpp_config_input(AVFilterLink *inlink);

/**
 * Configure output for CUDA VPP
 */
int ff_cuda_vpp_config_output(AVFilterLink *outlink);

/**
 * Check if a pixel format is supported
 */
int ff_cuda_vpp_format_is_supported(enum AVPixelFormat fmt, const enum AVPixelFormat *supported_formats, int nb_formats);

/**
 * Setup plane information for a given format
 */
int ff_cuda_vpp_setup_planes(CUDAVPPContext *s, enum AVPixelFormat format);

/**
 * Load CUDA module from PTX data
 */
int ff_cuda_vpp_load_module(AVFilterContext *ctx, CUDAVPPContext *s,
                            const unsigned char *ptx_data, unsigned int ptx_len);

/**
 * Get CUDA function from loaded module
 */
int ff_cuda_vpp_get_function(AVFilterContext *ctx, CUDAVPPContext *s,
                             CUfunction *func, const char *func_name);

#endif /* AVFILTER_CUDA_CUDA_VPP_H */
