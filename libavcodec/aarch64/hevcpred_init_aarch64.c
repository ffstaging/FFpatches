/*
 * HEVC Intra Prediction NEON initialization
 *
 * Copyright (c) 2026 FFmpeg Project
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

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/hevc/pred.h"

// DC prediction
void ff_hevc_pred_dc_4x4_8_neon(uint8_t *src, const uint8_t *top,
                               const uint8_t *left, ptrdiff_t stride,
                               int c_idx);
void ff_hevc_pred_dc_8x8_8_neon(uint8_t *src, const uint8_t *top,
                               const uint8_t *left, ptrdiff_t stride,
                               int c_idx);
void ff_hevc_pred_dc_16x16_8_neon(uint8_t *src, const uint8_t *top,
                                const uint8_t *left, ptrdiff_t stride,
                                int c_idx);
void ff_hevc_pred_dc_32x32_8_neon(uint8_t *src, const uint8_t *top,
                                const uint8_t *left, ptrdiff_t stride,
                                int c_idx);

// Planar prediction
void ff_hevc_pred_planar_4x4_8_neon(uint8_t *src, const uint8_t *top,
                                   const uint8_t *left, ptrdiff_t stride);
void ff_hevc_pred_planar_8x8_8_neon(uint8_t *src, const uint8_t *top,
                                   const uint8_t *left, ptrdiff_t stride);
void ff_hevc_pred_planar_16x16_8_neon(uint8_t *src, const uint8_t *top,
                                    const uint8_t *left, ptrdiff_t stride);
void ff_hevc_pred_planar_32x32_8_neon(uint8_t *src, const uint8_t *top,
                                    const uint8_t *left, ptrdiff_t stride);

// Mode 10 and 26
void ff_hevc_pred_angular_mode_10_8_neon(uint8_t *src, const uint8_t *top,
                                        const uint8_t *left, ptrdiff_t stride,
                                        int c_idx, int log2_size);
void ff_hevc_pred_angular_mode_26_8_neon(uint8_t *src, const uint8_t *top,
                                        const uint8_t *left, ptrdiff_t stride,
                                        int c_idx, int log2_size);

// Mode 18 (diagonal, angle=-32)
void ff_hevc_pred_angular_mode_18_4x4_8_neon(uint8_t *src, const uint8_t *top,
                                            const uint8_t *left, ptrdiff_t stride,
                                            int c_idx, int log2_size);
void ff_hevc_pred_angular_mode_18_8x8_8_neon(uint8_t *src, const uint8_t *top,
                                            const uint8_t *left, ptrdiff_t stride,
                                            int c_idx, int log2_size);
void ff_hevc_pred_angular_mode_18_16x16_8_neon(uint8_t *src, const uint8_t *top,
                                              const uint8_t *left, ptrdiff_t stride,
                                              int c_idx, int log2_size);
void ff_hevc_pred_angular_mode_18_32x32_8_neon(uint8_t *src, const uint8_t *top,
                                              const uint8_t *left, ptrdiff_t stride,
                                              int c_idx, int log2_size);

// Positive angle vertical modes (mode 27-34)
void ff_hevc_pred_angular_v_pos_4x4_8_neon(uint8_t *src, const uint8_t *top,
                                          const uint8_t *left, ptrdiff_t stride,
                                          int c_idx, int mode);
void ff_hevc_pred_angular_v_pos_8x8_8_neon(uint8_t *src, const uint8_t *top,
                                          const uint8_t *left, ptrdiff_t stride,
                                          int c_idx, int mode);
void ff_hevc_pred_angular_v_pos_16x16_8_neon(uint8_t *src, const uint8_t *top,
                                            const uint8_t *left, ptrdiff_t stride,
                                            int c_idx, int mode);
void ff_hevc_pred_angular_v_pos_32x32_8_neon(uint8_t *src, const uint8_t *top,
                                            const uint8_t *left, ptrdiff_t stride,
                                            int c_idx, int mode);

static void pred_dc_neon(uint8_t *src, const uint8_t *top,
                         const uint8_t *left, ptrdiff_t stride,
                         int log2_size, int c_idx)
{
    switch (log2_size) {
    case 2:
        ff_hevc_pred_dc_4x4_8_neon(src, top, left, stride, c_idx);
        break;
    case 3:
        ff_hevc_pred_dc_8x8_8_neon(src, top, left, stride, c_idx);
        break;
    case 4:
        ff_hevc_pred_dc_16x16_8_neon(src, top, left, stride, c_idx);
        break;
    case 5:
        ff_hevc_pred_dc_32x32_8_neon(src, top, left, stride, c_idx);
        break;
    default:
        av_unreachable("log2_size must be 2, 3, 4 or 5");
    }
}

typedef void (*pred_angular_func)(uint8_t *src, const uint8_t *top,
                                  const uint8_t *left, ptrdiff_t stride,
                                  int c_idx, int mode);
static pred_angular_func pred_angular_c[4];

static void pred_angular_0_neon(uint8_t *src, const uint8_t *top,
                                const uint8_t *left, ptrdiff_t stride,
                                int c_idx, int mode)
{
    if (mode == 10) {
        ff_hevc_pred_angular_mode_10_8_neon(src, top, left, stride, c_idx, 2);
    } else if (mode == 26) {
        ff_hevc_pred_angular_mode_26_8_neon(src, top, left, stride, c_idx, 2);
    } else if (mode == 18) {
        ff_hevc_pred_angular_mode_18_4x4_8_neon(src, top, left, stride, c_idx, 2);
    } else if (mode >= 27) {
        ff_hevc_pred_angular_v_pos_4x4_8_neon(src, top, left, stride, c_idx, mode);
    } else {
        pred_angular_c[0](src, top, left, stride, c_idx, mode);
    }
}

static void pred_angular_1_neon(uint8_t *src, const uint8_t *top,
                                const uint8_t *left, ptrdiff_t stride,
                                int c_idx, int mode)
{
    if (mode == 10) {
        ff_hevc_pred_angular_mode_10_8_neon(src, top, left, stride, c_idx, 3);
    } else if (mode == 26) {
        ff_hevc_pred_angular_mode_26_8_neon(src, top, left, stride, c_idx, 3);
    } else if (mode == 18) {
        ff_hevc_pred_angular_mode_18_8x8_8_neon(src, top, left, stride, c_idx, 3);
    } else if (mode >= 27) {
        ff_hevc_pred_angular_v_pos_8x8_8_neon(src, top, left, stride, c_idx, mode);
    } else {
        pred_angular_c[1](src, top, left, stride, c_idx, mode);
    }
}

static void pred_angular_2_neon(uint8_t *src, const uint8_t *top,
                                const uint8_t *left, ptrdiff_t stride,
                                int c_idx, int mode)
{
    if (mode == 10) {
        ff_hevc_pred_angular_mode_10_8_neon(src, top, left, stride, c_idx, 4);
    } else if (mode == 26) {
        ff_hevc_pred_angular_mode_26_8_neon(src, top, left, stride, c_idx, 4);
    } else if (mode == 18) {
        ff_hevc_pred_angular_mode_18_16x16_8_neon(src, top, left, stride, c_idx, 4);
    } else if (mode >= 27) {
        ff_hevc_pred_angular_v_pos_16x16_8_neon(src, top, left, stride, c_idx, mode);
    } else {
        pred_angular_c[2](src, top, left, stride, c_idx, mode);
    }
}

static void pred_angular_3_neon(uint8_t *src, const uint8_t *top,
                                const uint8_t *left, ptrdiff_t stride,
                                int c_idx, int mode)
{
    if (mode == 10) {
        ff_hevc_pred_angular_mode_10_8_neon(src, top, left, stride, c_idx, 5);
    } else if (mode == 26) {
        ff_hevc_pred_angular_mode_26_8_neon(src, top, left, stride, c_idx, 5);
    } else if (mode == 18) {
        ff_hevc_pred_angular_mode_18_32x32_8_neon(src, top, left, stride, c_idx, 5);
    } else if (mode >= 27) {
        ff_hevc_pred_angular_v_pos_32x32_8_neon(src, top, left, stride, c_idx, mode);
    } else {
        pred_angular_c[3](src, top, left, stride, c_idx, mode);
    }
}

av_cold void ff_hevc_pred_init_aarch64(HEVCPredContext *hpc, int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (!have_neon(cpu_flags))
        return;

    if (bit_depth == 8) {
        hpc->pred_dc        = pred_dc_neon;
        hpc->pred_planar[0] = ff_hevc_pred_planar_4x4_8_neon;
        hpc->pred_planar[1] = ff_hevc_pred_planar_8x8_8_neon;
        hpc->pred_planar[2] = ff_hevc_pred_planar_16x16_8_neon;
        hpc->pred_planar[3] = ff_hevc_pred_planar_32x32_8_neon;

        pred_angular_c[0] = hpc->pred_angular[0];
        pred_angular_c[1] = hpc->pred_angular[1];
        pred_angular_c[2] = hpc->pred_angular[2];
        pred_angular_c[3] = hpc->pred_angular[3];

        hpc->pred_angular[0] = pred_angular_0_neon;
        hpc->pred_angular[1] = pred_angular_1_neon;
        hpc->pred_angular[2] = pred_angular_2_neon;
        hpc->pred_angular[3] = pred_angular_3_neon;
    }
}
