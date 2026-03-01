/*
 * Copyright (c) 2025
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/aarch64/asm.S"

// Add int16 array with masking
// On entry:
//   x0 -> destination array (uint16_t*)
//   x1 -> source array (const uint16_t*)
//   w2  = mask value
//   w3  = number of elements
function ff_add_int16_neon, export=1
        dup             v4.8h, w2

        // Process 32 elements (64 bytes) if available
1:      cmp             w3, #32
        b.lt            2f
        ld1             {v0.8h, v1.8h}, [x1], #32
        ld1             {v2.8h, v3.8h}, [x0]
        add             v0.8h, v0.8h, v2.8h
        add             v1.8h, v1.8h, v3.8h
        and             v0.16b, v0.16b, v4.16b
        and             v1.16b, v1.16b, v4.16b
        st1             {v0.8h, v1.8h}, [x0], #32

        ld1             {v0.8h, v1.8h}, [x1], #32
        ld1             {v2.8h, v3.8h}, [x0]
        add             v0.8h, v0.8h, v2.8h
        add             v1.8h, v1.8h, v3.8h
        and             v0.16b, v0.16b, v4.16b
        and             v1.16b, v1.16b, v4.16b
        st1             {v0.8h, v1.8h}, [x0], #32
        sub             w3, w3, #32
        b               1b

        // Process 8 elements (16 bytes) if available
2:      cmp             w3, #8
        b.lt            3f
        ld1             {v0.8h}, [x1], #16
        ld1             {v1.8h}, [x0]
        add             v0.8h, v0.8h, v1.8h
        and             v0.16b, v0.16b, v4.16b
        st1             {v0.8h}, [x0], #16
        sub             w3, w3, #8
        b               2b

        // Scalar path for remaining elements
3:      cbz             w3, 4f
        ldrh            w5, [x1], #2
        ldrh            w6, [x0]
        add             w5, w5, w6
        and             w5, w5, w2
        strh            w5, [x0], #2
        subs            w3, w3, #1
        b.ne            3b

4:      ret
endfunc
