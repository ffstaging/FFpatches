/*
 * Copyright (c) 2026 Zhao Zhili <quinkblack@foxmail.com>
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

function ff_pred8x8_dc_aarch64, export=1
        sub             x9,  x0,  x1            // top pointer
        lsl             x11, x1,  #2            // stride * 4
        add             x14, x0,  x1            // src + stride
        add             x12, x0,  x11           // src + stride*4

        ldurb           w8,  [x0, #-1]          // left[0]
        add             x11, x14, x11           // src + stride*5
        ldrb            w7,  [x9]               // top[0]
        lsl             x16, x1,  #1            // stride * 2
        ldrb            w10, [x9, #4]           // top[4], dc1 init
        ldurb           w13, [x12, #-1]         // left[4], dc2 init

        add             x17, x0,  x16           // src + stride*2
        add             x16, x14, x16           // src + stride*3

        add             w8,  w8,  w7            // dc0 = left[0] + top[0]

        ldurb           w15, [x14, #-1]         // left[1]
        ldrb            w2,  [x9, #1]           // top[1]
        ldrb            w4,  [x9, #5]           // top[5]
        add             w15, w15, w2
        add             w10, w10, w4            // dc1 += top[5]
        add             w8,  w8,  w15           // dc0 += left[1] + top[1]

        ldurb           w15, [x11, #-1]         // left[5]
        ldurb           w2,  [x17, #-1]         // left[2]
        ldrb            w4,  [x9, #2]           // top[2]
        ldrb            w5,  [x9, #6]           // top[6]
        add             w2,  w2,  w4
        add             w8,  w8,  w2            // dc0 += left[2] + top[2]

        mov             w3,  #6
        madd            x3,  x1,  x3,  x0       // src + stride*6
        ldurb           w6,  [x3, #-1]          // left[6]

        ldurb           w2,  [x16, #-1]         // left[3]
        ldrb            w4,  [x9, #3]           // top[3]
        ldrb            w7,  [x9, #7]           // top[7]
        add             w2,  w2,  w4
        add             w8,  w8,  w2            // dc0 += left[3] + top[3]

        add             w5,  w5,  w7
        add             w10, w10, w5            // dc1 += top[6] + top[7]

        add             x9,  x9,  x1, lsl #3    // src + stride*7
        ldurb           w1,  [x9, #-1]          // left[7]

        add             w13, w13, w15           // dc2 += left[5]
        add             w13, w13, w1            // dc2 += left[7]
        add             w13, w13, w6            // dc2 += left[6]

        add             w8,  w8,  #4
        add             w2,  w13, w10           // dc3_sum = dc2_sum + dc1_sum
        lsr             w8,  w8,  #3            // dc0 >> 3

        add             w1,  w10, #2            // dc1_sum + 2
        add             w2,  w2,  #4            // dc3_sum + 4
        mov             w15, #0x01010101
        lsr             w1,  w1,  #2            // dc1 >> 2

        mul             w8,  w8,  w15           // dc0 splat
        lsr             w2,  w2,  #3            // dc3 >> 3
        mul             w1,  w1,  w15           // dc1 splat

        add             w13, w13, #2            // dc2_sum + 2
        mul             w10, w2,  w15           // dc3 splat

        stp             w8,  w1,  [x0]
        lsr             w13, w13, #2            // dc2 >> 2
        stp             w8,  w1,  [x14]
        mul             w13, w13, w15           // dc2 splat
        stp             w8,  w1,  [x17]
        stp             w8,  w1,  [x16]
        stp             w13, w10, [x12]
        stp             w13, w10, [x11]
        stp             w13, w10, [x3]
        stp             w13, w10, [x9]

        ret
endfunc

