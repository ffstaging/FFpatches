/*
 * Copyright (c) 2025 Institute of Software Chinese Academy of Sciences (ISCAS).
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

#include "libavutil/riscv/asm.S"

.macro add_member32 in, t0, t1, t2, t3, op0, op1, op2, op3
        .ifc \op0, -
                neg    t0, \t0
        .endif
        .ifc \op1, -
                neg    t1, \t1
        .endif
        .ifc \op2, -
                neg    t4, \t2
        .endif
        .ifc \op3, -
                neg    t5, \t3
        .endif

        .ifc \op0, -
                vwmacc.vx    v24, t0, \in
        .else
                vwmacc.vx    v24, \t0, \in
        .endif
        .ifc \op1, -
                vwmacc.vx    v25, t1, \in
        .else
                vwmacc.vx    v25, \t1, \in
        .endif
        .ifc \op2, -
                vwmacc.vx    v26, t4, \in
        .else
                vwmacc.vx    v26, \t2, \in
        .endif
        .ifc \op3, -
                vwmacc.vx    v27, t5, \in
        .else
                vwmacc.vx    v27, \t3, \in
        .endif
.endm

.macro tr_block_init
        vslidedown.vi    v12, v4, 4
        vslidedown.vi    v13, v5, 4
        vslidedown.vi    v14, v6, 4
        vslidedown.vi    v15, v7, 4

        vslidedown.vi    v20, v16, 4
        vslidedown.vi    v21, v17, 4
        vslidedown.vi    v22, v18, 4
        vslidedown.vi    v23, v19, 4
.endm

.macro tr_block1
        tr_block_init

        vwmul.vx        v24, v4, x12
        vwmul.vx        v25, v4, x13
        vwmul.vx        v26, v4, x14
        vwmul.vx        v27, v4, x15

        add_member32    v12, x13, x16, x19, x22,  +,  +,  +,  +
        add_member32    v5, x14, x19, x24, x26,  +,  +,  +,  -
        add_member32    v13, x15, x22, x26, x19,  +,  +,  -,  -
        add_member32    v6, x16, x25, x21, x12,  +,  +,  -,  -
        add_member32    v14, x17, x27, x16, x18,  +,  -,  -,  -
        add_member32    v7, x18, x24, x12, x25,  +,  -,  -,  -
        add_member32    v15, x19, x21, x17, x23,  +,  -,  -,  +


        add_member32    v16, x20, x18, x22, x16,  +,  -,  -,  +
        add_member32    v20, x21, x15, x27, x14,  +,  -,  -,  +
        add_member32    v17, x22, x13, x23, x21,  +,  -,  +,  +
        add_member32    v21, x23, x14, x18, x27,  +,  -,  +,  -
        add_member32    v18, x24, x17, x13, x20,  +,  -,  +,  -
        add_member32    v22, x25, x20, x15, x13,  +,  -,  +,  -
        add_member32    v19, x26, x23, x20, x17,  +,  -,  +,  -
        add_member32    v23, x27, x26, x25, x24,  +,  -,  +,  -
.endm

.macro tr_block2
        tr_block_init

        vwmul.vx        v24, v4, x16
        vwmul.vx        v25, v4, x17
        vwmul.vx        v26, v4, x18
        vwmul.vx        v27, v4, x19

        add_member32    v12, x25, x27, x24, x21,  +,  -,  -,  -
        add_member32    v5, x21, x16, x12, x17,  -,  -,  -,  -
        add_member32    v13, x12, x18, x25, x23,  -,  -,  -,  +
        add_member32    v6, x20, x26, x17, x15,  -,  +,  +,  +
        add_member32    v14, x26, x15, x19, x25,  +,  +,  +,  -
        add_member32    v7, x17, x19, x23, x12,  +,  +,  -,  -
        add_member32    v15, x15, x25, x13, x27,  +,  -,  -,  +

        add_member32    v16, x24, x14, x26, x13,  +,  -,  -,  +
        add_member32    v20, x22, x20, x16, x26,  -,  -,  +,  +
        add_member32    v17, x13, x24, x20, x14,  -,  +,  +,  -
        add_member32    v21, x19, x13, x22, x24,  -,  +,  -,  -
        add_member32    v18, x27, x21, x14, x16,  +,  +,  -,  +
        add_member32    v22, x18, x23, x27, x22,  +,  -,  -,  +
        add_member32    v19, x14, x13, x15, x18,  +,  -,  +,  -
        add_member32    v23, x23, x22, x21, x20,  +,  -,  +,  -
.endm

.macro tr_block3
        tr_block_init

        vwmul.vx        v24, v4, x20
        vwmul.vx        v25, v4, x21
        vwmul.vx        v26, v4, x22
        vwmul.vx        v27, v4, x23

        add_member32    v12, x18, x15, x12, x14,  -,  -,  -,  -
        add_member32    v5, x22, x27, x23, x18,  -,  -,  +,  +
        add_member32    v13, x16, x14, x21, x27,  +,  +,  +,  -
        add_member32    v6, x24, x22, x13, x19,  +,  -,  -,  -
        add_member32    v14, x14, x20, x24, x12,  -,  -,  +,  +
        add_member32    v7, x26, x16, x20, x22,  -,  +,  +,  -
        add_member32    v15, x12, x26, x14, x24,  +,  +,  -,  -
        add_member32    v16, x27, x13, x25, x15,  -,  -,  +,  +
        add_member32    v20, x13, x23, x19, x17,  -,  +,  +,  -
        add_member32    v17, x25, x19, x15, x26,  +,  +,  -,  +
        add_member32    v21, x15, x17, x26, x20,  +,  -,  +,  +
        add_member32    v18, x23, x25, x18, x13,  -,  -,  +,  -
        add_member32    v22, x17, x12, x16, x21,  -,  +,  -,  +
        add_member32    v19, x21, x24, x27, x25,  +,  -,  +,  +
        add_member32    v23, x19, x18, x17, x16,  +,  -,  +,  -
.endm

.macro tr_block4
        tr_block_init

        vwmul.vx        v24, v4, x24
        vwmul.vx        v25, v4, x25
        vwmul.vx        v26, v4, x26
        vwmul.vx        v27, v4, x27

        add_member32    v12, x17, x20, x23, x26,  -,  -,  -,  -
        add_member32    v5, x12, x15, x20, x25,  +,  +,  +,  +
        add_member32    v13, x20, x12, x17, x24,  -,  -,  -,  -
        add_member32    v6, x27, x18, x14, x23,  +,  +,  +,  +
        add_member32    v14, x21, x23, x12, x22,  +,  -,  -,  -
        add_member32    v7, x14, x27, x15, x21,  -,  -,  +,  +
        add_member32    v15, x16, x22, x18, x20,  +,  +,  -,  -
        add_member32    v16, x23, x17, x21, x19,  -,  -,  +,  +
        add_member32    v20, x25, x13, x24, x18,  -,  +,  -,  -
        add_member32    v17, x18, x16, x27, x17,  +,  -,  +,  +
        add_member32    v21, x13, x21, x25, x16,  -,  +,  +,  -
        add_member32    v18, x19, x26, x22, x15,  +,  -,  -,  +
        add_member32    v22, x26, x24, x19, x14,  -,  -,  +,  -
        add_member32    v19, x22, x19, x16, x13,  -,  +,  -,  +
        add_member32    v23, x15, x14, x13, x12,  +,  -,  +,  -
.endm

.macro butterfly e, o, tmp_p, tmp_m
        vadd.vv             \tmp_p, \e, \o
        vsub.vv             \tmp_m, \e, \o
.endm

.macro butterfly16 in0, in1, in2, in3, in4, in5, in6, in7
        vadd.vv              v20, \in0, \in1
        vsub.vv              \in0, \in0, \in1
        vadd.vv              \in1, \in2, \in3
        vsub.vv              \in2, \in2, \in3
        vadd.vv              \in3, \in4, \in5
        vsub.vv              \in4, \in4, \in5
        vadd.vv              \in5, \in6, \in7
        vsub.vv              \in6, \in6, \in7
.endm

.macro butterfly32 in0, in1, in2, in3, out
        vadd.vv             \out, \in0, \in1
        vsub.vv             \in0, \in0, \in1
        vadd.vv             \in1, \in2, \in3
        vsub.vv             \in2, \in2, \in3
.endm

.macro add_member in, tt0, tt1, tt2, tt3, tt4, tt5, tt6, tt7
        vwmacc.vx    v21, \tt0, \in
        vwmacc.vx    v22, \tt1, \in
        vwmacc.vx    v23, \tt2, \in
        vwmacc.vx    v24, \tt3, \in
        vwmacc.vx    v25, \tt4, \in
        vwmacc.vx    v26, \tt5, \in
        vwmacc.vx    v27, \tt6, \in
        vwmacc.vx    v28, \tt7, \in
.endm

.macro load16_rvv in0, in1, in2, in3, lo, hi, step
        sub t0, \hi, \lo

        vlse64.v \in0, (\lo), t0
        add \lo, \lo, \step

        vlse64.v \in1, (\lo), t0
        add \lo, \lo, \step

        vlse64.v \in2, (\lo), t0
        add \lo, \lo, \step

        vlse64.v \in3, (\lo), t0
.endm

.macro store16_rvv in0, in1, in2, in3, lo, hi, step
        sub t0, \hi, \lo
        li  t1, 2 * \step
        vsse64.v \in0, (\lo), t0

        addi \lo, \lo, \step
        sub t0, t0, t1
        vsse64.v \in1, (\lo), t0

        addi \lo, \lo, \step
        sub t0, t0, t1
        vsse64.v \in2, (\lo), t0

        addi \lo, \lo, \step
        sub t0, t0, t1
        vsse64.v \in3, (\lo), t0
.endm

.macro load32_rvv src
        addi             a2,  \src, 64
        li               a3,  256
        li               t0, 128

        vlse64.v v4, (a2), t0

        add s2, a2, a3
        vlse64.v v5, (s2), t0

        add s3, s2, a3
        vlse64.v v6, (s3), t0

        add s4, s3, a3
        vlse64.v v7, (s4), t0

        add s5, s4, a3
        vlse64.v v16, (s5), t0

        add s6, s5, a3
        vlse64.v v17, (s6), t0

        add s7, s6, a3
        vlse64.v v18, (s7), t0

        add s8, s7, a3
        vlse64.v v19, (s8), t0
.endm

.macro scale_store_rvv shift, dstL, dstH, step
        vsetivli    zero, 8, e16, m1, ta, ma
        vle16.v        v28, (t2)
        addi            t2, t2, 2*8
        vle16.v        v29, (t2)
        addi            t2, t2, 2*8
        vle16.v        v30, (t2)
        addi            t2, t2, 2*8
        vle16.v        v31, (t2)
        addi            t2, t2, 2*8

        vsetivli    zero, 4, e32, m1, ta, ma
        butterfly32     v28, v24, v29, v25, v2
        butterfly32     v30, v26, v31, v27, v3

        scale           v20, v21, v22, v23, v2, v28, v24, v29, v3, v30, v26, v31, \shift

        transpose16_4x4_2 20, 21, 22, 23, 24, 25, 26, 27, 28, 29

        vsetivli    zero, 2, e64, m1, ta, ma
        store16_rvv         v20, v21, v22, v23, \dstL, \dstH, \step
.endm

.macro store_to_stack_rvv off1, off2, in0, in2, in4, in6, in7, in5, in3, in1
        li      t0, \off1
        add     a2, sp, t0

        li      t0, \off2
        add     a3, sp, t0

        vse32.v         \in0, (a2)
        vse32.v         \in1, (a3)
        addi             a2, a2, 16
        addi             a3, a3, -16
        vse32.v         \in2, (a2)
        vse32.v         \in3, (a3)
        addi             a2, a2, 16
        addi             a3, a3, -16
        vse32.v         \in4, (a2)
        vse32.v         \in5, (a3)
        addi             a2, a2, 16
        addi             a3, a3, -16
        vse32.v         \in6, (a2)
        vse32.v         \in7, (a3)
.endm

.macro transpose16_4x4_2 r0, r1, r2, r3, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5
        vsetivli    zero, 8, e16, m1, ta, ma
        vid.v     v0
        vand.vi   v8, v0, 1
        vmsne.vi  v0, v8, 0

        vslideup.vi    v8, v\r1, 1
        vsetivli    zero, 4, e16, m1, ta, ma
        vmerge.vvm       v\tmp0\(), v\r0\(), v8, v0

        vslidedown.vi    v8, v\r0, 1
        vmerge.vvm       v\tmp1\(), v8, v\r1\(), v0

        vslideup.vi    v8, v\r3, 1
        vmerge.vvm       v\tmp2\(), v\r2\(), v8, v0

        vslidedown.vi    v8, v\r2\(), 1
        vmerge.vvm       v\tmp3\(), v8, v\r3\(), v0

        vsetivli    zero, 2, e32, m1, ta, ma

        vslideup.vi    v8, v\tmp2\(), 1
        vmerge.vvm       v\tmp4\(), v\tmp0\(), v8, v0

        vslidedown.vi    v8, v\tmp0\(), 1
        vmerge.vvm       v\tmp5\(), v8, v\tmp2\(), v0

        vslideup.vi    v8, v\tmp3\(), 1
        vmerge.vvm       v\tmp0\(), v\tmp1\(), v8, v0

        vslidedown.vi    v8, v\tmp1\(), 1
        vmerge.vvm       v\tmp2\(), v8, v\tmp3\(), v0

        vsetivli    zero, 1, e64, m1, ta, ma
        vmv.v.v  v\r0\(), v\tmp4\()
        vmv.v.v  v\r2\(), v\tmp5\()
        vmv.v.v  v\r1\(), v\tmp0\()
        vmv.v.v  v\r3\(), v\tmp2\()

        vsetivli    zero, 8, e16, m1, ta, ma

        vslideup.vi    v8, v\r2\(), 1
        vmerge.vvm       v\tmp0\(), v\r3\(), v8, v0

        vslidedown.vi    v8, v\r3\(), 1
        vmerge.vvm        v\tmp1\(), v8, v\r2\(), v0

        vslideup.vi    v8, v\r0\(), 1
        vmerge.vvm       v\tmp2\(), v\r1\(),v8, v0

        vslidedown.vi    v8, v\r1\(), 1
        vmerge.vvm        v\tmp3\(), v8, v\r0\(), v0

        vsetivli    zero, 4, e32, m1, ta, ma

        vslideup.vi    v8, v\tmp2\(), 1
        vmerge.vvm       v\tmp4\(), v\tmp0\(), v8, v0

        vslidedown.vi    v8, v\tmp0\(), 1
        vmerge.vvm       v\tmp5\(), v8, v\tmp2\(), v0

        vslideup.vi    v8, v\tmp3\(), 1
        vmerge.vvm       v\tmp0\(), v\tmp1\(), v8, v0

        vslidedown.vi    v8, v\tmp1\(), 1
        vmerge.vvm       v\tmp2\(), v8, v\tmp3\(), v0

        vsetivli    zero, 2, e64, m1, ta, ma

        vmerge.vvm       v\r3\(), v\r3\(), v\tmp4\(), v0
        vmerge.vvm       v\r1\(), v\r1\(), v\tmp5\(), v0
        vmerge.vvm       v\r2\(), v\r2\(), v\tmp0\(), v0
        vmerge.vvm       v\r0\(), v\r0\(), v\tmp2\(), v0
.endm

.macro load_trans_8x4
        li s6, 89
        li s7, 75
        li s8, 50
        li s9, 18

        neg    s2, s6
        neg    s4, s8
        neg    s5, s9
.endm

.macro scale out0, out1, out2, out3, in0, in1, in2, in3, in4, in5, in6, in7, shift
        vsetivli        zero, 4, e16, mf2, ta, ma
        vnclip.wi       \out0\(), \in0\(), \shift
        vnclip.wi       \out1\(), \in2\(), \shift
        vnclip.wi       \out2\(), \in4\(), \shift
        vnclip.wi       \out3\(), \in6\(), \shift

        vnclip.wi       \in1\(), \in1\(), \shift
        vnclip.wi       \in3\(), \in3\(), \shift
        vnclip.wi       \in5\(), \in5\(), \shift
        vnclip.wi       \in7\(), \in7\(), \shift

        vsetivli        zero, 2, e64, m1, ta, ma
        vslideup.vi     \out0\(), \in1\(), 1
        vslideup.vi     \out1\(), \in3\(), 1
        vslideup.vi     \out2\(), \in5\(), 1
        vslideup.vi     \out3\(), \in7\(), 1
.endm

.macro load_trans_4x4
        li s2, 64
        li s3, 83

        li s5, 36

        neg     s6, s2
        neg     s7, s3
.endm

.macro tr_4x4_8 in0, in1, in2, in3, out0, out1, out2, out3
        vsetivli    zero, 4, e16, m1, ta, ma
        vwcvt.x.x.v     v8, \in0
        vsetivli    zero, 4, e32, m1, ta, ma
        vsll.vi         v28, v8, 6

        vsetivli    zero, 16, e8, m1, ta, ma
        vmv.v.v v29, v28

        load_trans_4x4

        vsetivli    zero, 4, e16, mf2, ta, ma
        vwmul.vx    v30, \in1, s3
        vwmul.vx    v31, \in1, s5
        vwmacc.vx    v28, s2, \in2

        vwmacc.vx    v29, s6, \in2
        vwmacc.vx    v30, s5, \in3
        vwmacc.vx    v31, s7, \in3

        vsetivli    zero, 4, e32, m1, ta, ma
        vadd.vv             \out0, v28, v30
        vadd.vv             \out1, v29, v31
        vsub.vv             \out2, v29, v31
        vsub.vv             \out3, v28, v30
.endm

.macro tr16_8x4 in0, in1, in2, in3, offset
        tr_4x4_8        \in0, \in1, \in2, \in3, v24, v25, v26, v27

        load_trans_8x4

        vsetivli    zero, 4, e16, mf2, ta, ma
        vslidedown.vi    v8, \in0, 4
        vwmul.vx    v28, v8, s6
        vwmul.vx    v29, v8, s7
        vwmul.vx    v30, v8, s8
        vwmul.vx    v31, v8, s9

        vslidedown.vi    v8, \in1, 4
        vwmacc.vx    v28, s7, v8
        vwmacc.vx    v29, s5, v8
        vwmacc.vx    v30, s2, v8
        vwmacc.vx    v31, s4, v8

        vslidedown.vi    v8, \in2, 4
        vwmacc.vx    v28, s8, v8
        vwmacc.vx    v29, s2, v8
        vwmacc.vx    v30, s9, v8
        vwmacc.vx    v31, s7, v8

        vslidedown.vi    v8, \in3, 4
        vwmacc.vx    v28, s9, v8
        vwmacc.vx    v29, s4, v8
        vwmacc.vx    v30, s7, v8
        vwmacc.vx    v31, s2, v8

        vsetivli    zero, 4, e32, m1, ta, ma
        butterfly       v24, v28, v16, v23
        butterfly       v25, v29, v17, v22
        butterfly       v26, v30, v18, v21
        butterfly       v27, v31, v19, v20

        li t0, \offset
        add t0, sp, t0

        vse32.v v16, (t0)
        add t0, t0, 16
        vse32.v v17, (t0)
        add t0, t0, 16
        vse32.v v18, (t0)
        add t0, t0, 16
        vse32.v v19, (t0)
        add t0, t0, 16

        vse32.v v20, (t0)
        add t0, t0, 16
        vse32.v v21, (t0)
        add t0, t0, 16
        vse32.v v22, (t0)
        add t0, t0, 16
        vse32.v v23, (t0)
.endm

.macro load_trans_16x4
        li x12, 90
        li x13, 87
        li x14, 80
        li x15, 70
        li x16, 57
        li x17, 43
        li x18, 25
        li x19, 9

        neg x20, x12
        neg x21, x13
        neg x22, x14
        neg x23, x15

        neg x24, x16
        neg x25, x17
        neg x26, x18
        neg x27, x19
.endm

.macro tr_16x4_rvv name, shift, offset, step
func func_tr_16x4_\name\()_rvv, zve64x
        mv             a2,  a0
        addi           a3,  a0, \step * 64
        li             a4,  \step * 128
        vsetivli    zero, 2, e64, m1, ta, ma
        load16_rvv     v16, v17, v18, v19, a2, a3, a4

        tr16_8x4        v16, v17, v18, v19, \offset

        addi            a2,  a0, \step * 32
        addi            a3,  a0, \step * 3 *32
        li              a4,  \step * 128
        vsetivli        zero, 2, e64, m1, ta, ma
        load16_rvv          v20, v17, v18, v19, a2, a3, a4

        load_trans_16x4

        vsetivli    zero, 4, e16, mf2, ta, ma
        vslidedown.vi    v0, v17, 4
        vslidedown.vi    v1, v18, 4
        vslidedown.vi    v2, v19, 4
        vslidedown.vi    v3, v20, 4

        vwmul.vx            v21, v20, x12
        vwmul.vx            v22, v20, x13
        vwmul.vx            v23, v20, x14
        vwmul.vx            v24, v20, x15

        vwmul.vx            v25, v20, x16
        vwmul.vx            v26, v20, x17
        vwmul.vx            v27, v20, x18
        vwmul.vx            v28, v20, x19

        add_member      v3, x13, x16, x19, x25, x22, x20, x23, x26
        add_member      v17, x14, x19, x23, x21, x26, x16, x12, x17
        add_member      v0, x15, x25, x21, x19, x12, x18, x22, x24
        add_member      v18, x16, x22, x26, x12, x27, x21, x17, x15
        add_member      v1, x17, x20, x16, x18, x21, x15, x19, x22
        add_member      v19, x18, x23, x12, x22, x17, x19, x24, x13
        add_member      v2, x19, x26, x17, x24, x15, x22, x13, x20

        li t0, \offset
        add t0, sp, t0
        vle32.v        v16, (t0)

        addi            t0, t0, 16
        vle32.v        v17, (t0)

        addi            t0, t0, 16
        vle32.v        v18, (t0)

        addi            t0, t0, 16
        vle32.v        v19, (t0)

        vsetivli    zero, 4, e32, m1, ta, ma
        butterfly16       v16, v21, v17, v22, v18, v23, v19, v24
        store_to_stack_rvv  \offset, (\offset + 240), v20, v21, v22, v23, v19, v18, v17, v16

        li t0, \offset+64
        add t0, sp, t0

        vle32.v         v16, (t0)
        addi            t0, t0, 16
        vle32.v         v17, (t0)
        addi            t0, t0, 16
        vle32.v         v18, (t0)
        addi            t0, t0, 16
        vle32.v         v19, (t0)

        butterfly16     v16, v25, v17, v26, v18, v27, v19, v28
        store_to_stack_rvv  (\offset + 64), (\offset + 176), v20, v25, v26, v27, v19, v18, v17, v16
        ret
endfunc
.endm

tr_16x4_rvv noscale, 0, 2048, 4

.macro load_trans_32x4
        li x12, 90
        li x13, 90
        li x14, 88
        li x15, 85
        li x16, 82
        li x17, 78
        li x18, 73
        li x19, 67
        li x20, 61
        li x21, 54
        li x22, 46
        li x23, 38
        li x24, 31
        li x25, 22
        li x26, 13
        li x27, 4
.endm

.macro tr_32x4_rvv name, shift
func func_tr_32x4_\name\()_rvv, zve64x
        mv             t3, ra

        jal              func_tr_16x4_noscale_rvv

        vsetivli    zero, 2, e64, m1, ta, ma
        load32_rvv a0

        li t0, 2048
        add t2, sp, t0

        load_trans_32x4

        vsetivli    zero, 4, e16, mf2, ta, ma
        tr_block1
        mv                      t5, a1
        addi                    t1, a1, (56 + 3 * 64)
        scale_store_rvv         \shift, t5, t1, 64

        vsetivli    zero, 4, e16, mf2, ta, ma
        tr_block2
        addi                    t5, a1, 8
        addi                    t1, a1, (48 + 3 * 64)
        scale_store_rvv         \shift, t5, t1, 64

        vsetivli    zero, 4, e16, mf2, ta, ma
        tr_block3
        addi                    t5, a1, 16
        addi                    t1, a1, (40 + 3 * 64)
        scale_store_rvv         \shift, t5, t1, 64

        vsetivli    zero, 4, e16, mf2, ta, ma
        tr_block4
        addi                    t5, a1, 24
        addi                    t1, a1, (32 + 3 * 64)
        scale_store_rvv         \shift, t5, t1, 64

        jr t3
endfunc
.endm

tr_32x4_rvv firstpass, 7
tr_32x4_rvv secondpass_8, 20 - 8

.macro idct_32x32 bitdepth
func ff_hevc_idct_32x32_\bitdepth\()_rvv, zve64x
        addi    sp, sp, -8*13
        sd      ra, 8*12(sp)
.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        sd    s\i, 8*(11-\i)(sp)
.endr
        mv t6, a0

        csrwi   vxrm, 1
        li      t0, 2432
        sub     sp, sp, t0

.irp i, 0, 1, 2, 3, 4, 5, 6, 7
        li               t0, 8 * \i
        add              a0, t6, t0

        li               t0, 8 * \i * 32
        add              a1, sp, t0

        jal              func_tr_32x4_firstpass_rvv
.endr

.irp i, 0, 1, 2, 3, 4, 5, 6, 7
        addi             a0, sp, 8 * \i
        addi             a1, t6, 8 * \i * 32
        jal              func_tr_32x4_secondpass_\bitdepth\()_rvv
.endr

        li              t0, 2432
        add             sp,  sp,  t0

        ld    ra, 8*12(sp)
.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        ld    s\i, 8*(11-\i)(sp)
.endr
        addi    sp, sp, 8*13
        ret
endfunc
.endm

idct_32x32 8
