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