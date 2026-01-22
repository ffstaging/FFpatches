/*
 * Copyright (c) 2024 Ramiro Polla
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

function ff_pix_sum16_neon, export=1
// x0  const uint8_t *pix
// x1  ptrdiff_t line_size

        add             x2, x0, x1
        lsl             x1, x1, #1
        movi            v0.16b, #0
        mov             w3, #16

1:
        ld1             {v1.16b}, [x0], x1
        ld1             {v2.16b}, [x2], x1
        subs            w3, w3, #2
        uadalp          v0.8h, v1.16b
        uadalp          v0.8h, v2.16b
        b.ne            1b

        uaddlv          s0, v0.8h
        fmov            w0, s0

        ret
endfunc

function ff_pix_norm1_neon, export=1
// x0  const uint8_t *pix
// x1  ptrdiff_t line_size

        movi            v0.16b, #0
        mov             w2, #16

1:
        ld1             {v1.16b}, [x0], x1
        subs            w2, w2, #1
        umull           v2.8h, v1.8b,  v1.8b
        umull2          v3.8h, v1.16b, v1.16b
        uadalp          v0.4s, v2.8h
        uadalp          v0.4s, v3.8h
        b.ne            1b

        uaddlv          d0, v0.4s
        fmov            w0, s0

        ret
endfunc

#if HAVE_DOTPROD
ENABLE_DOTPROD

function ff_pix_norm1_neon_dotprod, export=1
// x0  const uint8_t *pix
// x1  ptrdiff_t line_size

        movi            v0.16b, #0
        mov             w2, #16

1:
        ld1             {v1.16b}, [x0], x1
        ld1             {v2.16b}, [x0], x1
        udot            v0.4s, v1.16b, v1.16b
        subs            w2, w2, #2
        udot            v0.4s, v2.16b, v2.16b
        b.ne            1b

        uaddlv          d0, v0.4s
        fmov            w0, s0

        ret
endfunc

DISABLE_DOTPROD
#endif

function ff_draw_edges_neon, export=1
    mov     x9, x0
    mov     x10, x1
    uxtw    x11, w2
    uxtw    x12, w3
    uxtw    x13, w4
    uxtw    x14, w5
    uxtw    x15, w6
    mov     x0, x9
    mov     x1, x12

    cmp     w13, #16
    b.eq    1f
    cmp     w13, #8
    b.eq    2f
    b       3f

1:
    ldrb    w2, [x0]
    dup     v0.16b, w2
    sub     x3, x0, #16
    st1     {v0.16b}, [x3]

    add     x3, x0, x11
    sub     x4, x3, #1
    ldrb    w2, [x4]
    dup     v1.16b, w2
    st1     {v1.16b}, [x3]

    add     x0, x0, x10
    subs    x1, x1, #1
    b.ne    1b
    b       4f

2:
    ldrb    w2, [x0]
    dup     v0.8b, w2
    sub     x3, x0, #8
    st1     {v0.8b}, [x3]

    add     x3, x0, x11
    sub     x4, x3, #1
    ldrb    w2, [x4]
    dup     v1.8b, w2
    st1     {v1.8b}, [x3]

    add     x0, x0, x10
    subs    x1, x1, #1
    b.ne    2b
    b       4f

3:
    ldrb    w2, [x0]
    dup     v0.8b, w2
    sub     x3, x0, #4
    st1     {v0.s}[0], [x3]

    add     x3, x0, x11
    sub     x4, x3, #1
    ldrb    w2, [x4]
    dup     v1.8b, w2
    st1     {v1.s}[0], [x3]

    add     x0, x0, x10
    subs    x1, x1, #1
    b.ne    3b

4:
    sub     x9, x9, x13
    tbnz    w15, #0, 5f
    b       6f

5:
    add     x2, x11, x13, lsl #1
    mov     x3, x14
    mov     x4, x9
    sub     x5, x9, x10

7:
    mov     x6, x5
    mov     x7, x4
    mov     x8, x2

8:
    cmp     x8, #64
    b.lt    17f
    ld1     {v0.16b, v1.16b, v2.16b, v3.16b}, [x7], #64
    st1     {v0.16b, v1.16b, v2.16b, v3.16b}, [x6], #64
    sub     x8, x8, #64
    b       8b

17:
    cmp     x8, #16
    b.lt    9f
    ld1     {v0.16b}, [x7], #16
    st1     {v0.16b}, [x6], #16
    sub     x8, x8, #16
    b       17b
9:
    cbz     x8, 10f
    ldrb    w16, [x7], #1
    strb    w16, [x6], #1
    sub     x8, x8, #1
    b       9b
10:
    sub     x5, x5, x10
    subs    x3, x3, #1
    b.ne    7b

6:
    tbnz    w15, #1, 11f
    b       12f

11:
    sub     x3, x12, #1
    mul     x3, x3, x10
    add     x4, x9, x3
    add     x5, x4, x10

    add     x2, x11, x13, lsl #1
    mov     x3, x14

13:
    mov     x6, x5
    mov     x7, x4
    mov     x8, x2

14:
    cmp     x8, #64
    b.lt    18f
    ld1     {v0.16b, v1.16b, v2.16b, v3.16b}, [x7], #64
    st1     {v0.16b, v1.16b, v2.16b, v3.16b}, [x6], #64
    sub     x8, x8, #64
    b       14b

18:
    cmp     x8, #16
    b.lt    15f
    ld1     {v0.16b}, [x7], #16
    st1     {v0.16b}, [x6], #16
    sub     x8, x8, #16
    b       18b
15:
    cbz     x8, 16f
    ldrb    w16, [x7], #1
    strb    w16, [x6], #1
    sub     x8, x8, #1
    b       15b
16:
    add     x5, x5, x10
    subs    x3, x3, #1
    b.ne    13b

12:
    ret
endfunc
