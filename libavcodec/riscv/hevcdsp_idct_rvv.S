/*
 * Copyright (c) 2025 Institute of Software, Chinese Academy of Sciences (ISCAS).
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
        li a2, 64
        li a3, 83

        li a5, 36
        li a6, -64
        li a7, -83
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
                neg    t2, \t2
        .endif
        .ifc \op3, -
                neg    t3, \t3
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
                vwmacc.vx    v26, t2, \in
        .else
                vwmacc.vx    v26, \t2, \in
        .endif
        .ifc \op3, -
                vwmacc.vx    v27, t3, \in
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
        add_member32    v5,  x14, x19, x24, x26,  +,  +,  +,  -
        add_member32    v13, x15, x22, x26, x19,  +,  +,  -,  -

        add_member32    v6,  x16, x25, x21, x12,  +,  +,  -,  -
        add_member32    v14, x17, x27, x16, x18,  +,  -,  -,  -
        add_member32    v7,  x18, x24, x12, x25,  +,  -,  -,  -
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
        add_member32    v5,  x21, x16, x12, x17,  -,  -,  -,  -
        add_member32    v13, x12, x18, x25, x23,  -,  -,  -,  +

        add_member32    v6,  x20, x26, x17, x15,  -,  +,  +,  +
        add_member32    v14, x26, x15, x19, x25,  +,  +,  +,  -
        add_member32    v7,  x17, x19, x23, x12,  +,  +,  -,  -
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
        add_member32    v5,  x22, x27, x23, x18,  -,  -,  +,  +
        add_member32    v13, x16, x14, x21, x27,  +,  +,  +,  -

        add_member32    v6,  x24, x22, x13, x19,  +,  -,  -,  -
        add_member32    v14, x14, x20, x24, x12,  -,  -,  +,  +
        add_member32    v7,  x26, x16, x20, x22,  -,  +,  +,  -
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
        add_member32    v5,  x12, x15, x20, x25,  +,  +,  +,  +
        add_member32    v13, x20, x12, x17, x24,  -,  -,  -,  -

        add_member32    v6,  x27, x18, x14, x23,  +,  +,  +,  +
        add_member32    v14, x21, x23, x12, x22,  +,  -,  -,  -
        add_member32    v7,  x14, x27, x15, x21,  -,  -,  +,  +
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

.macro butterfly16 out0, out1, out2, out3, out4, out5, out6, out7, in0, in1, in2, in3, in4, in5, in6, in7
        vadd.vv              \out0, \in0, \in1
        vsub.vv              \out1, \in0, \in1
        vadd.vv              \out2, \in2, \in3
        vsub.vv              \out3, \in2, \in3

        vadd.vv              \out4, \in4, \in5
        vsub.vv              \out5, \in4, \in5
        vadd.vv              \out6, \in6, \in7
        vsub.vv              \out7, \in6, \in7
.endm

.macro butterfly32 in0, in1, in2, in3, out0, out1, out2, out3
        vadd.vv             \out0, \in0, \in1
        vsub.vv             \out1, \in0, \in1

        vadd.vv             \out2, \in2, \in3
        vsub.vv             \out3, \in2, \in3
.endm

.macro load16xN_rvv
        addi t0,  a0, 64
        addi a2, t0, 256 * 1
        addi a3, t0, 256 * 2
        addi a4, t0, 256 * 3
        vle16.v v4, (t0)
        vle16.v v5, (a2)
        vle16.v v6, (a3)
        vle16.v v7, (a4)

        addi a5, t0, 256 * 4
        addi a6, t0, 256 * 5
        addi a7, t0, 256 * 6
        addi s9, t0, 256 * 7
        vle16.v v16, (a5)
        vle16.v v17, (a6)
        vle16.v v18, (a7)
        vle16.v v19, (s9)

        addi t1,  t0, 128
        addi s2, t1, 256 * 1
        addi s3, t1, 256 * 2
        addi s4, t1, 256 * 3
        vle16.v v12, (t1)
        vle16.v v13, (s2)
        vle16.v v14, (s3)
        vle16.v v15, (s4)

        addi s5, t1, 256 * 4
        addi s6, t1, 256 * 5
        addi s7, t1, 256 * 6
        addi s8, t1, 256 * 7
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
        vle16.v \in0, (t0)
        vle16.v \in1, (a2)
        vle16.v \in2, (a3)
        vle16.v \in3, (a4)

        addi t1,  a0, \off2
        addi s2, t1, \step * 1
        addi s3, t1, \step * 2
        addi s4, t1, \step * 3
        vle16.v \in4, (t1)
        vle16.v \in5, (s2)
        vle16.v \in6, (s3)
        vle16.v \in7, (s4)
.endm

.macro reload16 reload_offset
        add t0, t4, \reload_offset
        add t1, t0, t5
        add t2, t1, t5
        add t3, t2, t5

        vle32.v        v28, (t0)
        vle32.v        v29, (t1)
        vle32.v        v30, (t2)
        vle32.v        v31, (t3)
.endm

.macro scale out0, out1, out2, out3, out4, out5, out6, out7, in0, in1, in2, in3, in4, in5, in6, in7, shift
        vnclip.wi       \out0\(), \in0\(), \shift
        vnclip.wi       \out1\(), \in2\(), \shift
        vnclip.wi       \out2\(), \in4\(), \shift
        vnclip.wi       \out3\(), \in6\(), \shift

        vnclip.wi       \out4\(), \in1\(), \shift
        vnclip.wi       \out5\(), \in3\(), \shift
        vnclip.wi       \out6\(), \in5\(), \shift
        vnclip.wi       \out7\(), \in7\(), \shift
.endm

.macro scale_store_rvv shift, off, reload_offset
        vsetvli         zero, zero, e32, m1, ta, ma
        reload16        \reload_offset

        butterfly32     v28, v24, v29, v25, v0, v1, v2, v3
        butterfly32     v30, v26, v31, v27, v8, v9, v10, v11
        
        vsetvli         zero, zero, e16, mf2, ta, ma
        scale           v24, v25, v26, v27,  v31, v30, v29, v28,  v0, v1, v2, v3,  v8, v9, v10, v11, \shift

        li t2, 32*2
        add t0, a1, \off
        vssseg4e16.v v24, (t0), t2
        
        addi t0, a1, (64-4*2)-\off
        vssseg4e16.v v28, (t0), t2
.endm

.macro add_member in, tt0, tt1, tt2, tt3, tt4, tt5, tt6, tt7
        vwmacc.vx    v8, \tt0, \in
        vwmacc.vx    v9, \tt1, \in
        vwmacc.vx    v10, \tt2, \in
        vwmacc.vx    v11, \tt3, \in

        vwmacc.vx    v12, \tt4, \in
        vwmacc.vx    v13, \tt5, \in
        vwmacc.vx    v14, \tt6, \in
        vwmacc.vx    v15, \tt7, \in
.endm

.macro tr_16xN_rvv name, shift, step
func func_tr_16xN_\name\()_rvv, zve32x
        load16_rvv     v0, v1, v2, v3, 0, \step * 64, \step * (2 * 64), v4, v5, v6, v7,
        load16_rvv      v24, v25, v26, v27, \step * 32, \step * 3 * 32, \step * (2 * 64), v28, v29, v30, v31

        load_trans_16x4

        vwmul.vx            v8, v24, x12
        vwmul.vx            v9, v24, x13
        vwmul.vx            v10, v24, x14
        vwmul.vx            v11, v24, x15

        vwmul.vx            v12, v24, x16
        vwmul.vx            v13, v24, x17
        vwmul.vx            v14, v24, x18
        vwmul.vx            v15, v24, x19

        add_member       v28, x13, x16, x19, x25, x22, x20, x23, x26
        add_member       v25, x14, x19, x23, x21, x26, x16, x12, x17
        add_member       v29, x15, x25, x21, x19, x12, x18, x22, x24

        add_member       v26, x16, x22, x26, x12, x27, x21, x17, x15
        add_member       v30, x17, x20, x16, x18, x21, x15, x19, x22
        add_member       v27, x18, x23, x12, x22, x17, x19, x24, x13
        add_member       v31, x19, x26, x17, x24, x15, x22, x13, x20

        vsetvli         zero, zero, e32, m1, ta, ma
        vsext.vf2       v19, v0
        vsll.vi         v28, v19, 6
        vmv.v.v         v29, v28

        vsetvli    zero, zero, e16, mf2, ta, ma
        load_trans_4x4
        vwmul.vx    v30, v1, a3
        vwmul.vx    v31, v1, a5
        vwmacc.vx    v28, a2, v2

        vwmacc.vx    v29, a6, v2
        vwmacc.vx    v30, a5, v3
        vwmacc.vx    v31, a7, v3

        load_trans_8x4
        vwmul.vx    v20, v4, s6
        vwmul.vx    v21, v4, s7
        vwmul.vx    v22, v4, s8
        vwmul.vx    v23, v4, s9

        vwmacc.vx    v20, s7, v5
        vwmacc.vx    v21, s5, v5
        vwmacc.vx    v22, s2, v5
        vwmacc.vx    v23, s4, v5

        vwmacc.vx    v20, s8, v6
        vwmacc.vx    v21, s2, v6
        vwmacc.vx    v22, s9, v6
        vwmacc.vx    v23, s7, v6

        vwmacc.vx    v20, s9, v7
        vwmacc.vx    v21, s4, v7
        vwmacc.vx    v22, s7, v7
        vwmacc.vx    v23, s2, v7

        vsetvli    zero, zero, e32, m1, ta, ma
        vadd.vv             v24, v28, v30
        vadd.vv             v25, v29, v31
        vsub.vv             v26, v29, v31
        vsub.vv             v27, v28, v30

        butterfly       v24, v20, v0, v7
        butterfly       v25, v21, v1, v6
        butterfly       v26, v22, v2, v5
        butterfly       v27, v23, v3, v4

        butterfly16       v16, v31, v17, v30,  v18, v29, v19, v28,     v0, v8, v1, v9,  v2, v10, v3, v11
        butterfly16       v20, v27, v21, v26,  v22, v25, v23, v24      v4, v12, v5, v13,  v6, v14, v7, v15

.if \shift > 0
        vsetvli        zero, zero, e16, mf2, ta, ma
        scale           v0, v1, v2, v3,  v15, v14,v13, v12,      v16, v31, v17, v30,  v18, v29, v19, v28, \shift

        scale            v4, v5, v6, v7, v11, v10, v9, v8,      v20, v27, v21, v26,  v22, v25, v23, v24 , \shift

        li t0, 16*2
        addi t1, a1, 8
        addi t2, a1, 16
        addi t3, a1, 24
        vssseg4e16.v v0, (a1), t0
        vssseg4e16.v v4, (t1), t0

        vssseg4e16.v v8, (t2), t0
        vssseg4e16.v v12, (t3), t0
.else
        add             t1, t4, t5
        add             t2, t1, t5
        add             t3, t2, t5
        vse32.v         v16, (t4)
        vse32.v         v17, (t1)
        vse32.v         v18, (t2)
        vse32.v         v19, (t3)

        add             t0, t3, t5
        add             t1, t0, t5
        add             t2, t1, t5
        add             t3, t2, t5
        vse32.v         v20, (t0)
        vse32.v         v21, (t1)
        vse32.v         v22, (t2)
        vse32.v         v23, (t3)

        add             t0, t3, t5
        add             t1, t0, t5
        add             t2, t1, t5
        add             t3, t2, t5
        vse32.v         v24, (t0)
        vse32.v         v25, (t1)
        vse32.v         v26, (t2)
        vse32.v         v27, (t3)

        add             t0, t3, t5
        add             t1, t0, t5
        add             t2, t1, t5
        add             t3, t2, t5
        vse32.v         v28, (t0)
        vse32.v         v29, (t1)
        vse32.v         v30, (t2)
        vse32.v         v31, (t3)
.endif
        ret
endfunc
.endm

tr_16xN_rvv noscale, 0, 4

tr_16xN_rvv firstpass, 7, 1
tr_16xN_rvv secondpass_8, 20 - 8, 1

.macro tr_32xN_rvv name, shift
func func_tr_32xN_\name\()_rvv, zve32x
        vsetvli    zero, zero, e16, mf2, ta, ma
        load16xN_rvv

        load_trans_32x4

        tr_block1
        li t3, 0
        scale_store_rvv         \shift, 0, t3

        tr_block2
        slli    t3, t5, 2
        scale_store_rvv         \shift, 8, t3

        tr_block3
        scale_store_rvv         \shift, 16, s1

        tr_block4
        li      t0, 12
        mul     t3, t5, t0
        scale_store_rvv          \shift, 24, t3

        ret
endfunc
.endm

tr_32xN_rvv firstpass, 7
tr_32xN_rvv secondpass_8, 20 - 8

.macro idct_32x32 bitdepth
func ff_hevc_idct_32x32_\bitdepth\()_rvv, zve32x

        addi    sp, sp, -(__riscv_xlen / 8)*13
        sx  ra, (__riscv_xlen / 8)*(12)(sp)
.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        sx    s\i, (__riscv_xlen / 8)*(11-\i)(sp)
.endr
        mv t6, a0
        csrwi   vxrm, 1

        li      t0, 2048
        sub     t4, sp, t0
        sub     sp, t4, t0

        li      t0, 32
        vsetvli        t0, t0, e16, mf2, ta, ma
        slli    s0, t0, 1
        slli    t5, t0, 2
        slli    s1, t0, 5

        mv      a1, sp
1:
        jal     func_tr_16xN_noscale_rvv
        jal     func_tr_32xN_firstpass_rvv

        add     a0, a0, s0

        slli    t0, s1, 1
        add     a1, a1, t0

        addi     a3, t6, (32 * 2)
        bgt      a3, a0, 1b

        mv      a0, sp
        mv      a1, t6
1:
        jal     func_tr_16xN_noscale_rvv
        jal     func_tr_32xN_secondpass_\bitdepth\()_rvv

        add     a0, a0, s0

        slli    t0, s1, 1
        add     a1, a1, t0

        addi     a3, sp, (32 * 2)
        bgt      a3, a0, 1b

        li              t0, 4096
        add             sp,  sp,  t0

        lx  ra, (__riscv_xlen / 8)*(12)(sp)
.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        lx    s\i, (__riscv_xlen / 8)*(11-\i)(sp)
.endr
        addi    sp, sp, (__riscv_xlen / 8)*13
        ret
endfunc
.endm

idct_32x32 8

.macro idct_16x16 bitdepth
func ff_hevc_idct_16x16_\bitdepth\()_rvv, zve32x

        addi    sp, sp, -(__riscv_xlen / 8)*13
        sx  ra, (__riscv_xlen / 8)*(12)(sp)
.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        sx    s\i, (__riscv_xlen / 8)*(11-\i)(sp)
.endr
        mv t6, a0

        csrwi   vxrm, 1

        li      t0, 512
        sub     sp, sp, t0

        # li t0, 16
        vsetivli        t0, 16, e16, mf2, ta, ma

        slli    s0, t0, 1
        slli    t5, t0, 2
        slli    s1, t0, 5

        mv      a1, sp
1:
        jal     func_tr_16xN_firstpass_rvv

        add     a0, a0, s0
        add     a1, a1, s1

        addi     a3, t6, (16 * 2)
        bgt      a3, a0, 1b

        mv      a0, sp
        mv      a1, t6
1:
        jal     func_tr_16xN_secondpass_\bitdepth\()_rvv

        add     a0, a0, s0

        add     a1, a1, s1

        addi     a3, sp, (16 * 2)
        bgt      a3, a0, 1b

        add             sp,  sp, 512

        lx  ra, (__riscv_xlen / 8)*(12)(sp)
.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        lx    s\i, (__riscv_xlen / 8)*(11-\i)(sp)
.endr
        addi    sp, sp, (__riscv_xlen / 8)*13
        ret
endfunc
.endm

idct_16x16 8
