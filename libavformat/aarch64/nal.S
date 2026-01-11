/*
 * ARM NEON-optimized NAL startcode search
 * Copyright (c) 2024
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

        .arch armv8-a
        .text

function ff_nal_find_startcode_neon, export=1
        and     x2, x0, #-4              // align to 4-byte boundary
        sub     x7, x1, #3               // end -= 3
        add     x2, x2, #4               // align4 = aligned_p + 4
        mov     x3, x0                   // p = orig_p
        cmp     x0, x2
        ccmp    x7, x0, #0, cc
        bls     2f                       // skip alignment phase

        // Phase 1: align to 4-byte boundary
1:      ldrb    w0, [x3]
        cbnz    w0, 3f
        ldrb    w0, [x3, #1]
        cbnz    w0, 3f
        ldrb    w0, [x3, #2]
        cmp     w0, #1
        beq     9f                       // found 00 00 01
3:      add     x3, x3, #1
        cmp     x2, x3
        ccmp    x7, x3, #0, hi
        bhi     1b

2:      sub     x0, x7, x3               // remaining = end - p
        cmp     x0, #63
        bgt     4f                       // enter NEON phase if >= 64 bytes

        // Phase 3: byte-by-byte check for remaining data
5:      cmp     x7, x3
        bls     8f
6:      ldrb    w0, [x3]
        cbnz    w0, 7f
        ldrb    w0, [x3, #1]
        cbnz    w0, 7f
        ldrb    w0, [x3, #2]
        cmp     w0, #1
        beq     9f
7:      add     x3, x3, #1
        cmp     x7, x3
        bne     6b
8:      add     x0, x1, #3               // return orig_end + 3
        ret

        // Phase 2: NEON acceleration (64-byte blocks)
4:      sub     x8, x1, #66              // end64 = end - 66
        cmp     x8, x3
        bls     5b
        mov     w6, #65279               // 0xFEFF
        add     x5, x3, #64              // chunk_end = p + 64
        movk    w6, #0xfefe, lsl #16    // 0xFEFEFEFF
        b       1f

10:     add     x3, x3, #64              // p += 64
        add     x5, x5, #64              // chunk_end += 64
        cmp     x8, x3
        bls     5b

1:      // Load 64 bytes (4x16-byte vectors)
        ldp     q31, q30, [x3]           // load first 32 bytes
        ldp     q29, q28, [x3, #32]      // load next 32 bytes
        prfm    PLDL1KEEP, [x3, #192]    // prefetch

        // Check for zero bytes (data == 0)
        cmeq    v31.16b, v31.16b, #0     // z0
        cmeq    v30.16b, v30.16b, #0     // z1
        cmeq    v29.16b, v29.16b, #0     // z2
        cmeq    v28.16b, v28.16b, #0     // z3

        // Check for 00 pattern (current byte is 0 AND next byte is 0)
        ext     v24.16b, v31.16b, v31.16b, #1    // zs0
        ext     v27.16b, v30.16b, v30.16b, #1    // zs1
        ext     v26.16b, v29.16b, v29.16b, #1    // zs2
        ext     v25.16b, v28.16b, v28.16b, #1    // zs3

        // pattern00 = zero & zero_shift
        and     v24.16b, v24.16b, v31.16b        // p0
        and     v27.16b, v27.16b, v30.16b        // p1
        and     v26.16b, v26.16b, v29.16b        // p2
        and     v25.16b, v25.16b, v28.16b        // p3

        // Check if any 00 pattern exists (fast ORR test)
        orr     v27.16b, v24.16b, v27.16b
        orr     v25.16b, v26.16b, v25.16b
        orr     v25.16b, v25.16b, v27.16b
        dup     d31, v25.d[1]
        orr     v31.8b, v31.8b, v25.8b
        fmov    x0, d31
        cbz     x0, 10b                    // no 00 pattern, skip

        // Detailed check of this 64-byte chunk
        mov     x0, x3
2:      ldr     w2, [x0]
        add     w4, w2, w6                 // x - 0x01010101
        bic     w2, w4, w2                 // (~x) & (x - 0x01010101)
        tst     w2, #-2139062144           // & 0x80808080
        beq     3f

        ldrb    w2, [x0, #1]
        cbnz    w2, 4f
        ldrb    w4, [x0]
        ldrb    w2, [x0, #2]
        cbnz    w4, 5f
        cmp     w2, #1
        beq     9f                         // found 00 00 01
5:      ldrb    w4, [x0, #3]
        cbnz    w2, 6f
        cmp     w4, #1
        beq     11f                        // found 00 00 01 (offset +1)
        cbnz    w4, 3f
7:      ldrb    w2, [x0, #4]
        cmp     w2, #1
        beq     12f                        // found 00 00 01 (offset +2)
8:      cbnz    w2, 3f
        ldrb    w2, [x0, #5]
        cmp     w2, #1
        beq     13f                        // found 00 00 01 (offset +3)

3:      add     x0, x0, #4
        cmp     x0, x5
        bne     2b
        b       10b

4:      ldrb    w2, [x0, #3]
        cbnz    w2, 3b
        ldrb    w2, [x0, #2]
        cbz     w2, 7b
        ldrb    w2, [x0, #4]
        b       8b

6:      cbnz    w4, 3b
        ldrb    w2, [x0, #4]
        b       8b

9:      mov     x0, x3
        ret

11:     add     x0, x0, #1
        ret

12:     add     x0, x0, #2
        ret

13:     add     x0, x0, #3
        ret
endfunc
