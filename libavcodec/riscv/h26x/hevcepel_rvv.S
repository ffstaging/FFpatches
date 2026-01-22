 /*
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
.data
.align 2
qpel_filters:
    .byte  0,  0,  0,  0
    .byte -2, 58, 10, -2
    .byte -4, 54, 16, -2
    .byte -6, 46, 28, -4
    .byte -4, 36, 36, -4
    .byte -4, 28, 46, -6
    .byte -2, 16, 54, -4
    .byte -2, 10, 58, -2

.text
#include "libavutil/riscv/asm.S"
#define HEVC_MAX_PB_SIZE 64

.macro  lx rd, addr
#if (__riscv_xlen == 32)
        lw      \rd, \addr
#elif (__riscv_xlen == 64)
        ld      \rd, \addr
#else
        lq      \rd, \addr
#endif
.endm

.macro  sx rd, addr
#if (__riscv_xlen == 32)
        sw      \rd, \addr
#elif (__riscv_xlen == 64)
        sd      \rd, \addr
#else
        sq      \rd, \addr
#endif
.endm

/* clobbers t0, t1 */
.macro load_filter m
        la          t0, qpel_filters
        slli        t1, \m, 2
        add         t0, t0, t1
        lb          s1, 0(t0)
        lb          s2, 1(t0)
        lb          s3, 2(t0)
        lb          s4, 3(t0)
.endm

/* output is unclipped; clobbers t4 */
.macro filter_h         vdst, vsrc0, vsrc1, vsrc2, vsrc3, src
        addi             t4, \src, -1
        vle8.v           \vsrc0, (t4)
        vmv.v.x          \vsrc3, s1
        vwmulsu.vv       \vdst, \vsrc3, \vsrc0
        vle8.v           \vsrc1, (\src)
        addi             t4, \src, 1
        vle8.v           \vsrc2, (t4)
        addi             t4, \src, 2
        vle8.v           \vsrc3, (t4)

        vwmaccsu.vx      \vdst, s2, \vsrc1
        vwmaccsu.vx      \vdst, s3, \vsrc2
        vwmaccsu.vx      \vdst, s4, \vsrc3
.endm

.macro vreg

.endm

.macro hevc_epel_h       lmul, lmul2, lmul4
func ff_hevc_put_epel_h_8_\lmul\()_rvv, zve32x
    addi        sp, sp, -32
    sx          s1, 0(sp)
    sx          s2, 8(sp)
    sx          s3, 16(sp)
    sx          s4, 24(sp)
    load_filter a4
    mv          t3, a6
    li          t1, 0       # offset

1:
    vsetvli     t6, t3, e8, \lmul, ta, ma
    add         t2, a1, t1
    filter_h    v0, v16, v18, v20, v22, t2
    vsetvli     zero, zero, e16, \lmul2, ta, ma
    slli        t2, t1, 1
    add         t2, a0, t2
    vse16.v     v0, (t2)
    sub         t3, t3, t6
    add         t1, t1, t6
    bgt         t3, zero, 1b
    addi        a3, a3, -1
    mv          t3, a6
    add         a1, a1, a2
    addi        a0, a0, 2*HEVC_MAX_PB_SIZE
    li          t1, 0
    bgt         a3, zero, 1b

    lx          s1, 0(sp)
    lx          s2, 8(sp)
    lx          s3, 16(sp)
    lx          s4, 24(sp)
    addi        sp, sp, 32
    ret
endfunc

func ff_hevc_put_epel_uni_h_8_\lmul\()_rvv, zve32x
    csrwi       vxrm, 0
    addi        sp, sp, -32
    sx          s1, 0(sp)
    sx          s2, 8(sp)
    sx          s3, 16(sp)
    sx          s4, 24(sp)
    load_filter a5
    mv          t3, a7
    li          t1, 0       # offset

1:
    vsetvli     t6, t3, e8, \lmul, ta, ma
    add         t2, a2, t1
    filter_h    v0, v16, v18, v20, v22, t2
    vsetvli     zero, zero, e16, \lmul2, ta, ma
    vmax.vx     v0, v0, zero
    vsetvli     zero, zero, e8, \lmul, ta, ma
    vnclipu.wi  v0, v0, 6
    add         t2, a0, t1
    vse8.v      v0, (t2)
    sub         t3, t3, t6
    add         t1, t1, t6
    bgt         t3, zero, 1b
    addi        a4, a4, -1
    mv          t3, a7
    add         a2, a2, a3
    add         a0, a0, a1
    li          t1, 0
    bgt         a4, zero, 1b

    lx          s1, 0(sp)
    lx          s2, 8(sp)
    lx          s3, 16(sp)
    lx          s4, 24(sp)
    addi        sp, sp, 32
    ret
endfunc

func ff_hevc_put_epel_uni_w_h_8_\lmul\()_rvv, zve32x
    csrwi       vxrm, 0
    lx          t2, 0(sp)       # mx
    addi        a5, a5, 6       # shift
#if (__riscv_xlen == 32)
    lw          t3, 8(sp)       # width
#elif (__riscv_xlen == 64)
    lw          t3, 16(sp)
#endif
    addi        sp, sp, -32
    sx          s1, 0(sp)
    sx          s2, 8(sp)
    sx          s3, 16(sp)
    sx          s4, 24(sp)
    load_filter t2
    li          t2, 0           # offset

1:
    vsetvli     t6, t3, e8, \lmul, ta, ma
    add         t1, a2, t2
    filter_h    v8, v16, v18, v20, v22, t1
    vsetvli     zero, zero, e16, \lmul2, ta, ma
    vwmul.vx    v0, v8, a6
    vsetvli     zero, zero, e32, \lmul4, ta, ma
    vssra.vx    v0, v0, a5
    vsadd.vx    v0, v0, a7
    vmax.vx     v0, v0, zero
    vsetvli     zero, zero, e16, \lmul2, ta, ma
    vnclip.wi   v0, v0, 0
    vsetvli     zero, zero, e8, \lmul, ta, ma
    vnclipu.wi  v0, v0, 0
    add         t1, a0, t2
    vse8.v      v0, (t1)
    sub         t3, t3, t6
    add         t2, t2, t6
    bgt         t3, zero, 1b
    addi        a4, a4, -1
#if (__riscv_xlen == 32)
    lw          t3, 40(sp)
#elif (__riscv_xlen == 64)
    ld          t3, 48(sp)
#endif
    add         a2, a2, a3
    add         a0, a0, a1
    li          t2, 0
    bgt         a4, zero, 1b

    lx          s1, 0(sp)
    lx          s2, 8(sp)
    lx          s3, 16(sp)
    lx          s4, 24(sp)
    addi        sp, sp, 32
    ret
endfunc

func ff_hevc_put_epel_bi_h_8_\lmul\()_rvv, zve32x
    csrwi       vxrm, 0
    lw          t3, 0(sp)      # width
    addi        sp, sp, -32
    sx          s1, 0(sp)
    sx          s2, 8(sp)
    sx          s3, 16(sp)
    sx          s4, 24(sp)
    load_filter a6
    li          t1, 0          # offset

1:
    vsetvli     t6, t3, e16, \lmul2, ta, ma
    slli        t2, t1, 1
    add         t2, a4, t2
    vle16.v     v12, (t2)
    vsetvli     zero, zero, e8, \lmul, ta, ma
    add         t2, a2, t1
    filter_h    v0, v16, v18, v20, v22, t2
    vsetvli     zero, zero, e16, \lmul2, ta, ma
    vsadd.vv    v0, v0, v12
    vmax.vx     v0, v0, zero
    vsetvli     zero, zero, e8, \lmul, ta, ma
    vnclipu.wi  v0, v0, 7
    add         t2, a0, t1
    vse8.v      v0, (t2)
    sub         t3, t3, t6
    add         t1, t1, t6
    bgt         t3, zero, 1b
    addi        a5, a5, -1
    lw          t3, 32(sp)
    add         a2, a2, a3
    add         a0, a0, a1
    addi        a4, a4, 2*HEVC_MAX_PB_SIZE
    li          t1, 0
    bgt         a5, zero, 1b

    lx          s1, 0(sp)
    lx          s2, 8(sp)
    lx          s3, 16(sp)
    lx          s4, 24(sp)
    addi        sp, sp, 32
    ret
endfunc
.endm

hevc_epel_h m1, m2, m4

/* output is unclipped; clobbers v4 */
.macro filter_v         vdst, vsrc0, vsrc1, vsrc2, vsrc3
        vmv.v.x          v4, s1
        vwmulsu.vv       \vdst, v4, \vsrc0
        vwmaccsu.vx      \vdst, s2, \vsrc1
        vmv.v.v          \vsrc0, \vsrc1
        vwmaccsu.vx      \vdst, s3, \vsrc2
        vmv.v.v          \vsrc1, \vsrc2
        vwmaccsu.vx      \vdst, s4, \vsrc3
        vmv.v.v          \vsrc2, \vsrc3
.endm

.macro hevc_epel_v       lmul, lmul2, lmul4
func ff_hevc_put_epel_v_8_\lmul\()_rvv, zve32x
    addi        sp, sp, -32
    sx          s1, 0(sp)
    sx          s2, 8(sp)
    sx          s3, 16(sp)
    sx          s4, 24(sp)
    load_filter a5
    sub         a1, a1, a2      # src - src_stride
    li          t1, 0           # offset   
    mv          t4, a3 

1:
    add         t2, a1, t1
    slli        t3, t1, 1
    add         t3, a0, t3

    vsetvli     t5, a6, e8, \lmul, ta, ma
    vle8.V      v16, (t2)
    add         t2, t2, a2
    vle8.V      v18, (t2)
    add         t2, t2, a2
    vle8.V      v20, (t2)
    add         t2, t2, a2

2:
    vsetvli     zero, zero, e8, \lmul, ta, ma
    vle8.V      v22, (t2)
    add         t2, t2, a2
    filter_v    v0, v16, v18, v20, v22
    vsetvli     zero, zero, e16, \lmul2, ta, ma
    vse16.v     v0, (t3)
    add         t3, t3, 2*HEVC_MAX_PB_SIZE
    addi        a3, a3, -1
    bgt         a3, zero, 2b    
    add         t1, t1, t5
    sub         a6, a6, t5
    mv          a3, t4
    bgt         a6, zero, 1b

    lx          s1, 0(sp)
    lx          s2, 8(sp)
    lx          s3, 16(sp)
    lx          s4, 24(sp)
    addi        sp, sp, 32
    ret
endfunc

func ff_hevc_put_epel_uni_v_8_\lmul\()_rvv, zve32x
    csrwi       vxrm, 0 
    addi        sp, sp, -32
    sx          s1, 0(sp)
    sx          s2, 8(sp)
    sx          s3, 16(sp)
    sx          s4, 24(sp)
    load_filter a6
    sub         a2, a2, a3      # src - src_stride
    li          t1, 0           # offset   
    mv          t4, a4 

1:
    add         t2, a2, t1
    add         t3, a0, t1

    vsetvli     t5, a7, e8, \lmul, ta, ma
    vle8.V      v16, (t2)
    add         t2, t2, a3
    vle8.V      v18, (t2)
    add         t2, t2, a3
    vle8.V      v20, (t2)
    add         t2, t2, a3

2:
    vsetvli     zero, zero, e8, \lmul, ta, ma
    vle8.V      v22, (t2)
    add         t2, t2, a3
    filter_v    v0, v16, v18, v20, v22
    vsetvli     zero, zero, e16, \lmul2, ta, ma
    vmax.vx     v0, v0, zero
    vsetvli     zero, zero, e8, \lmul, ta, ma
    vnclipu.wi  v0, v0, 6
    vse8.v      v0, (t3)
    add         t3, t3, a1
    addi        a4, a4, -1
    bgt         a4, zero, 2b    
    add         t1, t1, t5
    sub         a7, a7, t5
    mv          a4, t4
    bgt         a7, zero, 1b

    lx          s1, 0(sp)
    lx          s2, 8(sp)
    lx          s3, 16(sp)
    lx          s4, 24(sp)
    addi        sp, sp, 32
    ret
endfunc

func ff_hevc_put_epel_uni_w_v_8_\lmul\()_rvv, zve32x
    csrwi       vxrm, 0 
#if (__riscv_xlen == 32)
    lw          t1, 4(sp)       # my
    lw          t6, 8(sp)       # width
#elif (__riscv_xlen == 64)
    ld          t1, 8(sp)
    lw          t6, 16(sp)
#endif
    addi        sp, sp, -32
    sx          s1, 0(sp)
    sx          s2, 8(sp)
    sx          s3, 16(sp)
    sx          s4, 24(sp)
    load_filter t1
    addi        a5, a5, 6       # shift
    sub         a2, a2, a3      # src - src_stride
    li          t1, 0           # offset   
    mv          t4, a4 

1:
    add         t2, a2, t1
    add         t3, a0, t1

    vsetvli     t5, t6, e8, \lmul, ta, ma
    vle8.V      v16, (t2)
    add         t2, t2, a3
    vle8.V      v18, (t2)
    add         t2, t2, a3
    vle8.V      v20, (t2)
    add         t2, t2, a3

2:
    vsetvli     zero, zero, e8, \lmul, ta, ma
    vle8.V      v22, (t2)
    add         t2, t2, a3
    filter_v    v0, v16, v18, v20, v22
    vsetvli     zero, zero, e16, \lmul2, ta, ma
    vwmul.vx    v8, v0, a6
    vsetvli     zero, zero, e32, \lmul4, ta, ma
    vssra.vx    v0, v8, a5
    vsadd.vx    v0, v0, a7
    vmax.vx     v0, v0, zero
    vsetvli     zero, zero, e16, \lmul2, ta, ma
    vnclip.wi   v0, v0, 0
    vsetvli     zero, zero, e8, \lmul, ta, ma
    vnclipu.wi  v0, v0, 0
    vse8.v      v0, (t3)
    add         t3, t3, a1
    addi        a4, a4, -1
    bgt         a4, zero, 2b    
    add         t1, t1, t5
    sub         t6, t6, t5
    mv          a4, t4
    bgt         t6, zero, 1b

    lx          s1, 0(sp)
    lx          s2, 8(sp)
    lx          s3, 16(sp)
    lx          s4, 24(sp)
    addi        sp, sp, 32
    ret
endfunc

func ff_hevc_put_epel_bi_v_8_\lmul\()_rvv, zve32x
    csrwi       vxrm, 0 
    lw          t6, 0(sp)      # width
    addi        sp, sp, -32
    sx          s1, 0(sp)
    sx          s2, 8(sp)
    sx          s3, 16(sp)
    sx          s4, 24(sp)
    load_filter a7
    sub         a2, a2, a3      # src - src_stride
    li          t1, 0           # offset   
    mv          t4, a5 

1:
    add         t2, a2, t1
    add         t3, a0, t1
    slli        t0, t1, 1
    add         t0, a4, t0

    vsetvli     t5, t6, e8, \lmul, ta, ma
    vle8.V      v16, (t2)
    add         t2, t2, a3
    vle8.V      v18, (t2)
    add         t2, t2, a3
    vle8.V      v20, (t2)
    add         t2, t2, a3

2:
    vsetvli     zero, zero, e8, \lmul, ta, ma
    vle8.V      v22, (t2)
    add         t2, t2, a3
    filter_v    v0, v16, v18, v20, v22
    vsetvli     zero, zero, e16, \lmul2, ta, ma
    vle16.v     v8, (t0)
    addi        t0, t0, 2*HEVC_MAX_PB_SIZE
    vsadd.vv    v0, v0, v8
    vmax.vx     v0, v0, zero
    vsetvli     zero, zero, e8, \lmul, ta, ma
    vnclipu.wi  v0, v0, 7
    vse8.v      v0, (t3)
    add         t3, t3, a1
    addi        a5, a5, -1
    bgt         a5, zero, 2b
    add         t1, t1, t5
    sub         t6, t6, t5
    mv          a5, t4
    bgt         t6, zero, 1b

    lx          s1, 0(sp)
    lx          s2, 8(sp)
    lx          s3, 16(sp)
    lx          s4, 24(sp)
    addi        sp, sp, 32
    ret
endfunc
.endm

hevc_epel_v m1, m2, m4