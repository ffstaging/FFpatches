/*
 * Copyright (c) 2025 Georgii Zagoruiko <georgii.zaguri@gmail.com>
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

#include "libavutil/aarch64/asm.S"

#define VVC_MAX_PB_SIZE 128

#if HAVE_SME
ENABLE_SME

function ff_vvc_put_luma_h_8_sme, export=1
        // dst           .req x0
        // _src          .req x1
        // _src_stride   .req x2
        // height        .req w3
        // hf            .req x4
        // vf            .req x5
        // width         .req w6
        smstart
        cntb            x8
        mov             x9, #(VVC_MAX_PB_SIZE * 2)
        mov             w13, #0
        mov             w14, #1
        mov             w15, #2
        mov             w16, #3
        ptrue           p0.b, VL8
        ptrue           p1.s
        ld1b            z30.b, p0/Z, [x4]
        eor             z0.b, z0.b, z0.b
        mov             z31.d, z30.d
        sub             x1, x1, #3
        ext             z31.b, z31.b, z0.b, #4
.Loop_H:
        cmp             w6, w8
        csel            w11, w6, w8, ls
        whilelo         p0.b, xzr, x6
        mov             w10, w3
        asr             w12, w11, #1
        whilelo         p2.h, xzr, x12
.Loop_W:
        ld1b            z0.b, p0/z, [x1]
        ld1b            z1.b, p0/z, [x1, x14]
        ld1b            z2.b, p0/z, [x1, x15]
        ld1b            z3.b, p0/z, [x1, x16]
        add             x1, x1, #4
        ld1b            z4.b, p0/z, [x1]
        ld1b            z5.b, p0/z, [x1, x14]
        ld1b            z6.b, p0/z, [x1, x15]
        ld1b            z7.b, p0/z, [x1, x16]
        sub             x1, x1, #4
        usmopa          za0.s, p0/m, p0/m, z0.b, z30.b
        usmopa          za1.s, p0/m, p0/m, z1.b, z30.b
        usmopa          za2.s, p0/m, p0/m, z2.b, z30.b
        usmopa          za3.s, p0/m, p0/m, z3.b, z30.b
        usmopa          za0.s, p0/m, p0/m, z4.b, z31.b
        usmopa          za1.s, p0/m, p0/m, z5.b, z31.b
        usmopa          za2.s, p0/m, p0/m, z6.b, z31.b
        usmopa          za3.s, p0/m, p0/m, z7.b, z31.b
        mova            z22.s, p1/m, za0v.s[w13, 0]
        mova            z24.s, p1/m, za1v.s[w13, 0]
        mova            z26.s, p1/m, za2v.s[w13, 0]
        mova            z28.s, p1/m, za3v.s[w13, 0]
        add             x1, x1, x2
        zero            {za}
        ld1b            z0.b, p0/z, [x1]
        ld1b            z1.b, p0/z, [x1, x14]
        ld1b            z2.b, p0/z, [x1, x15]
        ld1b            z3.b, p0/z, [x1, x16]
        add             x1, x1, #4
        ld1b            z4.b, p0/z, [x1]
        ld1b            z5.b, p0/z, [x1, x14]
        ld1b            z6.b, p0/z, [x1, x15]
        ld1b            z7.b, p0/z, [x1, x16]
        sub             x1, x1, #4
        sqxtnb          z21.h, z22.s
        sqxtnb          z22.h, z24.s
        sqxtnt          z21.h, z26.s
        sqxtnt          z22.h, z28.s
        st2h            {z21.h-z22.h}, p2, [x0]
        add             x1, x1, x2
        add             x0, x0, x9

        usmopa          za0.s, p0/m, p0/m, z0.b, z30.b
        usmopa          za1.s, p0/m, p0/m, z1.b, z30.b
        usmopa          za2.s, p0/m, p0/m, z2.b, z30.b
        usmopa          za3.s, p0/m, p0/m, z3.b, z30.b
        usmopa          za0.s, p0/m, p0/m, z4.b, z31.b
        usmopa          za1.s, p0/m, p0/m, z5.b, z31.b
        usmopa          za2.s, p0/m, p0/m, z6.b, z31.b
        usmopa          za3.s, p0/m, p0/m, z7.b, z31.b
        mova            z22.s, p1/m, za0v.s[w13, 0]
        mova            z24.s, p1/m, za1v.s[w13, 0]
        mova            z26.s, p1/m, za2v.s[w13, 0]
        mova            z28.s, p1/m, za3v.s[w13, 0]
        sqxtnb          z21.h, z22.s
        sqxtnb          z22.h, z24.s
        sqxtnt          z21.h, z26.s
        sqxtnt          z22.h, z28.s
        zero            {za}
        st2h            {z21.h-z22.h}, p2, [x0]
        subs            w10, w10, #2
        add             x0, x0, x9
        b.gt            .Loop_W
        msub            x0, x3, x9, x0
        msub            x1, x3, x2, x1
        add             x0, x0, x11, lsl #1
        subs            w6, w6, w11
        add             x1, x1, x11
        b.gt            .Loop_H
        smstop
        ret
endfunc

DISABLE_SME
#endif
