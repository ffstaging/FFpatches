;*****************************************************************************
;* x86-optimized functions for boxblur filter
;*
;* Copyright (c) 2025 Makar Kuznietsov
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with FFmpeg; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION .text

%if ARCH_X86_64

; void ff_boxblur_middle_avx2(uint8_t *dst, const uint8_t *src,
;                             int x_start, int x_end, int radius,
;                             int inv, int *sum_ptr)
INIT_YMM avx2
cglobal boxblur_middle, 7, 10, 6, dst, src, x_start, x_end, radius, inv, sum_ptr, x, tmp, sum
    mov      sumd, [sum_ptrq]
    movd        xm3, invd
    vpbroadcastd m3, xm3
    mov      xd, x_startd

.vloop:
    ; Load incoming pixels: src[x + radius]
    lea      tmpq, [xq + radiusq]
    movu     xm0, [srcq + tmpq]

    ; Load outgoing pixels: src[x - radius - 1]
    lea      tmpq, [xq - 1]
    sub      tmpq, radiusq
    movu     xm1, [srcq + tmpq]

    ; Zero-extend u8 -> u16
    pmovzxbw m0, xm0
    pmovzxbw m1, xm1

    ; Compute signed difference
    psubw      m2, m0, m1
    pmovsxwd m4, xm2

    ; Extract high 8 words and sign-extend
    vextracti128 xm0, m2, 1
    pmovsxwd m5, xm0

    ; Multiply by inv
    pmulld     m4, m4, m3
    pmulld     m5, m5, m3

    ; Compute prefix sum for m4 (lower 8 pixels)
    mova           m0, m4
    pslldq        m1, m0, 4
    paddd         m0, m0, m1
    pslldq        m1, m0, 8
    paddd         m0, m0, m1

    ; Propagate carry across 128-bit lanes
    vextracti128   xm1, m0, 0
    vpshufd        xm1, xm1, 0xFF
    vpxor          m2, m2, m2
    vinserti128    m2, m2, xm1, 1
    vpaddd         m0, m0, m2

    ; Add accumulator
    movd           xm2, sumd
    vpbroadcastd   m2, xm2
    paddd         m0, m0, m2
    mova           m4, m0

    ; Update accumulator for next iteration
    vextracti128   xm1, m0, 1
    pshufd        xm1, xm1, 0xFF
    movd           sumd, xm1

    ; Compute prefix sum for m5 (upper 8 pixels)
    mova           m0, m5
    pslldq        m1, m0, 4
    paddd         m0, m0, m1
    pslldq        m1, m0, 8
    paddd         m0, m0, m1

    ; Propagate carry across 128-bit lanes
    vextracti128   xm1, m0, 0
    pshufd        xm1, xm1, 0xFF
    pxor          m2, m2, m2
    vinserti128    m2, m2, xm1, 1
    paddd         m0, m0, m2

    ; Add accumulator
    movd           xm2, sumd
    vpbroadcastd   m2, xm2
    paddd         m0, m0, m2
    mova           m5, m0

    ; Update accumulator for next iteration
    vextracti128   xm1, m0, 1
    pshufd        xm1, xm1, 0xFF
    movd           sumd, xm1

    ; Shift and pack results
    psrad      m4, m4, 16
    psrad      m5, m5, 16

    ; Pack lower 8 pixels
    vextracti128 xm0, m4, 0
    vextracti128 xm1, m4, 1
    packusdw    xm0, xm0, xm1
    packuswb    xm0, xm0, xm0
    movq          [dstq + xq + 0], xm0

    ; Pack upper 8 pixels
    vextracti128 xm0, m5, 0
    vextracti128 xm1, m5, 1
    packusdw    xm0, xm0, xm1
    packuswb    xm0, xm0, xm0
    movq          [dstq + xq + 8], xm0

    add      xd, 16
    cmp      xd, x_endd
    jl       .vloop

    mov      [sum_ptrq], sumd
    RET

; void ff_boxblur_middle16_avx2(uint16_t *dst, const uint16_t *src,
;                               int x_start, int x_end, int radius,
;                               int inv, int *sum_ptr)
INIT_YMM avx2
cglobal boxblur_middle16, 7, 10, 5, dst, src, x_start, x_end, radius, inv, sum_ptr, x, tmp, sum
    mov      sumd, [sum_ptrq]
    movd        xm3, invd
    vpbroadcastd m3, xm3
    mov      xd, x_startd

.vloop:
    ; Load incoming pixels: src[x + radius] (accounting for 2-byte stride)
    lea      tmpq, [xq + radiusq]
    movu     xm0, [srcq + tmpq*2]

    ; Load outgoing pixels: src[x - radius - 1]
    lea      tmpq, [xq - 1]
    sub      tmpq, radiusq
    movu     xm1, [srcq + tmpq*2]

    ; Zero-extend u16 -> u32
    pmovzxwd m0, xm0
    pmovzxwd m1, xm1

    ; Compute signed difference
    psubd      m2, m0, m1

    ; Multiply by inv
    pmulld     m4, m2, m3

    ; Compute prefix sum
    mova           m0, m4
    pslldq        m1, m0, 4
    paddd         m0, m0, m1
    pslldq        m1, m0, 8
    paddd         m0, m0, m1

    ; Propagate carry across 128-bit lanes
    vextracti128   xm1, m0, 0
    pshufd        xm1, xm1, 0xFF
    pxor          m2, m2, m2
    vinserti128    m2, m2, xm1, 1
    paddd         m0, m0, m2

    ; Add accumulator
    movd           xm2, sumd
    vpbroadcastd   m2, xm2
    paddd         m0, m0, m2
    mova           m4, m0

    ; Update accumulator for next iteration
    vextracti128   xm1, m0, 1
    pshufd        xm1, xm1, 0xFF
    movd           sumd, xm1

    ; Shift and pack results
    psrld      m4, m4, 16
    vextracti128 xm0, m4, 0
    vextracti128 xm1, m4, 1
    packusdw    xm0, xm0, xm1
    movu          [dstq + xq*2], xm0

    add      xd, 8
    cmp      xd, x_endd
    jl       .vloop

    mov      [sum_ptrq], sumd
    RET

%endif

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
%endif
