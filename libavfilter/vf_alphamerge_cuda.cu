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


template <typename T>
__device__ void alphamerge_impl(T *dst, int dst_pitch,
                                const T *src, int src_pitch,
                                int width, int height)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height) {
        dst[y * dst_pitch + x] = src[y * src_pitch + x];
    }
}

extern "C" {
    __global__ void alphamerge_planar(unsigned char* main_alpha_plane,
                                      int main_alpha_linesize,
                                      const unsigned char* alpha_mask_luma_plane,
                                      int alpha_mask_luma_linesize,
                                      int width, int height)
    {
        alphamerge_impl<unsigned char>(main_alpha_plane, main_alpha_linesize,
                                       alpha_mask_luma_plane, alpha_mask_luma_linesize,
                                       width, height);
    }
}