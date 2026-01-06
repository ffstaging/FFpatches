/*
 * Copyright (c) 2026 Zhao Zhili <zhilizhao@tencent.com>
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

function ff_add_bytes_l2_neon, export=1
        bic             w4, w3, #63
        and             w3, w3, #63
        cbz             w4, 2f
1:
        // 64 bytes per loop iteration
        ld1             {v0.16b - v3.16b}, [x1], #64
        ld1             {v4.16b - v7.16b}, [x2], #64
        subs            w4, w4, #64
        add             v0.16b, v0.16b, v4.16b
        add             v1.16b, v1.16b, v5.16b
        add             v2.16b, v2.16b, v6.16b
        add             v3.16b, v3.16b, v7.16b
        st1             {v0.16b - v3.16b}, [x0], #64
        b.ne            1b
2:
        bic             w4, w3, #15
        and             w3, w3, #15
        cbz             w4, 4f
3:
        // 16 bytes per loop iteration
        ld1             {v0.16b}, [x1], #16
        ld1             {v4.16b}, [x2], #16
        subs            w4, w4, #16
        add             v0.16b, v0.16b, v4.16b
        st1             {v0.16b}, [x0], #16
        b.ne            3b
4:
        cbz             w3, 6f
5:
        ldrb            w5, [x1], #1
        ldrb            w6, [x2], #1
        subs            w3, w3, #1
        add             w5, w5, w6
        strb            w5, [x0], #1
        b.ne            5b
6:
        ret
endfunc

.macro add_png_paeth_prediction, bpp, load=0
.if \load
        ldr             q0, [x0, #(-\bpp)]          // a = dst[i - bpp]
        ldr             q1, [x2, #(-\bpp)]          // c = top[i - bpp]
        ld1             {v2.16b}, [x2]              // b = top[i]
        ld1             {v18.16b}, [x1]
.else
        mov             v1.16b, v2.16b
        ext             v2.16b, v2.16b, v2.16b, #\bpp
        ext             v18.16b, v18.16b, v18.16b, #\bpp
.endif

        uabd            v4.8b, v2.8b, v1.8b         // pa = abs(b - c)
        uaddl           v7.8h, v1.8b, v1.8b         // 2 * c
        uabd            v3.8b, v0.8b, v1.8b         // pb = abs(a - c)
        uaddl           v5.8h, v0.8b, v2.8b         // a + b

        cmhs            v16.8b, v3.8b, v4.8b        // pb >= pa
        uabd            v5.8h, v5.8h, v7.8h
        umin            v6.8b, v4.8b, v3.8b         // min(pa, pb)
        uqxtn           v5.8b, v5.8h

        bsl             v16.8b, v0.8b, v2.8b        // pb >= pa ? a : b
        cmhs            v6.8b, v5.8b, v6.8b         // pc >= min(pa, pb)
        bsl             v6.8b, v16.8b, v1.8b        // pc >= min ? (a or b) : c

        add             v0.8b, v6.8b, v18.8b
.if \bpp == 3 || \bpp == 4
        str             s0, [x0], #\bpp
.else
        str             d0, [x0], #\bpp
.endif
.endm

function ff_add_png_paeth_prediction_neon, export=1
        cmp             w4, #3
        b.gt            40f
30:     // bpp = 3
        // 15 bytes per loop
        // overread and write one byte, so (w3 - 1) first
        sub             w5, w3, #1
        mov             w7, #15
        udiv            w5, w5, w7
        mul             w5, w5, w7
        sub             w3, w3, w5
        cbz             w5, 2f
31:
        add_png_paeth_prediction 3, load=1
        subs            w5, w5, w7
        add             x1, x1, x7
        add             x2, x2, x7
.rept   4
        add_png_paeth_prediction 3
.endr
        b.ne            31b
        b               2f

40:     // bpp = 4
        cmp             w4, #4
        b.gt            60f
        // 16 bytes per loop
        bic             w5, w3, #15
        and             w3, w3, #15
        cbz             w5, 2f
41:
        add_png_paeth_prediction 4, load=1
        subs            w5, w5, #16
        add             x1, x1, #16
        add             x2, x2, #16
.rept   3
        add_png_paeth_prediction 4
.endr
        b.ne            41b
        b               2f

60:     // bpp = 6
        cmp             w4, #6
        b.gt            80f
        // 12 bytes per loop
        // overread 4 bytes, overwrite 2 byte, (w3 - 4) first
        sub             w5, w3, #4
        mov             w7, #12
        udiv            w5, w5, w7
        mul             w5, w5, w7
        sub             w3, w3, w5
        cbz             w5, 2f
61:
        add_png_paeth_prediction 6, load=1
        add_png_paeth_prediction 6
        subs            w5, w5, w7
        add             x1, x1, x7
        add             x2, x2, x7
        b.ne            61b
        b               2f

80:     // 16 bytes per loop
        bic             w5, w3, #15
        and             w3, w3, #15
        cbz             w5, 2f
81:
        add_png_paeth_prediction 8, load=1
        add_png_paeth_prediction 8
        subs            w5, w5, #16
        add             x1, x1, #16
        add             x2, x2, #16
        b.ne            81b
2:
        neg             w6, w4                      // -bpp
        cbz             w3, 8f
3:
        ldrb            w7, [x0, w6, sxtw]          // a = dst[i - bpp]
        ldrb            w9, [x2, w6, sxtw]          // c = top[i - bpp]
        ldrb            w8, [x2], #1                // b = top[i]

        sub             w10, w8, w9                 // p = b - c
        sub             w11, w7, w9                 // a - c

        cmp             w10, #0
        cneg            w12, w10, lt                // pa = abs(b -c)
        cmp             w11, #0
        add             w14, w10, w11
        cneg            w13, w11, lt                // pb = abs(a - c)
        cmp             w14, #0
        cneg            w14, w14, lt                // pc = abs(a + b - 2*c)

        ldrb            w16, [x1], #1

        cmp             w12, w13
        b.gt            4f
        cmp             w12, w14
        b.gt            4f
        mov             w15, w7
        b               5f
4:
        cmp             w13, w14
        csel            w15, w8, w9, le
5:
        subs            w3, w3, #1
        add             w15, w15, w16
        strb            w15, [x0], #1
        b.ne            3b
8:
        ret
endfunc
