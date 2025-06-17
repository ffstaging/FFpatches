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

.macro load_trans_4x4
        li s2, 64
        li s3, 83

        li s5, 36
        li s6, -64
        li s7, -83
.endm

.macro load_trans_8x4
        li s6, 89
        li s7, 75
        li s8, 50
        li s9, 18

        li s2, -89
        li s4, -50
        li s5, -18
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

        li x20, -90
        li x21, -87
        li x22, -80
        li x23, -70

        li x24, -57
        li x25, -43
        li x26, -25
        li x27, -9
.endm

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

.macro tr_block1
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

.macro load16xN_rvv avl
        addi t0,  a0, 64
        addi a2, t0, 256 * 1
        addi a3, t0, 256 * 2
        addi a4, t0, 256 * 3
        addi a5, t0, 256 * 4
        addi a6, t0, 256 * 5
        addi a7, t0, 256 * 6
        addi s9, t0, 256 * 7

        addi t1,  t0, 128
        addi s2, t1, 256 * 1
        addi s3, t1, 256 * 2
        addi s4, t1, 256 * 3
        addi s5, t1, 256 * 4
        addi s6, t1, 256 * 5
        addi s7, t1, 256 * 6
        addi s8, t1, 256 * 7

        vle16.v v4, (t0)
        vle16.v v5, (a2)
        vle16.v v6, (a3)
        vle16.v v7, (a4)

        vle16.v v16, (a5)
        vle16.v v17, (a6)
        vle16.v v18, (a7)
        vle16.v v19, (s9)

        vle16.v v12, (t1)
        vle16.v v13, (s2)
        vle16.v v14, (s3)
        vle16.v v15, (s4)

        vle16.v v20, (s5)
        vle16.v v21, (s6)
        vle16.v v22, (s7)
        vle16.v v23, (s8)
.endm

.macro load16_rvv in0, in1, in2, in3, off1, off2, step, in4, in5, in6, in7
        addi t0,  a0, \off1
        addi a2, t0, \step * 1
        addi a3, t0, \step * 2
        addi a4, t0, \step * 3

        addi t1,  a0, \off2
        addi s2, t1, \step * 1
        addi s3, t1, \step * 2
        addi s4, t1, \step * 3

        vle16.v \in0, (t0)
        vle16.v \in1, (a2)
        vle16.v \in2, (a3)
        vle16.v \in3, (a4)

        vle16.v \in4, (t1)
        vle16.v \in5, (s2)
        vle16.v \in6, (s3)
        vle16.v \in7, (s4)
.endm

.macro reload16 offset, avl
        li t0, 2048
        add t0, sp, t0
        addi t0, t0, \offset
        addi t1, t0, 4 * \avl\() * 1
        addi t2, t0, 4 * \avl\() * 2
        addi t3, t0, 4 * \avl\() * 3

        vle32.v        v28, (t0)
        vle32.v        v29, (t1)
        vle32.v        v30, (t2)
        vle32.v        v31, (t3)
.endm

.macro  storeNx4_rvv in0, in1, in2, in3, off1, off2, step, tmp0, tmp1, tmp2, tmp3
        li t0, \step

        addi a0, a1, \off1
        addi t2, a0, 1 * 2
        addi t4, a0, 2 * 2
        addi s0, a0, 3 * 2

        addi t1, a1, 62 - \off1\()
        addi t3, t1, -1 * 2
        addi t5, t1, -2 * 2
        addi s1, t1, -3 * 2

        vsse16.v \in0, (a0), t0
        vsse16.v \in1, (t2), t0
        vsse16.v \in2, (t4), t0
        vsse16.v \in3, (s0), t0

        vsse16.v \tmp0, (t1), t0
        vsse16.v \tmp1, (t3), t0
        vsse16.v \tmp2, (t5), t0
        vsse16.v \tmp3, (s1), t0
.endm

.macro scale_store_rvv shift, step, off1, off2, reload_offset, avl
        vsetivli    zero, \avl\(), e32, m1, ta, ma
        reload16 \reload_offset, \avl\()

        butterfly32     v28, v24, v29, v25, v2
        butterfly32     v30, v26, v31, v27, v3

        vsetivli        zero, \avl\(), e16, mf2, ta, ma
        scale           v1, v10, v11, v9, v2, v28, v24, v29, v3, v30, v26, v31, \shift

        storeNx4_rvv      v1, v10, v11, v9, \off1, \off2, \step, v2, v28, v24, v29
.endm

.macro store_to_stack_rvv off1, off2, in0, in2, in4, in6, in7, in5, in3, in1, avl
.if \off1 < 2048
        addi     a2, sp, \off1
.else
        li      t0, \off1
        add     a2, sp, t0
.endif

.if \off2 < 2048
        addi     a3, sp, \off2
.else
        li      t0, \off2
        add     a3, sp, t0
.endif

        addi             a4, a2, \avl\() * 4 * 1
        addi             a5, a3, -\avl\() * 4 * 1
        addi             a6, a2, \avl\() * 4 * 2
        addi             a7, a3, -\avl\() * 4 * 2
        addi             s2, a2, \avl\() * 4 * 3
        addi             s3, a3, -\avl\() * 4 * 3

        vse32.v         \in0, (a2)
        vse32.v         \in1, (a3)
        vse32.v         \in2, (a4)
        vse32.v         \in3, (a5)
        vse32.v         \in4, (a6)
        vse32.v         \in5, (a7)
        vse32.v         \in6, (s2)
        vse32.v         \in7, (s3)
.endm

.macro scale out0, out1, out2, out3, in0, in1, in2, in3, in4, in5, in6, in7, shift
        vnclip.wi       \out0\(), \in0\(), \shift
        vnclip.wi       \out1\(), \in2\(), \shift
        vnclip.wi       \out2\(), \in4\(), \shift
        vnclip.wi       \out3\(), \in6\(), \shift

        vnclip.wi       \in0\(), \in1\(), \shift
        vnclip.wi       \in1\(), \in3\(), \shift
        vnclip.wi       \in2\(), \in5\(), \shift
        vnclip.wi       \in3\(), \in7\(), \shift
.endm

.macro tr_4xN_8 in0, in1, in2, in3, out0, out1, out2, out3, avl
        vwcvt.x.x.v     v8, \in0
        vsetivli    zero, \avl\(), e32, m1, ta, ma
        vsll.vi         v28, v8, 6
        vmv.v.v v29, v28

        load_trans_4x4

        vsetivli    zero, \avl\(), e16, mf2, ta, ma
        vwmul.vx    v30, \in1, s3
        vwmul.vx    v31, \in1, s5
        vwmacc.vx    v28, s2, \in2

        vwmacc.vx    v29, s6, \in2
        vwmacc.vx    v30, s5, \in3
        vwmacc.vx    v31, s7, \in3

        vsetivli    zero, \avl\(), e32, m1, ta, ma
        vadd.vv             \out0, v28, v30
        vadd.vv             \out1, v29, v31
        vsub.vv             \out2, v29, v31
        vsub.vv             \out3, v28, v30
.endm

.macro tr16_8xN in0, in1, in2, in3, offset, in4, in5, in6, in7, avl
        tr_4xN_8        \in0, \in1, \in2, \in3, v24, v25, v26, v27, \avl\()
        load_trans_8x4

        vsetivli    zero, \avl\(), e16, mf2, ta, ma
        vwmul.vx    v28, \in4, s6
        vwmul.vx    v29, \in4, s7
        vwmul.vx    v30, \in4, s8
        vwmul.vx    v31, \in4, s9

        vwmacc.vx    v28, s7, \in5
        vwmacc.vx    v29, s5, \in5
        vwmacc.vx    v30, s2, \in5
        vwmacc.vx    v31, s4, \in5

        vwmacc.vx    v28, s8, \in6
        vwmacc.vx    v29, s2, \in6
        vwmacc.vx    v30, s9, \in6
        vwmacc.vx    v31, s7, \in6

        vwmacc.vx    v28, s9, \in7
        vwmacc.vx    v29, s4, \in7
        vwmacc.vx    v30, s7, \in7
        vwmacc.vx    v31, s2, \in7

        vsetivli    zero, \avl\(), e32, m1, ta, ma
        butterfly       v24, v28, v16, v23
        butterfly       v25, v29, v17, v22
        butterfly       v26, v30, v18, v21
        butterfly       v27, v31, v19, v20
.if \offset < 2048
        addi t0, sp, \offset
.else
        li t0, \offset
        add t0, sp, t0
.endif

        addi s2, t0, \avl\() * 4 * 1
        addi s3, t0, \avl\() * 4 * 2
        addi s4, t0, \avl\() * 4 * 3

        addi s5, t0, \avl\() * 4 * 4
        addi s6, t0, \avl\() * 4 * 5
        addi s7, t0, \avl\() * 4 * 6
        addi s8, t0, \avl\() * 4 * 7

        vsetivli    zero, \avl\(), e32, m1, ta, ma
        vse32.v v16, (t0)
        vse32.v v17, (s2)
        vse32.v v18, (s3)
        vse32.v v19, (s4)

        vse32.v v20, (s5)
        vse32.v v21, (s6)
        vse32.v v22, (s7)
        vse32.v v23, (s8)
.endm

.macro tr_16xN_rvv name, shift, offset, step, vlen
func func_tr_16xN_\name\()_rvv_\vlen\(), zve64x
        vsetivli        zero, (\vlen\() / 32), e16, mf2, ta, ma
        load16_rvv     v16, v17, v18, v19, 0, \step * 64, \step * (2 * 64), v0, v1, v2, v3,
        tr16_8xN        v16, v17, v18, v19, \offset, v0, v1, v2, v3, (\vlen\() / 32)

        vsetivli        zero, (\vlen\() / 32), e16, mf2, ta, ma
        load16_rvv          v20, v17, v18, v19, \step * 32, \step * 3 * 32, \step * (2 * 64), v3, v0, v1, v2,

        load_trans_16x4

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

.if \offset < 2048
        addi t0, sp, \offset
.else
        li t0, \offset
        add t0, sp, t0
.endif
        addi s2, t0, (\vlen\() / 8) * 1
        addi s3, t0, (\vlen\() / 8) * 2
        addi s4, t0, (\vlen\() / 8) * 3

        vsetivli    zero, (\vlen\() / 32), e32, m1, ta, ma

        vle32.v        v16, (t0)
        vle32.v        v17, (s2)
        vle32.v        v18, (s3)
        vle32.v        v19, (s4)

        butterfly16       v16, v21, v17, v22, v18, v23, v19, v24
        store_to_stack_rvv  \offset, (\offset + 2 * \vlen\() - (\vlen\() / 8)), v20, v21, v22, v23, v19, v18, v17, v16, (\vlen\() / 32)

.if \offset < 2048 - ( 4 * (\vlen\() / 32) * 4)
        addi t0, sp, \offset + (4 * (\vlen\() / 32) * 4)
.else
        li t0, \offset + ( 4 * (\vlen\() / 32) * 4)
        add t0, sp, t0
.endif
        addi s2, t0, (\vlen\() / 8)
        addi s3, t0, (\vlen\() / 8) * 2
        addi s4, t0, (\vlen\() / 8) * 3

        vle32.v         v16, (t0)
        vle32.v         v17, (s2)
        vle32.v         v18, (s3)
        vle32.v         v19, (s4)

        butterfly16     v16, v25, v17, v26, v18, v27, v19, v28
        store_to_stack_rvv  (\offset + (\vlen\() / 2)), (\offset + \vlen\() * 2 - \vlen\() / 8 - \vlen\() / 2), v20, v25, v26, v27, v19, v18, v17, v16, (\vlen\() / 32)
        ret
endfunc
.endm

tr_16xN_rvv noscale, 0, 2048, 4, 128
tr_16xN_rvv noscale, 0, 2048, 4, 256

.macro tr_32xN_rvv name, shift, vlen
func func_tr_32xN_\name\()_rvv_\vlen\(), zve64x
        vsetivli    zero, (\vlen\() / 32), e16, mf2, ta, ma
        load16xN_rvv

        load_trans_32x4

        tr_block1
        scale_store_rvv         \shift, 64, 0, (56 + 3 * 64), 0, (\vlen\() / 32)

        tr_block2
        scale_store_rvv          \shift, 64, 8, (48 + 3 * 64), (\vlen\() / 2), (\vlen\() / 32)

        tr_block3
        scale_store_rvv         \shift, 64, 16, (40 + 3 * 64), (\vlen\() / 2) * 2, (\vlen\() / 32)

        tr_block4
        scale_store_rvv          \shift, 64, 24, (32 + 3 * 64), (\vlen\() / 2) * 3, (\vlen\() / 32)

        ret
endfunc
.endm

tr_32xN_rvv firstpass, 7, 128
tr_32xN_rvv secondpass_8, 20 - 8, 128

tr_32xN_rvv firstpass, 7, 256
tr_32xN_rvv secondpass_8, 20 - 8, 256

.macro idct_32x32 bitdepth, vlen
func ff_hevc_idct_32x32_\bitdepth\()_rvv_\vlen\(), zve64x

        addi    sp, sp, -(__riscv_xlen / 8)*13
        sx  ra, (__riscv_xlen / 8)*(12)(sp)
.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        sx    s\i, (__riscv_xlen / 8)*(11-\i)(sp)
.endr
        mv t6, a0

        csrwi   vxrm, 1

        li      t0, 4224
        sub     sp, sp, t0

.set i, 0
.rept 1024 / \vlen\()
.set OFFSET, \vlen\() * i / 16
        addi              a0, t6, OFFSET
.set OFFSET, \vlen\() * i * 2
        addi              a1, sp, OFFSET
        jal              func_tr_16xN_noscale_rvv_\vlen\()
        jal              func_tr_32xN_firstpass_rvv_\vlen\()
.set i, i+1
.endr

.set i, 0
.rept 1024 / \vlen\()
.set OFFSET, \vlen\() * i / 16
        addi              a0, sp, OFFSET
.set OFFSET, \vlen\() * i * 2
        addi              a1, t6, OFFSET
        jal              func_tr_16xN_noscale_rvv_\vlen\()
        jal              func_tr_32xN_secondpass_\bitdepth\()_rvv_\vlen\()
.set i, i+1
.endr

        li              t0, 4224
        add             sp,  sp,  t0

        lx  ra, (__riscv_xlen / 8)*(12)(sp)
.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        lx    s\i, (__riscv_xlen / 8)*(11-\i)(sp)
.endr
        addi    sp, sp, (__riscv_xlen / 8)*13
        ret
endfunc
.endm

idct_32x32 8, 128
idct_32x32 8, 256
