;*****************************************************************************
;* x86-optimized functions for nlmeans filter
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

%if HAVE_AVX2_EXTERNAL && ARCH_X86_64

SECTION_RODATA 32

ending_lut: dd -1, -1, -1, -1, -1, -1, -1, -1,\
                0, -1, -1, -1, -1, -1, -1, -1,\
                0,  0, -1, -1, -1, -1, -1, -1,\
                0,  0,  0, -1, -1, -1, -1, -1,\
                0,  0,  0,  0, -1, -1, -1, -1,\
                0,  0,  0,  0,  0, -1, -1, -1,\
                0,  0,  0,  0,  0,  0, -1, -1,\
                0,  0,  0,  0,  0,  0,  0, -1,\
                0,  0,  0,  0,  0,  0,  0,  0

SECTION .text

%macro PROCESS_8_SSD_INTEGRAL 0
    pmovzxbd      m0, [s1q + xq]
    pmovzxbd      m1, [s2q + xq]
    psubd         m0, m1
    pmulld        m0, m0

    movu          m1, [dst_topq + xq*4]
    movu          m2, [dst_topq + xq*4 - 4]
    psubd         m1, m2
    paddd         m0, m1

    mova          m5, m0
    pslldq        m5, 4
    paddd         m0, m5
    mova          m5, m0
    pslldq        m5, 8
    paddd         m0, m5
    mova          m5, m0
    pslldq        m5, 16
    paddd         m0, m5

    vextracti128 xm5, m0, 0
    pshufd      xm5, xm5, 0xff
    pxor          m4, m4
    vinserti128   m4, m4, xm5, 1
    paddd         m0, m4

    movd        xm5, carryd
    vpbroadcastd  m4, xm5
    paddd         m0, m4

    movu [dstq + xq*4], m0

    vextracti128 xm5, m0, 1
    pshufd      xm5, xm5, 0xff
    movd      carryd, xm5

    add           xq, 8
%endmacro

; void ff_compute_safe_ssd_integral_image(uint32_t *dst, ptrdiff_t dst_linesize_32,
;                                         const uint8_t *s1, ptrdiff_t linesize1,
;                                         const uint8_t *s2, ptrdiff_t linesize2,
;                                         int w, int h);
;
; Assumptions (see C version):
; - w is multiple of 16 and w >= 16
; - h >= 1
; - dst[-1] and dst_top[-1] are readable

INIT_YMM avx2
cglobal compute_safe_ssd_integral_image, 8, 14, 6, 0, dst, dst_lz, s1, ls1, s2, ls2, w, h, dst_top, dst_stride, x, carry, tmp
    mov            wd, dword wm
    mov            hd, dword hm
    movsxd         wq, wd

    mov   dst_strideq, dst_lzq
    shl   dst_strideq, 2
    mov      dst_topq, dstq
    sub      dst_topq, dst_strideq

.yloop:
    xor           xq, xq
    mov       carryd, [dstq - 4]

.xloop:
    ; ---- process 8 pixels ----
    PROCESS_8_SSD_INTEGRAL
    ; ---- process 8 pixels ----
    PROCESS_8_SSD_INTEGRAL
    cmp           xq, wq
    jl .xloop

    add          s1q, ls1q
    add          s2q, ls2q
    add         dstq, dst_strideq
    add     dst_topq, dst_strideq
    dec           hd
    jg .yloop
    RET

; void ff_compute_weights_line(const uint32_t *const iia,
;                              const uint32_t *const iib,
;                              const uint32_t *const iid,
;                              const uint32_t *const iie,
;                              const uint8_t *const src,
;                              float *total,
;                              float *sum,
;                              const float *const lut,
;                              int max,
;                              int startx, int endx);

INIT_YMM avx2
cglobal compute_weights_line, 8, 13, 5, 0, iia, iib, iid, iie, src, total, sum, lut, x, startx, endx, mod, elut
    movsxd startxq, dword startxm
    movsxd   endxq, dword endxm
    VPBROADCASTD      m2, r8m

    mov      xq, startxq
    mov    modq, mmsize / 4
    lea   elutq, [ending_lut]

    vpcmpeqd  m4, m4

    .loop:
        mov    startxq, endxq
        sub    startxq, xq
        cmp    startxq, modq
        cmovge startxq, modq
        sal    startxq, 5

        movu   m0, [iieq + xq * 4]

        psubd  m0, [iidq + xq * 4]
        psubd  m0, [iibq + xq * 4]
        paddd  m0, [iiaq + xq * 4]
        por    m0, [elutq + startxq]
        pminud m0, m2
        pslld  m0, 2
        mova   m3, m4
        vgatherdps m1, [lutq + m0], m3

        pmovzxbd m0, [srcq + xq]
        cvtdq2ps m0, m0

        mulps m0, m1

        addps m1, [totalq + xq * 4]
        addps m0, [sumq + xq * 4]

        movups [totalq + xq * 4], m1
        movups [sumq + xq * 4], m0

        add xq, mmsize / 4
        cmp xq, endxq
        jl .loop
    RET

%endif
