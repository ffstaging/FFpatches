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

// Transpose direction constants (from transpose.h)
#define TRANSPOSE_CCLOCK_FLIP 0
#define TRANSPOSE_CLOCK       1
#define TRANSPOSE_CCLOCK      2
#define TRANSPOSE_CLOCK_FLIP  3
#define TRANSPOSE_REVERSAL    4
#define TRANSPOSE_HFLIP       5
#define TRANSPOSE_VFLIP       6

// FFmpeg passes pitch in bytes, CUDA uses potentially larger types
#define FIXED_PITCH(T) \
    (dst_pitch/sizeof(T))

#define DEFAULT_DST(n, T) \
    dst[n][yo*FIXED_PITCH(T)+xo]

// --- COORDINATE TRANSFORMATION FUNCTIONS ---

__device__ static inline void get_transpose_coords(int src_x, int src_y, int src_width, int src_height,
                                                  int *dst_x, int *dst_y, int dst_width, int dst_height, int dir)
{
    switch (dir) {
    case TRANSPOSE_CCLOCK_FLIP: // 90° CCW + vertical flip
        *dst_x = src_y;
        *dst_y = src_x;
        break;
    case TRANSPOSE_CLOCK: // 90° CW
        *dst_x = src_y;
        *dst_y = src_width - 1 - src_x;
        break;
    case TRANSPOSE_CCLOCK: // 90° CCW
        *dst_x = src_height - 1 - src_y;
        *dst_y = src_x;
        break;
    case TRANSPOSE_CLOCK_FLIP: // 90° CW + vertical flip
        *dst_x = src_height - 1 - src_y;
        *dst_y = src_width - 1 - src_x;
        break;
    case TRANSPOSE_REVERSAL: // 180° rotation
        *dst_x = src_width - 1 - src_x;
        *dst_y = src_height - 1 - src_y;
        break;
    case TRANSPOSE_HFLIP: // Horizontal flip
        *dst_x = src_width - 1 - src_x;
        *dst_y = src_y;
        break;
    case TRANSPOSE_VFLIP: // Vertical flip
        *dst_x = src_x;
        *dst_y = src_height - 1 - src_y;
        break;
    default:
        *dst_x = src_x;
        *dst_y = src_y;
        break;
    }
}

// --- TRANSPOSE KERNELS ---

#define TRANSPOSE_DEF(name, in_type, out_type) \
__device__ static inline void Transpose_##name##_impl( \
    cudaTextureObject_t src_tex[4], out_type *dst[4], \
    int xo, int yo, int width, int height, int dst_pitch, \
    int dst_width, int dst_height, int src_width, int src_height, int dir) \
{ \
    int src_x, src_y; \
    get_transpose_coords(xo, yo, width, height, &src_x, &src_y, src_width, src_height, dir); \
    \
    in_type pixel = tex2D<in_type>(src_tex[0], src_x + 0.5f, src_y + 0.5f); \
    DEFAULT_DST(0, out_type) = pixel; \
}

#define TRANSPOSE_UV_DEF(name, in_type_uv, out_type_uv) \
__device__ static inline void Transpose_##name##_uv_impl( \
    cudaTextureObject_t src_tex[4], out_type_uv *dst[4], \
    int xo, int yo, int width, int height, int dst_pitch, \
    int dst_width, int dst_height, int src_width, int src_height, int dir) \
{ \
    int src_x, src_y; \
    get_transpose_coords(xo, yo, width, height, &src_x, &src_y, src_width, src_height, dir); \
    \
    in_type_uv pixel_u = tex2D<in_type_uv>(src_tex[1], src_x + 0.5f, src_y + 0.5f); \
    in_type_uv pixel_v = tex2D<in_type_uv>(src_tex[2], src_x + 0.5f, src_y + 0.5f); \
    DEFAULT_DST(1, out_type_uv) = pixel_u; \
    DEFAULT_DST(2, out_type_uv) = pixel_v; \
}

#define TRANSPOSE_NV_UV_DEF(name, in_type_uv, out_type_uv) \
__device__ static inline void Transpose_##name##_uv_impl( \
    cudaTextureObject_t src_tex[4], out_type_uv *dst[4], \
    int xo, int yo, int width, int height, int dst_pitch, \
    int dst_width, int dst_height, int src_width, int src_height, int dir) \
{ \
    int src_x, src_y; \
    get_transpose_coords(xo, yo, width, height, &src_x, &src_y, src_width, src_height, dir); \
    \
    in_type_uv pixel_uv = tex2D<in_type_uv>(src_tex[1], src_x + 0.5f, src_y + 0.5f); \
    DEFAULT_DST(1, out_type_uv) = pixel_uv; \
}


// Define transpose implementations for all formats
TRANSPOSE_DEF(yuv420p, uchar, uchar)
TRANSPOSE_UV_DEF(yuv420p, uchar, uchar)

TRANSPOSE_DEF(nv12, uchar, uchar)
TRANSPOSE_NV_UV_DEF(nv12, uchar2, uchar2)

TRANSPOSE_DEF(yuv444p, uchar, uchar)
TRANSPOSE_UV_DEF(yuv444p, uchar, uchar)

TRANSPOSE_DEF(p010le, ushort, ushort)
TRANSPOSE_NV_UV_DEF(p010le, ushort2, ushort2)

TRANSPOSE_DEF(p016le, ushort, ushort)
TRANSPOSE_NV_UV_DEF(p016le, ushort2, ushort2)

TRANSPOSE_DEF(yuv444p16le, ushort, ushort)
TRANSPOSE_UV_DEF(yuv444p16le, ushort, ushort)

TRANSPOSE_DEF(rgb0, uchar4, uchar4)
TRANSPOSE_DEF(bgr0, uchar4, uchar4)
TRANSPOSE_DEF(rgba, uchar4, uchar4)
TRANSPOSE_DEF(bgra, uchar4, uchar4)

// --- KERNEL ARGUMENT DEFINITIONS ---

#define TRANSPOSE_KERNEL_ARGS(T) \
    cudaTextureObject_t src_tex_0, cudaTextureObject_t src_tex_1, \
    cudaTextureObject_t src_tex_2, cudaTextureObject_t src_tex_3, \
    T *dst_0, T *dst_1, T *dst_2, T *dst_3, \
    int width, int height, int dst_pitch, \
    int dst_width, int dst_height, \
    int src_width, int src_height, int dir

#define TRANSPOSE_KERNEL_IMPL(func_impl, T) \
    cudaTextureObject_t src_tex[4] = { src_tex_0, src_tex_1, src_tex_2, src_tex_3 }; \
    T *dst[4] = { dst_0, dst_1, dst_2, dst_3 }; \
    int xo = blockIdx.x * blockDim.x + threadIdx.x; \
    int yo = blockIdx.y * blockDim.y + threadIdx.y; \
    if (xo >= width || yo >= height) return; \
    \
    func_impl(src_tex, dst, xo, yo, width, height, dst_pitch, \
              dst_width, dst_height, src_width, src_height, dir);

extern "C" {

// --- TRANSPOSE KERNELS ---

#define TRANSPOSE_KERNEL(name, T) \
__global__ void Transpose_##name(TRANSPOSE_KERNEL_ARGS(T)) \
{ \
    TRANSPOSE_KERNEL_IMPL(Transpose_##name##_impl, T) \
}

#define TRANSPOSE_UV_KERNEL(name, T) \
__global__ void Transpose_##name##_uv(TRANSPOSE_KERNEL_ARGS(T)) \
{ \
    TRANSPOSE_KERNEL_IMPL(Transpose_##name##_uv_impl, T) \
}

// Transpose kernels for all formats
TRANSPOSE_KERNEL(yuv420p, uchar)
TRANSPOSE_UV_KERNEL(yuv420p, uchar)

TRANSPOSE_KERNEL(nv12, uchar)
TRANSPOSE_UV_KERNEL(nv12, uchar2)

TRANSPOSE_KERNEL(yuv444p, uchar)
TRANSPOSE_UV_KERNEL(yuv444p, uchar)

TRANSPOSE_KERNEL(p010le, ushort)
TRANSPOSE_UV_KERNEL(p010le, ushort2)

TRANSPOSE_KERNEL(p016le, ushort)
TRANSPOSE_UV_KERNEL(p016le, ushort2)

TRANSPOSE_KERNEL(yuv444p16le, ushort)
TRANSPOSE_UV_KERNEL(yuv444p16le, ushort)

TRANSPOSE_KERNEL(rgb0, uchar4)
TRANSPOSE_KERNEL(bgr0, uchar4)
TRANSPOSE_KERNEL(rgba, uchar4)
TRANSPOSE_KERNEL(bgra, uchar4)

// For RGB formats, UV kernels are not needed, but we provide empty implementations
// to maintain consistency with the function loading logic

#define EMPTY_UV_KERNEL(name, T) \
__global__ void Transpose_##name##_uv(TRANSPOSE_KERNEL_ARGS(T)) { } \

EMPTY_UV_KERNEL(rgb0, uchar)
EMPTY_UV_KERNEL(bgr0, uchar)
EMPTY_UV_KERNEL(rgba, uchar)
EMPTY_UV_KERNEL(bgra, uchar)

}
