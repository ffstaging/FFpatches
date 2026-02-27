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

#include "cuda/vector_helpers.cuh"
#include "transpose.h"

#define FIXED_PITCH(T) \
    (dst_pitch / sizeof(T))

__device__ static inline void get_transpose_coords(int src_x, int src_y,
                                                   int src_width, int src_height,
                                                   int *dst_x, int *dst_y,
                                                   int dst_width, int dst_height,
                                                   int dir)
{
    switch (dir) {
    case TRANSPOSE_CCLOCK_FLIP:
        *dst_x = src_y;
        *dst_y = src_x;
        break;
    case TRANSPOSE_CLOCK:
        *dst_x = src_y;
        *dst_y = src_width - 1 - src_x;
        break;
    case TRANSPOSE_CCLOCK:
        *dst_x = src_height - 1 - src_y;
        *dst_y = src_x;
        break;
    case TRANSPOSE_CLOCK_FLIP:
        *dst_x = src_height - 1 - src_y;
        *dst_y = src_width - 1 - src_x;
        break;
    case TRANSPOSE_REVERSAL:
        *dst_x = src_width - 1 - src_x;
        *dst_y = src_height - 1 - src_y;
        break;
    case TRANSPOSE_HFLIP:
        *dst_x = src_width - 1 - src_x;
        *dst_y = src_y;
        break;
    case TRANSPOSE_VFLIP:
        *dst_x = src_x;
        *dst_y = src_height - 1 - src_y;
        break;
    default:
        *dst_x = src_x;
        *dst_y = src_y;
        break;
    }
}

#define TRANSPOSE_KERNEL_ARGS(T) \
    cudaTextureObject_t src_tex_0, cudaTextureObject_t src_tex_1, \
    cudaTextureObject_t src_tex_2, cudaTextureObject_t src_tex_3, \
    T *dst_0, T *dst_1, T *dst_2, T *dst_3, \
    int width, int height, int dst_pitch, \
    int dst_width, int dst_height, \
    int src_width, int src_height, int dir

#define KERNEL_PREAMBLE(T) \
    int xo = blockIdx.x * blockDim.x + threadIdx.x; \
    int yo = blockIdx.y * blockDim.y + threadIdx.y; \
    if (xo >= width || yo >= height) return; \
    int src_x, src_y; \
    get_transpose_coords(xo, yo, width, height, \
                         &src_x, &src_y, src_width, src_height, dir);

extern "C" {

__global__ void Transpose_uchar(TRANSPOSE_KERNEL_ARGS(uchar))
{
    KERNEL_PREAMBLE(uchar)
    dst_0[yo * FIXED_PITCH(uchar) + xo] =
        tex2D<uchar>(src_tex_0, src_x + 0.5f, src_y + 0.5f);
}

__global__ void Transpose_ushort(TRANSPOSE_KERNEL_ARGS(ushort))
{
    KERNEL_PREAMBLE(ushort)
    dst_0[yo * FIXED_PITCH(ushort) + xo] =
        tex2D<ushort>(src_tex_0, src_x + 0.5f, src_y + 0.5f);
}

__global__ void Transpose_uchar4(TRANSPOSE_KERNEL_ARGS(uchar4))
{
    KERNEL_PREAMBLE(uchar4)
    dst_0[yo * FIXED_PITCH(uchar4) + xo] =
        tex2D<uchar4>(src_tex_0, src_x + 0.5f, src_y + 0.5f);
}

__global__ void Transpose_uchar_uv(TRANSPOSE_KERNEL_ARGS(uchar))
{
    KERNEL_PREAMBLE(uchar)
    int pitch = FIXED_PITCH(uchar);
    dst_1[yo * pitch + xo] = tex2D<uchar>(src_tex_1, src_x + 0.5f, src_y + 0.5f);
    dst_2[yo * pitch + xo] = tex2D<uchar>(src_tex_2, src_x + 0.5f, src_y + 0.5f);
}

__global__ void Transpose_ushort_uv(TRANSPOSE_KERNEL_ARGS(ushort))
{
    KERNEL_PREAMBLE(ushort)
    int pitch = FIXED_PITCH(ushort);
    dst_1[yo * pitch + xo] = tex2D<ushort>(src_tex_1, src_x + 0.5f, src_y + 0.5f);
    dst_2[yo * pitch + xo] = tex2D<ushort>(src_tex_2, src_x + 0.5f, src_y + 0.5f);
}

__global__ void Transpose_uchar2(TRANSPOSE_KERNEL_ARGS(uchar2))
{
    KERNEL_PREAMBLE(uchar2)
    dst_1[yo * FIXED_PITCH(uchar2) + xo] =
        tex2D<uchar2>(src_tex_1, src_x + 0.5f, src_y + 0.5f);
}

__global__ void Transpose_ushort2(TRANSPOSE_KERNEL_ARGS(ushort2))
{
    KERNEL_PREAMBLE(ushort2)
    dst_1[yo * FIXED_PITCH(ushort2) + xo] =
        tex2D<ushort2>(src_tex_1, src_x + 0.5f, src_y + 0.5f);
}

}
