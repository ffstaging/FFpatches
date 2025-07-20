/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright © 2025 Tristan Matthews
 * Partly based on h264_pred.c Copyright (c) 2023 SiFive, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "libavutil/riscv/asm.S"

func ff_pred8x8_horizontal_8_rvv_vl256, zve32x
        lpad    0
        # mf4 only for VLEN >= 256, we only need to load 8 elements not all 32 elements so use mf4
        vsetivli zero, 8, e8, mf4, ta
        j ff_pred8x8_horizontal_8_main
endfunc

func ff_pred8x8_horizontal_8_rvv, zve32x
        lpad    0
        vsetivli zero, 8, e8, mf2, ta
ff_pred8x8_horizontal_8_main:
        add     t0, a1, a1
        mv      t1, a0
        add     t2, a0, a1

        .rept 3
        lb      t3, -1(t1)             # Load left pixel of row 0
        lb      t4, -1(t2)             # Load left pixel of row 1 (doing 2 loads up front since they may stall)
        vmv.v.x v0, t3
        vmv.v.x v1, t4
        vse8.v  v0, (t1)
        add     t1, t1, t0
        vse8.v  v1, (t2)
        add     t2, t2, t0
        .endr

        lb      t3, -1(t1)
        lb      t4, -1(t2)
        vmv.v.x v0, t3
        vmv.v.x v1, t4
        vse8.v  v0, (t1)
        vse8.v  v1, (t2)

        ret
endfunc

func ff_pred8x8_plane_8_rvv, zve32x
        lpad    0
        vsetivli zero, 4, e8, mf2, ta

        vid.v   v0
        vmv.v.i v1, 3
        vsub.vv v2, v1, v0                   # v2 = {3, 2, 1, 0} aka index

        vsetivli zero, 4, e16, mf2, ta

        vid.v   v4
        vadd.vi v6, v4, 1                    # v6 = {1..4} = weight1

        vsetivli zero, 4, e8, mf2, ta

        sub     t0, a0, a1
        addi    t1, t0, 4
        vle8.v  v8, (t1)

        addi    t2, t0, -1
        vle8.v  v9, (t2)

        vrgather.vv v10, v9, v2

        slli    t3, a1, 2
        addi    t4, t3, -1
        add     t5, a0, t4

        vlse8.v v11, (t5), a1
        vlse8.v v12, (t2), a1
        vrgather.vv v13, v12, v2

        vsetivli zero, 4, e16, mf2, ta

        vzext.vf2 v14, v8
        vzext.vf2 v15, v10

        vzext.vf2 v16, v11
        vzext.vf2 v17, v13

        vsub.vv v18, v14, v15
        vmul.vv v19, v18, v6

        vsub.vv v20, v16, v17
        vmul.vv v21, v20, v6

        vmv.v.x v22, zero
        vwredsum.vs v22, v21, v22

        vmv.v.x v23, zero
        vwredsum.vs v23, v19, v23

        vmv.x.s t6, v23
        slli t1, t6, 4
        add  t6, t6, t1
        addi t6, t6, 16
        srai t6, t6, 5

        vmv.x.s t5, v22
        slli t1, t5, 4
        add  t5, t5, t1
        addi t5, t5, 16
        srai t5, t5, 5

        add t2, t6, t5
        slli t3, t2, 1
        add t3, t3, t2

        slli a2, a1, 3
        sub a2, a2, a1
        addi a2, a2, -1
        add a2, a2, a0
        lbu  a3, (a2)

        addi a2, a0, 7
        sub a2, a2, a1
        lbu a4, (a2)

        add a5, a4, a3
        addi a5, a5, 1
        slli a5, a5, 4

        sub a5, a5, t3                       # linear combination of H, V and src

        vsetivli zero, 8, e16, mf2, ta
        vid.v   v7

        vmv.v.x v18, t6
        vmul.vv v18, v18, v7

        .irp reg 19, 20, 21, 22, 23, 24, 25, 26
        vadd.vx v\reg, v18, a5
        vmax.vx v\reg, v\reg, zero
        add a5, a5, t5
        .endr

        li         t2, 255

        .irp reg 19, 20, 21, 22, 23, 24, 25, 26
        vsra.vi    v\reg, v\reg, 5
        vmin.vx    v\reg, v\reg, t2          # clamp to unsigned 255
        .endr

        vsetivli zero, 8, e8, mf2, ta

        .irp reg 19, 20, 21, 22, 23, 24, 25, 26
        vnclipu.wi v\reg, v\reg, 0
        .endr

        vse8.v  v19, (a0)
        add t1, a0, a1

        .irp reg 20, 21, 22, 23, 24, 25
        vse8.v  v\reg, (t1)
        add t1, t1, a1
        .endr

        vse8.v  v26, (t1)

        ret
endfunc

func ff_pred16x16_horizontal_8_rvv, zve32x
        lpad    0
        vsetivli zero, 16, e8, mf2, ta
        add     t0, a1, a1
        mv      t1, a0
        add     t2, a0, a1

        .rept 7
        lb      t3, -1(t1)             # Load left pixels of rows 0,1 (doing 2 loads up front since they may stall)
        lb      t4, -1(t2)
        vmv.v.x v0, t3
        vmv.v.x v1, t4
        vse8.v  v0, (t1)
        add     t1, t1, t0
        vse8.v  v1, (t2)
        add     t2, t2, t0
        .endr

        lb      t3, -1(t1)
        lb      t4, -1(t2)
        vmv.v.x v0, t3
        vmv.v.x v1, t4
        vse8.v  v0, (t1)
        vse8.v  v1, (t2)

        ret
endfunc

func ff_pred16x16_vertical_8_rvv, zve32x
        lpad    0
        vsetivli zero, 16, e8, mf2, ta

        sub     t0, a0, a1

        vle8.v  v0, (t0)

        vse8.v  v0, (a0)

        add t1, a0, a1
        vse8.v  v0, (t1)

        .rept 14
        add t1, t1, a1
        vse8.v  v0, (t1)
        .endr

        ret
endfunc

func ff_pred16x16_128_dc_8_rvv, zve32x
        lpad    0
        vsetivli zero, 16, e8, mf2, ta
        li t0, 128
        vmv.v.x v0, t0

        slli a2, a1, 1
        vse8.v  v0, (a0)

        add t2, a0, a1
        vse8.v  v0, (t2)

        add t1, a0, a2
        add t2, t2, a2

        .rept 6
        vse8.v  v0, (t1)
        vse8.v  v0, (t2)

        add t1, t1, a2
        add t2, t2, a2

        .endr

        vse8.v  v0, (t1)
        vse8.v  v0, (t2)

        ret
endfunc

func ff_pred16x16_dc_8_rvv, zve32x
        lpad    0
        vsetivli zero, 16, e8, mf2, ta, ma
        csrw vxrm, 0

        addi t0, a0, -1
        vlse8.v v0, (t0), a1

        sub     t1, a0, a1
        vle8.v  v1, (t1)

        vsetivli zero, 8, e8, mf4, ta, ma
        vmv.v.i v2, 0

        vsetivli zero, 16, e8, mf2, ta, ma
        vwredsumu.vs v2, v0, v2 # sum = sum(left)
        vwredsumu.vs v2, v1, v2 # sum += sum(top)

        li t2, 5
        vsetivli        zero, 8, e16, mf4, ta, ma
        vssrl.vx        v3, v2, t2

        vsetivli        zero, 16, e8, mf2, ta, ma
        vrgather.vi     v4, v3, 0

        vse8.v v4, (a0)
        add t1, a0, a1

        .rept 15
        vse8.v v4, (t1)
        add t1, t1, a1
        .endr

        ret
endfunc


func ff_pred16x16_left_dc_8_rvv, zve32x
        lpad    0
        vsetivli zero, 16, e8, mf2, ta, ma

        addi t2, a0, -1
        vlse8.v v1, (t2), a1

        vmv.v.i v2, 0
        vwredsumu.vs v2, v1, v2

        vsetivli zero, 8, e16, mf4, ta, ma
        vadd.vi v2, v2, 8
        vsrl.vi v3, v2, 4

        vsetivli zero, 16, e8, mf2, ta, ma
        vmv.x.s t3, v3
        vmv.v.x v4, t3

        slli a2, a1, 1
        add t4, a0, a1

        vse8.v v4, (a0)
        vse8.v v4, (t4)
        add t1, a0, a2
        add t4, t4, a2

        .rept 6
        vse8.v v4, (t1)
        vse8.v v4, (t4)
        add t1, t1, a2
        add t4, t4, a2
        .endr

        vse8.v v4, (t1)
        vse8.v v4, (t4)

        ret
endfunc

func ff_pred16x16_top_dc_8_rvv, zve32x
        lpad    0
        vsetivli zero, 16, e8, mf2, ta, ma
        csrw vxrm, 0

        sub     t0, a0, a1
        vle8.v  v0, (t0)

        vmv.v.i v1, 0

        vsetivli zero, 16, e8, m1, ta, ma

        vwredsumu.vs v1, v0, v1

        li t1, 4
        vsetivli        zero, 8, e16, mf4, ta, ma
        vssrl.vx        v1, v1, t1

        vsetivli        zero, 16, e8, mf2, ta, ma
        vrgather.vi     v2, v1, 0

        vse8.v v2, (a0)
        slli a2, a1, 1
        add t1, a0, a2
        add t2, a0, a1
        vse8.v v2, (t2)

        add t2, t2, a2
        vse8.v v2, (t1)
        vse8.v v2, (t2)

        .rept 6
        add t1, t1, a2
        add t2, t2, a2
        vse8.v v2, (t1)
        vse8.v v2, (t2)
        .endr

        ret
endfunc

func ff_pred16x16_plane_8_rvv, zve32x
        lpad    0
        vsetivli zero, 8, e8, mf2, ta

        vid.v   v0
        vmv.v.i v1, 7
        vsub.vv v2, v1, v0                   # v2 = {7, 6, 5, 4, 3, 2, 1, 0} aka index

        sub     t0, a0, a1
        addi    t1, t0, 8
        addi    t2, t0, -1
        vle8.v  v3, (t1)
        vle8.v  v4, (t2)
        vrgather.vv v5, v4, v2

        slli    t3, a1, 3
        add     t3, a0, t3
        addi    t3, t3, -1

        vlse8.v v6, (t3), a1
        vlse8.v v7, (t2), a1
        vrgather.vv v8, v7, v2

        vsetivli zero, 8, e16, m1, ta

        vzext.vf2 v9, v3
        vzext.vf2 v10, v5

        vzext.vf2 v11, v6
        vzext.vf2 v12, v8

        vsub.vv v13, v9, v10
        vid.v   v14
        vadd.vi v14, v14, 1
        vmul.vv v15, v13, v14

        vsub.vv v16, v11, v12
        vmul.vv v17, v16, v14

        vmv.v.x v18, zero
        vwredsum.vs v18, v17, v18

        vmv.v.x v19, zero
        vwredsum.vs v19, v15, v19

        vmv.x.s t4, v19
        slli t1, t4, 2
        add t1, t1, t4
        addi t1, t1, 32
        srai t1, t1, 6

        vmv.x.s t5, v18
        slli t2, t5, 2
        add t2, t2, t5
        addi t2, t2, 32
        srai t2, t2, 6

        add t3, t1, t2
        slli t4, t3, 3
        sub t4, t4, t3

        slli t5, a1, 4
        sub t5, t5, a1
        addi t5, t5, -1

        li t6, 15
        sub t6, t6, a1
        add t6, a0, t6
        lbu a2, (t6)

        add a3, a0, t5
        lbu a4, (a3)

        add a4, a4, a2
        addi a4, a4, 1
        slli a4, a4, 4

        sub a5, a4, t4                       # a5 = linear combination of H, V and src

        vsetivli zero, 16, e16, m1, ta
        vid.v v20

        vmv.v.x v21, t1
        vmul.vv v22, v21, v20

        mv t3, a0

        .rept 2

        vsetivli zero, 16, e16, m1, ta

        .irp reg 23, 24, 25, 26, 27, 28, 29, 30
        vadd.vx v\reg, v22, a5
        vmax.vx v\reg, v\reg, zero
        add a5, a5, t2
        .endr

        li a6, 255

        .irp reg, 23, 24, 25, 26, 27, 28, 29, 30
        vsra.vi v\reg, v\reg, 5
        vmin.vx v\reg, v\reg, a6
        .endr

        vsetivli zero, 16, e8, mf2, ta

        .irp reg, 23, 24, 25, 26, 27, 28, 29, 30
        vnclipu.wi v\reg, v\reg, 0
        vse8.v  v\reg, (t3)
        add t3, t3, a1
        .endr

        .endr

        ret
endfunc
