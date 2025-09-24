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

#define HEVC_MAX_PB_SIZE 64
#define AV_INPUT_BUFFER_PADDING_SIZE 64
#define HEVC_SAO_STRIDE (2 * HEVC_MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE)

const .Lhevc_sao_edge_pos, align=4
.4byte 1 // horizontal
.4byte HEVC_SAO_STRIDE // vertical
.4byte HEVC_SAO_STRIDE + 1 // 45 degree
.4byte HEVC_SAO_STRIDE - 1 // 135 degree
endconst

const .Lhevc_edge_idx, align=0
        .2byte  1,2,0,3,4
endconst

func ff_hevc_sao_edge_filter_8_rvv, zve32x
        lla             a7, .Lhevc_sao_edge_pos 
        li              t5, HEVC_SAO_STRIDE

        slli            a4, a4, 2
        add             a4, a7, a4
        lw              a4, (a4)                // stride_src

        lla             t0, .Lhevc_edge_idx
        vsetivli        zero, 5, e16, m1, ta, ma
        vle16.v         v2, (a3)                // load sao_offset_val
        vle16.v         v4, (t0)
        vrgather.vv     v6, v2, v4              // reorder to [1,2,0,3,4]

        sub             t5, t5, a5              // stride_src - width
        sub             t6, a2, a5              // stride_dst - width
1:
        mv              t4, a5
        sub             t2, a1, a4              // src_a (prev) = src - sao_edge_pos
        add             t3, a1, a4              // src_b (next) = src + sao_edge_pos
2:
        vsetvli         t1, t4, e8, m1, ta, ma
        vle8.v          v3, (a1)                // load src
        vle8.v          v4, (t2)                // load src_a (prev)
        vle8.v          v5, (t3)                // load src_b (next)
        add             a1, a1, t1
        add             t2, t2, t1
        add             t3, t3, t1

        vsetvli         zero, zero, e8, m1, ta, mu
        vmv.v.i         v1, 0
        vmsgtu.vv       v0, v3, v4              // (cur > prev)
        vmerge.vim      v17, v1, 1, v0
        vmsgtu.vv       v0, v4, v3              // (prev > cur)
        vmerge.vim      v20, v17, -1, v0        // diff0 = CMP(cur, prev) = (cur > prev) - (cur < prev)

        vmsgtu.vv       v0, v3, v5             // (cur > next)
        vmerge.vim      v19, v1, 1, v0          
        vmsgtu.vv        v0, v5, v3             // (next > cur)
        vmerge.vim      v21, v19, -1, v0        // diff1 = CMP(cur, next) = (cur > next) - (cur < next)

        vwadd.vv        v22, v20, v21           // diff = diff0 + diff1

        vsetvli         zero, zero, e16, m2, ta, ma
        vadd.vi         v20, v22, 2             // offset_val = diff + 2
        vrgather.vv     v16, v6, v20            // sao_offset_val

        vzext.vf2       v18, v3
        vsadd.vv        v20, v16, v18           // + sao_offset_val

        vsetvli         zero, zero, e16, m2, ta, mu
        li              t0, 255
        vmsgt.vx        v0, v20, t0
        vmerge.vxm      v16, v20, t0, v0        // > 255
        vmslt.vi        v0, v20, 0
        vmerge.vim      v16, v16, 0, v0         // < 0

        vsetvli         zero, zero, e8, m1, ta, ma
        vnsrl.wi        v20, v16, 0

        vse8.v          v20, (a0)
        add             a0, a0, t1
        sub             t4, t4, t1
        bnez            t4, 2b

        add             a1, a1, t5
        add             a0, a0, t6
        // no width to filter, setup next line
        addi            a6, a6, -1
        bnez            a6, 1b

        ret
endfunc