/*
 * Copyright (c) 2024 Institute of Software Chinese Academy of Sciences (ISCAS).
 * Copyright (C) 2026 Alibaba Group Holding Limited.
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

#ifndef AVCODEC_RISCV_H26X_H2656DSP_H
#define AVCODEC_RISCV_H26X_H2656DSP_H

void ff_h2656_put_pixels_8_rvv_256(int16_t *dst, const uint8_t *src, ptrdiff_t srcstride, int height, intptr_t mx, intptr_t my, int width);
void ff_h2656_put_pixels_8_rvv_128(int16_t *dst, const uint8_t *src, ptrdiff_t srcstride, int height, intptr_t mx, intptr_t my, int width);

void ff_hevc_put_qpel_h_8_m1_rvv(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height,
        intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_uni_h_8_m1_rvv(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
        ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_uni_w_h_8_m1_rvv(uint8_t *_dst,  ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_bi_h_8_m1_rvv(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
        ptrdiff_t _srcstride, const int16_t *src2, int height, intptr_t
        mx, intptr_t my, int width);
void ff_hevc_put_qpel_v_8_m1_rvv(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height,
        intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_uni_v_8_m1_rvv(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
        ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_uni_w_v_8_m1_rvv(uint8_t *_dst,  ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_bi_v_8_m1_rvv(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
        ptrdiff_t _srcstride, const int16_t *src2, int height, intptr_t
        mx, intptr_t my, int width);
void ff_hevc_put_qpel_hv_8_m1_rvv(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height,
        intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_uni_hv_8_m1_rvv(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
        ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_uni_w_hv_8_m1_rvv(uint8_t *_dst,  ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_bi_hv_8_m1_rvv(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
        ptrdiff_t _srcstride, const int16_t *src2, int height, intptr_t
        mx, intptr_t my, int width);

void ff_hevc_put_epel_h_8_m1_rvv(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height,
        intptr_t mx, intptr_t my, int width);
void ff_hevc_put_epel_uni_h_8_m1_rvv(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
        ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_epel_uni_w_h_8_m1_rvv(uint8_t *_dst,  ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width);
void ff_hevc_put_epel_bi_h_8_m1_rvv(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
        ptrdiff_t _srcstride, const int16_t *src2, int height, intptr_t
        mx, intptr_t my, int width);
void ff_hevc_put_epel_v_8_m1_rvv(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride, int height,
        intptr_t mx, intptr_t my, int width);
void ff_hevc_put_epel_uni_v_8_m1_rvv(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
        ptrdiff_t _srcstride, int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_epel_uni_w_v_8_m1_rvv(uint8_t *_dst,  ptrdiff_t _dststride,
        const uint8_t *_src, ptrdiff_t _srcstride,
        int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width);
void ff_hevc_put_epel_bi_v_8_m1_rvv(uint8_t *_dst, ptrdiff_t _dststride, const uint8_t *_src,
        ptrdiff_t _srcstride, const int16_t *src2, int height, intptr_t
        mx, intptr_t my, int width);
#endif
