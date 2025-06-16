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


extern "C" {

__global__ void pad_planar_cuda(unsigned char* dst, int dst_pitch, int dst_w, int dst_h,
                                const unsigned char* src, int src_pitch, int src_w, int src_h,
                                int roi_x, int roi_y, unsigned char fill_val)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= dst_w || y >= dst_h) {
        return;
    }

    if (x >= roi_x && x < (roi_x + src_w) && y >= roi_y && y < (roi_y + src_h)) {
        const int src_x = x - roi_x;
        const int src_y = y - roi_y;
        dst[y * dst_pitch + x] = src[src_y * src_pitch + src_x];
    } else {
        dst[y * dst_pitch + x] = fill_val;
    }
}

__global__ void pad_uv_cuda(uchar2* dst, int dst_pitch, int dst_w, int dst_h,
                            const uchar2* src, int src_pitch, int src_w, int src_h,
                            int roi_x, int roi_y, uchar2 fill_val)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= dst_w || y >= dst_h) {
        return;
    }

    const int dst_pitch_uchar2 = dst_pitch / sizeof(uchar2);
    const int src_pitch_uchar2 = src_pitch / sizeof(uchar2);

    if (x >= roi_x && x < (roi_x + src_w) && y >= roi_y && y < (roi_y + src_h)) {
        const int src_x = x - roi_x;
        const int src_y = y - roi_y;
        dst[y * dst_pitch_uchar2 + x] = src[src_y * src_pitch_uchar2 + src_x];
    } else {
        dst[y * dst_pitch_uchar2 + x] = fill_val;
    }
}

}