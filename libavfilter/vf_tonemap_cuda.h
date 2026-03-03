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

#ifndef AVFILTER_VF_TONEMAP_CUDA_H
#define AVFILTER_VF_TONEMAP_CUDA_H

#if defined(__CUDACC__) || defined(__CUDA__)
#include <stdint.h>
typedef uint8_t* CUdeviceptr_t;
#else
#include <ffnvcodec/dynlink_cuda.h>
typedef CUdeviceptr CUdeviceptr_t;
#endif

/** Tonemap algorithm selection, mirrored in the host-side enum. */
enum TonemapAlgoCUDA {
    TONEMAP_NONE_CUDA,
    TONEMAP_LINEAR_CUDA,
    TONEMAP_GAMMA_CUDA,
    TONEMAP_CLIP_CUDA,
    TONEMAP_REINHARD_CUDA,
    TONEMAP_HABLE_CUDA,
    TONEMAP_MOBIUS_CUDA,
    TONEMAP_MAX_CUDA,
};

/** Source HDR transfer function for the EOTF linearization step. */
enum TransferFuncCUDA {
    TRC_ST2084_CUDA = 0,
    TRC_HLG_CUDA    = 1,
};

/** Output SDR delinearization curve selection. */
enum DelinearizeFuncCUDA {
    DELIN_BT1886_CUDA = 0, ///< inverse EOTF, gamma 2.4
    DELIN_BT709_CUDA  = 1, ///< BT.709 OETF with linear segment
};

/**
 * Kernel parameter block passed by value to the CUDA tonemap kernel.
 * Shared between the host C code and the device .cu code.
 */
typedef struct CUDATonemapParams {
    CUdeviceptr_t dst_y;          ///< output luma plane
    CUdeviceptr_t dst_uv;         ///< output chroma plane (interleaved UV)
    CUdeviceptr_t src_y;          ///< input luma plane (P010)
    CUdeviceptr_t src_uv;         ///< input chroma plane (P010, interleaved UV)

    int width;                    ///< frame width in pixels
    int height;                   ///< frame height in pixels
    int src_pitch;                ///< input plane pitch in bytes
    int dst_pitch;                ///< output plane pitch in bytes

    float rgb_matrix[9];          ///< YUV-to-RGB matrix (source colorspace)
    float yuv_matrix[9];          ///< RGB-to-YUV matrix (output colorspace)
    float rgb2rgb_matrix[9];      ///< gamut conversion (e.g. BT.2020 to BT.709)
    float luma_src[3];            ///< source luma coefficients (cr, cg, cb)
    float luma_dst[3];            ///< destination luma coefficients

    int tonemap_func;             ///< algorithm, one of TonemapAlgoCUDA
    float param;                  ///< algorithm-specific tuning parameter
    float desat_param;            ///< highlight desaturation strength
    float signal_peak;            ///< HDR signal peak (multiples of ref white)
    float target_peak;            ///< SDR target peak (normally 1.0)

    int src_trc;                  ///< source transfer, one of TransferFuncCUDA
    int dst_trc;                  ///< dest transfer, one of DelinearizeFuncCUDA
    int src_range_full;           ///< 1 if source is full-range (JPEG)
    int dst_range_full;           ///< 1 if output is full-range (JPEG)
    int rgb2rgb_passthrough;      ///< 1 if source and dest primaries match
    int chroma_loc;               ///< chroma sample location for downsampling
    int out_depth;                ///< output bit depth (8 or 10)
} CUDATonemapParams;

#endif /* AVFILTER_VF_TONEMAP_CUDA_H */
