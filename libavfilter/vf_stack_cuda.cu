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

#include "cuda/vector_helpers.cuh"

#define FIXED_PITCH(T) \
    (dst_pitch / sizeof(T))

#define OFFSET_DST(n, T) \
    dst_##n[(target_y) * FIXED_PITCH(T) + (target_x)]

#define BOUNDS_CHECK() \
    int xo = blockIdx.x * blockDim.x + threadIdx.x; \
    int yo = blockIdx.y * blockDim.y + threadIdx.y; \
    if (xo >= width || yo >= height) \
        return; \
    int target_x = xo + dst_x; \
    int target_y = yo + dst_y; \
    if (target_x < 0 || target_y < 0 || \
        target_x >= frame_width || target_y >= frame_height) \
        return;

#define COPY_SCALE() \
    float hscale = (float)src_width / (float)width; \
    float vscale = (float)src_height / (float)height; \
    float xi = (xo + 0.5f) * hscale; \
    float yi = (yo + 0.5f) * vscale;

extern "C" {

// --- COLOR KERNELS ---

__global__ void SetColor_uchar(
    uchar *dst_0, uchar *dst_1, uchar *dst_2, uchar *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int c0, int c1, int c2, int c3,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    OFFSET_DST(0, uchar) = (uchar)c0;
}

__global__ void SetColor_ushort(
    ushort *dst_0, ushort *dst_1, ushort *dst_2, ushort *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int c0, int c1, int c2, int c3,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    OFFSET_DST(0, ushort) = (ushort)c0;
}

__global__ void SetColor_uchar4(
    uchar4 *dst_0, uchar4 *dst_1, uchar4 *dst_2, uchar4 *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int c0, int c1, int c2, int c3,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    OFFSET_DST(0, uchar4) = make_uchar4(c0, c1, c2, c3);
}

__global__ void SetColor_uchar_uv(
    uchar *dst_0, uchar *dst_1, uchar *dst_2, uchar *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int c0, int c1, int c2, int c3,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    OFFSET_DST(1, uchar) = (uchar)c1;
    OFFSET_DST(2, uchar) = (uchar)c2;
}

__global__ void SetColor_ushort_uv(
    ushort *dst_0, ushort *dst_1, ushort *dst_2, ushort *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int c0, int c1, int c2, int c3,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    OFFSET_DST(1, ushort) = (ushort)c1;
    OFFSET_DST(2, ushort) = (ushort)c2;
}

__global__ void SetColor_uchar2(
    uchar2 *dst_0, uchar2 *dst_1, uchar2 *dst_2, uchar2 *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int c0, int c1, int c2, int c3,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    OFFSET_DST(1, uchar2) = make_uchar2(c1, c2);
}

__global__ void SetColor_ushort2(
    ushort2 *dst_0, ushort2 *dst_1, ushort2 *dst_2, ushort2 *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int c0, int c1, int c2, int c3,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    OFFSET_DST(1, ushort2) = make_ushort2(c1, c2);
}

// --- COPY KERNELS ---

__global__ void StackCopy_uchar(
    cudaTextureObject_t src_tex_0, cudaTextureObject_t src_tex_1,
    cudaTextureObject_t src_tex_2, cudaTextureObject_t src_tex_3,
    uchar *dst_0, uchar *dst_1, uchar *dst_2, uchar *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int src_width, int src_height,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    COPY_SCALE();
    OFFSET_DST(0, uchar) = tex2D<uchar>(src_tex_0, xi, yi);
}

__global__ void StackCopy_ushort(
    cudaTextureObject_t src_tex_0, cudaTextureObject_t src_tex_1,
    cudaTextureObject_t src_tex_2, cudaTextureObject_t src_tex_3,
    ushort *dst_0, ushort *dst_1, ushort *dst_2, ushort *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int src_width, int src_height,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    COPY_SCALE();
    OFFSET_DST(0, ushort) = tex2D<ushort>(src_tex_0, xi, yi);
}

__global__ void StackCopy_uchar4(
    cudaTextureObject_t src_tex_0, cudaTextureObject_t src_tex_1,
    cudaTextureObject_t src_tex_2, cudaTextureObject_t src_tex_3,
    uchar4 *dst_0, uchar4 *dst_1, uchar4 *dst_2, uchar4 *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int src_width, int src_height,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    COPY_SCALE();
    OFFSET_DST(0, uchar4) = tex2D<uchar4>(src_tex_0, xi, yi);
}

__global__ void StackCopy_uchar_uv(
    cudaTextureObject_t src_tex_0, cudaTextureObject_t src_tex_1,
    cudaTextureObject_t src_tex_2, cudaTextureObject_t src_tex_3,
    uchar *dst_0, uchar *dst_1, uchar *dst_2, uchar *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int src_width, int src_height,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    COPY_SCALE();
    OFFSET_DST(1, uchar) = tex2D<uchar>(src_tex_1, xi, yi);
    OFFSET_DST(2, uchar) = tex2D<uchar>(src_tex_2, xi, yi);
}

__global__ void StackCopy_ushort_uv(
    cudaTextureObject_t src_tex_0, cudaTextureObject_t src_tex_1,
    cudaTextureObject_t src_tex_2, cudaTextureObject_t src_tex_3,
    ushort *dst_0, ushort *dst_1, ushort *dst_2, ushort *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int src_width, int src_height,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    COPY_SCALE();
    OFFSET_DST(1, ushort) = tex2D<ushort>(src_tex_1, xi, yi);
    OFFSET_DST(2, ushort) = tex2D<ushort>(src_tex_2, xi, yi);
}

__global__ void StackCopy_uchar2(
    cudaTextureObject_t src_tex_0, cudaTextureObject_t src_tex_1,
    cudaTextureObject_t src_tex_2, cudaTextureObject_t src_tex_3,
    uchar2 *dst_0, uchar2 *dst_1, uchar2 *dst_2, uchar2 *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int src_width, int src_height,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    COPY_SCALE();
    OFFSET_DST(1, uchar2) = tex2D<uchar2>(src_tex_1, xi, yi);
}

__global__ void StackCopy_ushort2(
    cudaTextureObject_t src_tex_0, cudaTextureObject_t src_tex_1,
    cudaTextureObject_t src_tex_2, cudaTextureObject_t src_tex_3,
    ushort2 *dst_0, ushort2 *dst_1, ushort2 *dst_2, ushort2 *dst_3,
    int width, int height, int dst_pitch,
    int dst_x, int dst_y,
    int src_width, int src_height,
    int frame_width, int frame_height)
{
    BOUNDS_CHECK();
    COPY_SCALE();
    OFFSET_DST(1, ushort2) = tex2D<ushort2>(src_tex_1, xi, yi);
}

}
