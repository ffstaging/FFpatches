/*
 * HEVC Intra Prediction NEON optimizations
 *
 * Copyright (c) 2026 FFmpeg Project
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

/* HEVC Intra Prediction functions
 *
 * Function signatures (different from H264):
 * pred_dc:      void (uint8_t *src, const uint8_t *top, const uint8_t *left,
 *                     ptrdiff_t stride, int log2_size, int c_idx)
 * pred_planar:  void (uint8_t *src, const uint8_t *top, const uint8_t *left,
 *                     ptrdiff_t stride)
 * pred_angular: void (uint8_t *src, const uint8_t *top, const uint8_t *left,
 *                     ptrdiff_t stride, int c_idx, int mode)
 */

// =============================================================================
// DC Prediction
// =============================================================================

/*
 * DC prediction algorithm:
 * 1. dc = sum(top[0..size-1]) + sum(left[0..size-1]) + size
 * 2. dc >>= (log2_size + 1)
 * 3. Fill block with dc value
 * 4. If c_idx == 0 && size < 32: smooth edges
 *    - POS(0,0) = (left[0] + 2*dc + top[0] + 2) >> 2
 *    - First row: (top[x] + 3*dc + 2) >> 2
 *    - First col: (left[y] + 3*dc + 2) >> 2
*/

// -----------------------------------------------------------------------------
// pred_dc_4x4_8: DC prediction
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// w4: c_idx
// -----------------------------------------------------------------------------
function ff_hevc_pred_dc_4x4_8_neon, export=1
        // Load top[0..3] and left[0..3]
        ldr             s0, [x1]                // top[0..3]
        ldr             s1, [x2]                // left[0..3]

        // Sum using NEON
        uaddlv          h2, v0.8b               // sum top (only 4 valid bytes)
        uaddlv          h3, v1.8b               // sum left (only 4 valid bytes)
        add             v2.4h, v2.4h, v3.4h     // total sum

        // Add rounding (4) and shift by 3
        movi            v3.4h, #4
        add             v2.4h, v2.4h, v3.4h
        ushr            v2.4h, v2.4h, #3        // >> 3
        dup             v2.8b, v2.b[0]          // broadcast dc

        // Store 4 rows
        str             s2, [x0]
        str             s2, [x0, x3]
        add             x5, x0, x3, lsl #1
        str             s2, [x5]
        str             s2, [x5, x3]

        // Edge smoothing for luma only
        cbnz            w4, 9f

        umov            w6, v2.b[0]             // dc

        // Vectorized edge smoothing
        add             w9, w6, w6, lsl #1      // 3*dc
        add             w9, w9, #2              // 3*dc + 2
        dup             v3.8h, w9               // broadcast to 16-bit

        // First row: (top[x] + 3*dc + 2) >> 2
        ushll           v4.8h, v0.8b, #0        // widen top
        add             v4.8h, v4.8h, v3.8h
        ushr            v4.8h, v4.8h, #2
        xtn             v4.8b, v4.8h
        str             s4, [x0]                // store smoothed row, overwrite corner below

        // Corner: POS(0,0) = (left[0] + 2*dc + top[0] + 2) >> 2
        ldrb            w7, [x2]                // left[0]
        ldrb            w8, [x1]                // top[0]
        add             w10, w6, w6             // 2*dc
        add             w10, w10, w7
        add             w10, w10, w8
        add             w10, w10, #2
        lsr             w10, w10, #2
        strb            w10, [x0]

        // First column: (left[y] + 3*dc + 2) >> 2 for y=1..3
        ushll           v5.8h, v1.8b, #0        // widen left
        add             v5.8h, v5.8h, v3.8h
        ushr            v5.8h, v5.8h, #2
        xtn             v5.8b, v5.8h
        add             x5, x0, x3
        st1             {v5.b}[1], [x5]
        add             x5, x5, x3
        st1             {v5.b}[2], [x5]
        add             x5, x5, x3
        st1             {v5.b}[3], [x5]

9:      ret
endfunc
	
// -----------------------------------------------------------------------------
// pred_dc_8x8_8: DC prediction
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// w4: c_idx
// -----------------------------------------------------------------------------
function ff_hevc_pred_dc_8x8_8_neon, export=1
        // Load top[0..7] and left[0..7]
        ldr             d0, [x1]                // top[0..7]
        ldr             d1, [x2]                // left[0..7]

        // Sum all pixels
        uaddlv          h2, v0.8b               // sum top
        uaddlv          h3, v1.8b               // sum left
        add             v2.4h, v2.4h, v3.4h     // total sum

        // Add rounding (8) and shift by 4
        movi            v3.4h, #8
        add             v2.4h, v2.4h, v3.4h     // + 8
        ushr            v2.4h, v2.4h, #4        // >> 4
        umov            w6, v2.h[0]             // dc as scalar
        dup             v2.8b, w6               // broadcast dc

        // Check if edge smoothing needed (luma only)
        cbnz            w4, 2f

        // === Luma path: fill + edge smoothing combined ===

        // Precompute smoothed values
        add             w9, w6, w6, lsl #1      // 3*dc
        add             w9, w9, #2              // 3*dc + 2
        dup             v3.8h, w9               // broadcast to 16-bit

        // Smoothed first row
        ushll           v4.8h, v0.8b, #0
        add             v4.8h, v4.8h, v3.8h
        ushr            v4.8h, v4.8h, #2
        xtn             v4.8b, v4.8h            // smoothed row

        // Corner: POS(0,0) = (left[0] + 2*dc + top[0] + 2) >> 2
        ldrb            w7, [x2]
        ldrb            w8, [x1]
        add             w10, w6, w6
        add             w10, w10, w7
        add             w10, w10, w8
        add             w10, w10, #2
        lsr             w10, w10, #2
        ins             v4.b[0], w10

        // Smoothed column values
        ushll           v5.8h, v1.8b, #0
        add             v5.8h, v5.8h, v3.8h
        ushr            v5.8h, v5.8h, #2
        xtn             v5.8b, v5.8h            // smoothed col values in v5.b[0..7]

        // Store row 0 (smoothed)
        str             d4, [x0]

        // Store DC fill for rows 1-7 first
        add             x5, x0, x3
.rept 7
        str             d2, [x5]
        add             x5, x5, x3
.endr

        // Scatter-store column bytes
        add             x5, x0, x3
.irp n, 1, 2, 3, 4, 5, 6, 7
        st1             {v5.b}[\n], [x5]
        add             x5, x5, x3
.endr
        ret

2:      // === Chroma path: plain DC fill ===
        str             d2, [x0]
.rept 7
        add             x0, x0, x3
        str             d2, [x0]
.endr
        ret
endfunc

// -----------------------------------------------------------------------------
// pred_dc_16x16_8: DC prediction
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// w4: c_idx
// -----------------------------------------------------------------------------
function ff_hevc_pred_dc_16x16_8_neon, export=1
        // Load top[0..15] and left[0..15]
        ldr             q0, [x1]                // top[0..15]
        ldr             q1, [x2]                // left[0..15]

        // Sum all pixels
        uaddlv          h2, v0.16b              // sum top
        uaddlv          h3, v1.16b              // sum left
        add             v2.4h, v2.4h, v3.4h

        // Add rounding (16) and shift by 5
        movi            v3.4h, #16
        add             v2.4h, v2.4h, v3.4h
        ushr            v2.4h, v2.4h, #5
        umov            w6, v2.h[0]             // dc as scalar
        dup             v2.16b, w6              // broadcast dc

        // Check if edge smoothing needed (luma only)
        cbnz            w4, 2f

        // === Luma path: fill + edge smoothing combined ===

        // Precompute smoothed first row
        add             w9, w6, w6, lsl #1      // 3*dc
        add             w9, w9, #2              // 3*dc + 2
        dup             v3.8h, w9

        ushll           v4.8h, v0.8b, #0
        ushll2          v5.8h, v0.16b, #0
        add             v4.8h, v4.8h, v3.8h
        add             v5.8h, v5.8h, v3.8h
        ushr            v4.8h, v4.8h, #2
        ushr            v5.8h, v5.8h, #2
        xtn             v4.8b, v4.8h
        xtn2            v4.16b, v5.8h           // smoothed first row

        // Corner
        ldrb            w7, [x2]
        ldrb            w8, [x1]
        add             w10, w6, w6
        add             w10, w10, w7
        add             w10, w10, w8
        add             w10, w10, #2
        lsr             w10, w10, #2
        ins             v4.b[0], w10

        // Smoothed column
        ushll           v5.8h, v1.8b, #0
        ushll2          v6.8h, v1.16b, #0
        add             v5.8h, v5.8h, v3.8h
        add             v6.8h, v6.8h, v3.8h
        ushr            v5.8h, v5.8h, #2
        ushr            v6.8h, v6.8h, #2
        xtn             v5.8b, v5.8h
        xtn2            v5.16b, v6.8h           // smoothed column values

        // Store row 0 (smoothed)
        str             q4, [x0]

        // Store DC fill for all 15 remaining rows first
        add             x5, x0, x3
.rept 15
        str             q2, [x5]
        add             x5, x5, x3
.endr

        // Now scatter-store column bytes over the DC fill
        add             x5, x0, x3
.irp n, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
        st1             {v5.b}[\n], [x5]
        add             x5, x5, x3
.endr
        ret

2:      // === Chroma path: plain DC fill ===
        str             q2, [x0]
.rept 15
        add             x0, x0, x3
        str             q2, [x0]
.endr
        ret
endfunc

// -----------------------------------------------------------------------------
// pred_dc_32x32_8: DC prediction (no edge smoothing)
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// w4: c_idx
// -----------------------------------------------------------------------------
function ff_hevc_pred_dc_32x32_8_neon, export=1
        // Load top[0..31] and left[0..31]
        ldp             q0, q1, [x1]            // top[0..31]
        ldp             q2, q3, [x2]            // left[0..31]

        // Sum all pixels
        uaddlv          h0, v0.16b
        uaddlv          h1, v1.16b
        uaddlv          h2, v2.16b
        uaddlv          h3, v3.16b
        add             v0.4h, v0.4h, v1.4h
        add             v2.4h, v2.4h, v3.4h
        add             v0.4h, v0.4h, v2.4h

        // Add rounding (32) and shift by 6
        movi            v2.4h, #32
        add             v0.4h, v0.4h, v2.4h
        ushr            v0.4h, v0.4h, #6
        dup             v0.16b, v0.b[0]
        mov             v1.16b, v0.16b

        // Store 32 rows
        mov             w6, #32
2:
        stp             q0, q1, [x0]
        add             x0, x0, x3
        subs            w6, w6, #1
        b.ne            2b

        // No edge smoothing for 32x32 (size >= 32)
        ret
endfunc

// =============================================================================
// Planar Prediction
// =============================================================================

/*
 * Planar prediction algorithm:
 * For each pixel (x, y):
 * POS(x,y) = ((size-1-x)*left[y] + (x+1)*top[size] +
 *             (size-1-y)*top[x] + (y+1)*left[size] + size) >> (log2_size+1)
 */
// -----------------------------------------------------------------------------
// pred_planar_4x4_8: Planar prediction
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// -----------------------------------------------------------------------------
function ff_hevc_pred_planar_4x4_8_neon, export=1
        // Load reference samples
        ldr             s0, [x1]                // top[0..3]
        ldr             s1, [x2]                // left[0..3]
        ldrb            w4, [x1, #4]            // top[4]
        ldrb            w5, [x2, #4]            // left[4]

        // Setup weight vectors for x direction: [3,2,1,0] and [1,2,3,4]
        movrel          x6, planar_weights_4
        ldr             d4, [x6]                // weights_dec = [3,2,1,0, ...]
        ldr             d5, [x6, #8]            // weights_inc = [1,2,3,4, ...]

        // Broadcast top[4] and left[4]
        dup             v6.8b, w4               // top[size]
        dup             v7.8b, w5               // left[size]

        // Rounding constant (hoisted out of loop)
        movi            v20.8h, #4

        // Process row by row
        mov             w8, #0                  // y = 0

1:
        // For row y:
        // weight_y_dec = size - 1 - y = 3 - y
        // weight_y_inc = y + 1

        mov             w9, #3
        sub             w9, w9, w8              // 3 - y
        add             w10, w8, #1             // y + 1

        // Load left[y]
        ldrb            w11, [x2, w8, uxtw]
        dup             v2.8b, w11              // broadcast left[y]

        // (size-1-x) * left[y] : use weights_dec * left[y]
        umull           v16.8h, v4.8b, v2.8b    // v16 = weights_dec * left[y]

        // (x+1) * top[size] : use weights_inc * top[4]
        umull           v17.8h, v5.8b, v6.8b    // v17 = weights_inc * top[size]

        // (size-1-y) * top[x]
        dup             v3.8b, w9               // broadcast (3 - y)
        umull           v18.8h, v3.8b, v0.8b    // v18 = (3-y) * top[x]

        // (y+1) * left[size]
        dup             v3.8b, w10              // broadcast (y + 1)
        umull           v19.8h, v3.8b, v7.8b    // v19 = (y+1) * left[size]

        // Sum all terms + rounding
        add             v16.8h, v16.8h, v17.8h
        add             v18.8h, v18.8h, v19.8h
        add             v16.8h, v16.8h, v18.8h
        add             v16.8h, v16.8h, v20.8h  // + 4 (rounding)

        // Shift right by 3 (log2_size + 1 = 3)
        shrn            v16.8b, v16.8h, #3

        // Store 4 pixels
        str             s16, [x0]
        add             x0, x0, x3

        add             w8, w8, #1
        cmp             w8, #4
        b.lt            1b

        ret
endfunc

// -----------------------------------------------------------------------------
// pred_planar_8x8_8: Planar prediction
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// -----------------------------------------------------------------------------
function ff_hevc_pred_planar_8x8_8_neon, export=1
        // Load reference samples
        ldr             d0, [x1]                // top[0..7]
        ldr             d1, [x2]                // left[0..7]
        ldrb            w4, [x1, #8]            // top[8]
        ldrb            w5, [x2, #8]            // left[8]

        // Setup weight vectors
        movrel          x6, planar_weights_8
        ldr             d4, [x6]                // weights_dec = [7,6,5,4,3,2,1,0]
        ldr             d5, [x6, #8]            // weights_inc = [1,2,3,4,5,6,7,8]

        dup             v6.8b, w4               // top[size]
        dup             v7.8b, w5               // left[size]

        // Rounding constant (hoisted out of loop)
        movi            v20.8h, #8

        mov             w8, #0

1:
        mov             w9, #7
        sub             w9, w9, w8
        add             w10, w8, #1

        ldrb            w11, [x2, w8, uxtw]
        dup             v2.8b, w11

        umull           v16.8h, v4.8b, v2.8b
        umull           v17.8h, v5.8b, v6.8b
        dup             v3.8b, w9
        umull           v18.8h, v3.8b, v0.8b
        dup             v3.8b, w10
        umull           v19.8h, v3.8b, v7.8b

        add             v16.8h, v16.8h, v17.8h
        add             v18.8h, v18.8h, v19.8h
        add             v16.8h, v16.8h, v18.8h
        add             v16.8h, v16.8h, v20.8h

        shrn            v16.8b, v16.8h, #4

        str             d16, [x0]
        add             x0, x0, x3

        add             w8, w8, #1
        cmp             w8, #8
        b.lt            1b

        ret
endfunc

// -----------------------------------------------------------------------------
// pred_planar_16x16_8: Planar prediction
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// -----------------------------------------------------------------------------
function ff_hevc_pred_planar_16x16_8_neon, export=1
        // Load reference samples
        ldr             q0, [x1]                // top[0..15]
        ldr             q1, [x2]                // left[0..15]
        ldrb            w4, [x1, #16]           // top[16]
        ldrb            w5, [x2, #16]           // left[16]

        // Setup weight vectors for 16 elements
        movrel          x6, planar_weights_16
        ldr             q4, [x6]                // weights_dec [15..0]
        ldr             q5, [x6, #16]           // weights_inc [1..16]

        dup             v6.16b, w4
        dup             v7.16b, w5

        // Rounding constant (hoisted out of loop)
        movi            v20.8h, #16

        mov             w8, #0

1:
        mov             w9, #15
        sub             w9, w9, w8
        add             w10, w8, #1

        ldrb            w11, [x2, w8, uxtw]
        dup             v2.16b, w11

        // Need to process in two halves due to 16-bit intermediate results
        // Low 8 elements
        umull           v16.8h, v4.8b, v2.8b
        umull           v17.8h, v5.8b, v6.8b
        dup             v3.8b, w9
        umull           v18.8h, v3.8b, v0.8b
        dup             v3.8b, w10
        umull           v19.8h, v3.8b, v7.8b

        add             v16.8h, v16.8h, v17.8h
        add             v18.8h, v18.8h, v19.8h
        add             v16.8h, v16.8h, v18.8h
        add             v16.8h, v16.8h, v20.8h
        shrn            v16.8b, v16.8h, #5

        // High 8 elements
        umull2          v21.8h, v4.16b, v2.16b
        umull2          v22.8h, v5.16b, v6.16b
        dup             v3.16b, w9               // broadcast (size-1-y) to both low and high halves
        umull2          v23.8h, v3.16b, v0.16b
        dup             v3.16b, w10              // broadcast (y+1) to both low and high halves
        umull2          v24.8h, v3.16b, v7.16b

        add             v21.8h, v21.8h, v22.8h
        add             v23.8h, v23.8h, v24.8h
        add             v21.8h, v21.8h, v23.8h
        add             v21.8h, v21.8h, v20.8h
        shrn2           v16.16b, v21.8h, #5

        str             q16, [x0]
        add             x0, x0, x3

        add             w8, w8, #1
        cmp             w8, #16
        b.lt            1b

        ret
endfunc

// -----------------------------------------------------------------------------
// pred_planar_32x32_8: Planar prediction
//
// Formula: POS(x,y) = ((31-x)*left[y] + (x+1)*top[32]
//                     + (31-y)*top[x]  + (y+1)*left[32] + 32) >> 6
//
// Decomposed as:  base[x] = weight_inc[x]*top[32] + 31*top[x] + 32
//                 Per row:  base[x] += left[32]    (incremental for (y+1)*left[32])
//                           base[x] -= top[x]      (incremental for (31-y)*top[x])
//                           result   = base[x] + weight_dec[x]*left[y]
//
// Both row_add and the (31-y)*top[x] term are folded into the base,
// eliminating all GP→NEON scalar broadcasts except for left[y].
// The loop is fully unrolled (32 rows via macro) to avoid branch overhead
// and enable NEON-domain left[y] broadcasts from preloaded registers.
//
// Arguments:
// x0: src
// x1: top
// x2: left
// x3: stride
// -----------------------------------------------------------------------------
function ff_hevc_pred_planar_32x32_8_neon, export=1
        stp             d8, d9, [sp, #-64]!
        stp             d10, d11, [sp, #16]
        stp             d12, d13, [sp, #32]
        stp             d14, d15, [sp, #48]

        // Load top[0..31]
        ldp             q0, q1, [x1]            // top[0..15], top[16..31]
        ldrb            w4, [x1, #32]           // top[32]
        ldrb            w5, [x2, #32]           // left[32]

        // Load weight vectors
        movrel          x6, planar_weights_32
        ldp             q4, q5, [x6]            // weight_dec = {31,30,...,0}
        ldp             q6, q7, [x6, #32]       // weight_inc = {1,2,...,32}

        // Precompute term_A = weight_inc * top[32]  (16-bit)
        dup             v2.16b, w4
        umull           v8.8h, v6.8b, v2.8b
        umull2          v9.8h, v6.16b, v2.16b
        umull           v10.8h, v7.8b, v2.8b
        umull2          v11.8h, v7.16b, v2.16b

        // Widen top[x] for incremental subtraction
        ushll           v12.8h, v0.8b, #0
        ushll2          v13.8h, v0.16b, #0

        // 31*top[x] = top[x]<<5 - top[x]
        ushll           v6.8h, v0.8b, #5
        ushll2          v7.8h, v0.16b, #5
        sub             v6.8h, v6.8h, v12.8h    // 31*top[0..7]
        sub             v7.8h, v7.8h, v13.8h    // 31*top[8..15]

        // base[0..15] = term_A + 31*top[0..15] + 32
        movi            v3.8h, #32
        add             v8.8h, v8.8h, v6.8h
        add             v8.8h, v8.8h, v3.8h
        add             v9.8h, v9.8h, v7.8h
        add             v9.8h, v9.8h, v3.8h

        // Same for top[16..31]
        ushll           v6.8h, v1.8b, #0
        ushll2          v7.8h, v1.16b, #0
        ushll           v14.8h, v1.8b, #5
        ushll2          v15.8h, v1.16b, #5
        sub             v14.8h, v14.8h, v6.8h   // 31*top[16..23]
        sub             v15.8h, v15.8h, v7.8h   // 31*top[24..31]
        add             v10.8h, v10.8h, v14.8h
        add             v10.8h, v10.8h, v3.8h
        add             v11.8h, v11.8h, v15.8h
        add             v11.8h, v11.8h, v3.8h

        // Compute combined decrement: top[x] - left[32]
        // Each row: base += left[32] and base -= top[x]
        // Combined: base -= (top[x] - left[32])
        dup             v3.8h, w5               // left[32] as 16-bit
        sub             v12.8h, v12.8h, v3.8h   // top[0..7] - left[32]
        sub             v13.8h, v13.8h, v3.8h   // top[8..15] - left[32]
        sub             v6.8h, v6.8h, v3.8h     // top[16..23] - left[32]
        sub             v7.8h, v7.8h, v3.8h     // top[24..31] - left[32]

        // Now base needs initial +=left[32] for y=0 (row_add = 1*left[32])
        add             v8.8h, v8.8h, v3.8h
        add             v9.8h, v9.8h, v3.8h
        add             v10.8h, v10.8h, v3.8h
        add             v11.8h, v11.8h, v3.8h

        // Persistent registers:
        //   v8-v11  = base[0..31] (includes running row_add, decremented by combined each row)
        //   v12,v13 = top[0..15] - left[32] (combined decrement)
        //   v6,v7   = top[16..31] - left[32] (combined decrement)
        //   v4,v5   = weight_dec[0..31] (8-bit)
        //   v1,v3   = left[0..31] preloaded (8-bit)

        // Load left[0..31] into v1,v3
        ldp             q1, q3, [x2]

.macro planar32_row lane, leftreg
        dup             v2.16b, \leftreg\().b[\lane]
        umull           v16.8h, v4.8b, v2.8b
        umull2          v17.8h, v4.16b, v2.16b
        umull           v18.8h, v5.8b, v2.8b
        umull2          v19.8h, v5.16b, v2.16b
        add             v16.8h, v16.8h, v8.8h
        add             v17.8h, v17.8h, v9.8h
        add             v18.8h, v18.8h, v10.8h
        add             v19.8h, v19.8h, v11.8h
        shrn            v14.8b, v16.8h, #6
        shrn2           v14.16b, v17.8h, #6
        shrn            v15.8b, v18.8h, #6
        shrn2           v15.16b, v19.8h, #6
        stp             q14, q15, [x0]
        add             x0, x0, x3
        sub             v8.8h, v8.8h, v12.8h
        sub             v9.8h, v9.8h, v13.8h
        sub             v10.8h, v10.8h, v6.8h
        sub             v11.8h, v11.8h, v7.8h
.endm

        // Rows 0-15 from v1
        planar32_row 0, v1
        planar32_row 1, v1
        planar32_row 2, v1
        planar32_row 3, v1
        planar32_row 4, v1
        planar32_row 5, v1
        planar32_row 6, v1
        planar32_row 7, v1
        planar32_row 8, v1
        planar32_row 9, v1
        planar32_row 10, v1
        planar32_row 11, v1
        planar32_row 12, v1
        planar32_row 13, v1
        planar32_row 14, v1
        planar32_row 15, v1

        // Rows 16-31 from v3
        planar32_row 0, v3
        planar32_row 1, v3
        planar32_row 2, v3
        planar32_row 3, v3
        planar32_row 4, v3
        planar32_row 5, v3
        planar32_row 6, v3
        planar32_row 7, v3
        planar32_row 8, v3
        planar32_row 9, v3
        planar32_row 10, v3
        planar32_row 11, v3
        planar32_row 12, v3
        planar32_row 13, v3
        planar32_row 14, v3
        planar32_row 15, v3

.purgem planar32_row

        ldp             d14, d15, [sp, #48]
        ldp             d10, d11, [sp, #16]
        ldp             d12, d13, [sp, #32]
        ldp             d8, d9, [sp], #64
        ret
endfunc


// =============================================================================
// Weight tables for planar prediction
// =============================================================================

const planar_weights_4, align=4
        .byte   3, 2, 1, 0, 0, 0, 0, 0          // weights_dec for 4x4
        .byte   1, 2, 3, 4, 0, 0, 0, 0          // weights_inc for 4x4
endconst

const planar_weights_8, align=4
        .byte   7, 6, 5, 4, 3, 2, 1, 0          // weights_dec
        .byte   1, 2, 3, 4, 5, 6, 7, 8          // weights_inc
endconst

const planar_weights_16, align=4
        .byte   15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
        .byte   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
endconst

const planar_weights_32, align=4
        .byte   31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16
        .byte   15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
        .byte   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
        .byte   17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32
endconst

// =============================================================================
// Angular Prediction
// =============================================================================
	
// -----------------------------------------------------------------------------
// pred_angular_mode_10_8: Horizontal prediction (mode 10)
// Caller must ensure top[-1] and left[-1] are valid (used for edge smoothing
// when c_idx == 0 and size < 32).
// Arguments:
// x0: src
// x1: top (unused for H reference modes)
// x2: left
// x3: stride
// w4: c_idx
// w5: log2_size
// -----------------------------------------------------------------------------
function ff_hevc_pred_angular_mode_10_8_neon, export=1
        cmp             w5, #2
        b.eq            .Lmode10_4x4
        cmp             w5, #3
        b.eq            .Lmode10_8x8
        cmp             w5, #4
        b.eq            .Lmode10_16x16

        // --- size 32 ---
        mov             w7, #0
.Lmode10_32x32_row:
        ldrb            w8, [x2, w7, uxtw]     // left[y]
        dup             v0.16b, w8
        st1             {v0.16b}, [x0]
        str             q0, [x0, #16]
        add             x0, x0, x3
        add             w7, w7, #1
        cmp             w7, #32
        b.lt            .Lmode10_32x32_row
        // size 32 never does edge smoothing
        ret

        // --- size 16 ---
.Lmode10_16x16:
        mov             x6, x0                  // save src base
        mov             w7, #0
.Lmode10_16x16_row:
        ldrb            w8, [x2, w7, uxtw]
        dup             v0.16b, w8
        st1             {v0.16b}, [x0], x3
        add             w7, w7, #1
        cmp             w7, #16
        b.lt            .Lmode10_16x16_row
        b               .Lmode10_edge_smooth

        // --- size 8, fully unrolled ---
.Lmode10_8x8:
        mov             x6, x0                  // save src base
.irp idx, 0, 1, 2, 3, 4, 5, 6, 7
        ldrb            w8, [x2, #\idx]
        dup             v0.8b, w8
        st1             {v0.8b}, [x0], x3
.endr
        b               .Lmode10_edge_smooth

        // --- size 4, fully unrolled ---
.Lmode10_4x4:
        mov             x6, x0                  // save src base
.irp idx, 0, 1, 2, 3
        ldrb            w8, [x2, #\idx]
        dup             v0.8b, w8
        str             s0, [x0]
        add             x0, x0, x3
.endr

.Lmode10_edge_smooth:
        cbnz            w4, 9f

        mov             x0, x6                  // restore src base

        ldrb            w8, [x2]                // left[0]
        dup             v5.16b, w8

        ldrb            w9, [x1, #-1]           // top[-1]
        dup             v1.16b, w9
        uxtl            v1.8h, v1.8b

        cmp             w5, #2
        b.eq            .Lmode10_smooth_4
        cmp             w5, #3
        b.eq            .Lmode10_smooth_8

        // size 16 edge smoothing
        ldr             q2, [x1]                // top[0..15]
        uxtl            v3.8h, v2.8b
        uxtl2           v4.8h, v2.16b
        sub             v3.8h, v3.8h, v1.8h
        sub             v4.8h, v4.8h, v1.8h
        sshr            v3.8h, v3.8h, #1
        sshr            v4.8h, v4.8h, #1
        uaddw           v3.8h, v3.8h, v5.8b
        uaddw2          v4.8h, v4.8h, v5.16b
        sqxtun          v2.8b, v3.8h
        sqxtun2         v2.16b, v4.8h
        st1             {v2.16b}, [x0]
        ret

.Lmode10_smooth_4:
        ldr             s2, [x1]
        uxtl            v3.8h, v2.8b
        sub             v3.8h, v3.8h, v1.8h
        sshr            v3.8h, v3.8h, #1
        uaddw           v3.8h, v3.8h, v5.8b
        sqxtun          v2.8b, v3.8h
        st1             {v2.s}[0], [x0]
        ret

.Lmode10_smooth_8:
        ldr             d2, [x1]
        uxtl            v3.8h, v2.8b
        sub             v3.8h, v3.8h, v1.8h
        sshr            v3.8h, v3.8h, #1
        uaddw           v3.8h, v3.8h, v5.8b
        sqxtun          v2.8b, v3.8h
        st1             {v2.8b}, [x0]

9:      ret
endfunc

// -----------------------------------------------------------------------------
// pred_angular_mode_26_8: Vertical prediction (mode 26)
// Caller must ensure top[-1] and left[-1] are valid (used for edge smoothing
// when c_idx == 0 and size < 32).
// Arguments:
// x0: src
// x1: top
// x2: left (unused for V reference modes)
// x3: stride
// w4: c_idx
// w5: log2_size
// -----------------------------------------------------------------------------
function ff_hevc_pred_angular_mode_26_8_neon, export=1
        mov             w6, #1
        lsl             w6, w6, w5      // size
        mov             x7, x0          // x7 = write pointer (preserve x0)

        cmp             w5, #2
        b.ne            3f
        // size 4: copy top[0..3], 4 rows
        ldr             s0, [x1]        // Load top[0..3] once
        mov             w9, w6          // Loop counter = 4

104:    st1             {v0.s}[0], [x7], x3 // Store 4 bytes, increment stride
        subs            w9, w9, #1
        b.gt            104b
        b               .Lmode26_edge_smooth

3:      cmp             w5, #3
        b.ne            4f
        // size 8: copy top[0..7], 8 rows
        ldr             d0, [x1]        // Load top[0..7] once
        mov             w9, w6          // Loop counter = 8

108:    st1             {v0.8b}, [x7], x3
        subs            w9, w9, #1
        b.gt            108b
        b               .Lmode26_edge_smooth

4:      cmp             w5, #4
        b.ne            0f
        // size 16: copy top[0..15], 16 rows
        ldr             q0, [x1]        // Load top[0..15] once
        mov             w9, w6          // Loop counter = 16

116:    st1             {v0.16b}, [x7], x3
        subs            w9, w9, #1
        b.gt            116b
        b               .Lmode26_edge_smooth

0:      // size >= 32: load and copy
        ldp             q0, q1, [x1]    // Load top[0..31] once
        mov             x7, x0
        mov             w9, w6          // Loop counter = size

132:    stp             q0, q1, [x7]    // Store 32 bytes
        add             x7, x7, x3      // Advance output pointer
        subs            w9, w9, #1
        b.gt            132b

.Lmode26_edge_smooth:
        cbnz            w4, 9f
        cmp             w5, #5
        b.ge            9f

        // Restore x0 to original src (x7 has moved to src + size*stride)
        mul             x9, x3, x6
        sub             x0, x7, x9              // x0 = x7 - size*stride = original src

        ldrb            w8, [x1]
        ldrb            w9, [x2, #-1]
        dup             v5.16b, w8              // v5 = top[0] (keep bytes for uaddw)
        dup             v1.16b, w9
        uxtl            v1.8h, v1.8b            // Unsigned extend left[-1] to halfwords in v1

        cmp             w5, #2
        b.eq            224f
        cmp             w5, #3
        b.eq            228f

        ldr             q2, [x2]
        uxtl            v3.8h, v2.8b           // Unsigned extend left[0..7] to halfwords
        uxtl2           v4.8h, v2.16b          // Unsigned extend left[8..15] to halfwords
        sub             v3.8h, v3.8h, v1.8h    // Subtract left[-1] (result can be negative)
        sub             v4.8h, v4.8h, v1.8h
        sshr            v3.8h, v3.8h, #1       // Arithmetic shift right by 1
        sshr            v4.8h, v4.8h, #1
        uaddw           v3.8h, v3.8h, v5.8b    // Add top[0] from v5 (unsigned extend, top[0] is unsigned)
        uaddw2          v4.8h, v4.8h, v5.16b
        sqxtun          v3.8b, v3.8h           // Saturate back to bytes
        sqxtun2         v3.16b, v4.8h
        
        st1             {v3.b}[0], [x0], x3
        st1             {v3.b}[1], [x0], x3
        st1             {v3.b}[2], [x0], x3
        st1             {v3.b}[3], [x0], x3
        st1             {v3.b}[4], [x0], x3
        st1             {v3.b}[5], [x0], x3
        st1             {v3.b}[6], [x0], x3
        st1             {v3.b}[7], [x0], x3
        st1             {v3.b}[8], [x0], x3
        st1             {v3.b}[9], [x0], x3
        st1             {v3.b}[10], [x0], x3
        st1             {v3.b}[11], [x0], x3
        st1             {v3.b}[12], [x0], x3
        st1             {v3.b}[13], [x0], x3
        st1             {v3.b}[14], [x0], x3
        st1             {v3.b}[15], [x0], x3
        b               9f
        
224:    ldr             s2, [x2]
        uxtl            v3.8h, v2.8b           // Unsigned extend left[0..3] to halfwords
        sub             v3.8h, v3.8h, v1.8h    // Subtract left[-1] (result can be negative)
        sshr            v3.8h, v3.8h, #1       // Arithmetic shift right by 1
        uaddw           v3.8h, v3.8h, v5.8b    // Add top[0] from v5 (unsigned extend, top[0] is unsigned)
        sqxtun          v3.8b, v3.8h           // Saturate back to bytes
        st1             {v3.b}[0], [x0], x3
        st1             {v3.b}[1], [x0], x3
        st1             {v3.b}[2], [x0], x3
        st1             {v3.b}[3], [x0], x3
        b               9f

228:    ldr             d2, [x2]
        uxtl            v3.8h, v2.8b           // Unsigned extend left[0..7] to halfwords
        sub             v3.8h, v3.8h, v1.8h    // Subtract left[-1] (result can be negative)
        sshr            v3.8h, v3.8h, #1       // Arithmetic shift right by 1
        uaddw           v3.8h, v3.8h, v5.8b    // Add top[0] from v5 (unsigned extend, top[0] is unsigned)
        sqxtun          v3.8b, v3.8h           // Saturate back to bytes
        st1             {v3.b}[0], [x0], x3
        st1             {v3.b}[1], [x0], x3
        st1             {v3.b}[2], [x0], x3
        st1             {v3.b}[3], [x0], x3
        st1             {v3.b}[4], [x0], x3
        st1             {v3.b}[5], [x0], x3
        st1             {v3.b}[6], [x0], x3
        st1             {v3.b}[7], [x0], x3
        b               9f

9:      ret
endfunc

// -----------------------------------------------------------------------------
// pred_angular_mode_18_4x4_8: Mode 18 prediction for 4x4 block
// Row 0: top[-1], top[0], top[1], top[2]
// Row 1: left[0], top[-1], top[0], top[1]
// Row 2: left[1], left[0], top[-1], top[0]
// Row 3: left[2], left[1], left[0], top[-1]
// -----------------------------------------------------------------------------
function ff_hevc_pred_angular_mode_18_4x4_8_neon, export=1
        // Build ref array in register
        // ref[-4..-1] = left[3], left[2], left[1], left[0]  (reversed)
        // ref[0..4] = top[-1..3]

        // Load left[0..3] and reverse
        ldr             s0, [x2]                // left[0..3]
        rev32           v0.8b, v0.8b            // v0 = {left[3], left[2], left[1], left[0], ...}

        // Load top[-1..3]
        sub             x4, x1, #1
        ldr             d1, [x4]                // top[-1..6]

        // Combine: {left[3,2,1,0], top[-1,0,1,2,3,...]}
        ins             v0.s[1], v1.s[0]        // v0 = {left[3,2,1,0], top[-1,0,1,2], ...}

        // Row 0: ref[0..3] = top[-1..2] = v0[4..7]
        ext             v2.8b, v0.8b, v0.8b, #4
        str             s2, [x0]
        add             x0, x0, x3

        // Row 1: ref[-1..2] = v0[3..6]
        ext             v2.8b, v0.8b, v0.8b, #3
        str             s2, [x0]
        add             x0, x0, x3

        // Row 2: ref[-2..1] = v0[2..5]
        ext             v2.8b, v0.8b, v0.8b, #2
        str             s2, [x0]
        add             x0, x0, x3

        // Row 3: ref[-3..0] = v0[1..4]
        ext             v2.8b, v0.8b, v0.8b, #1
        str             s2, [x0]

        ret
endfunc

// -----------------------------------------------------------------------------
// pred_angular_mode_18_8x8_8: Mode 18 prediction for 8x8 block
// -----------------------------------------------------------------------------
function ff_hevc_pred_angular_mode_18_8x8_8_neon, export=1
        // ref[-8..-1] = left[7..0] (reversed)
        // ref[0..8] = top[-1..7]

        // Load left[0..7] and reverse
        ldr             d0, [x2]                // left[0..7]
        rev64           v0.8b, v0.8b            // {left[7..0]}

        // Load top[-1..7]
        sub             x4, x1, #1
        ldr             q1, [x4]                // top[-1..14]

        // Combine into v2 (16 bytes): {left[7..0], top[-1..7]}
        mov             v2.d[0], v0.d[0]        // v2[0..7] = left[7..0]
        mov             v2.d[1], v1.d[0]        // v2[8..15] = top[-1..6]

        // Row 0: ref[0..7] = top[-1..6] = v2[8..15]
        mov             v3.d[0], v2.d[1]
        str             d3, [x0]
        add             x0, x0, x3

        // Row 1-7: use ext with decreasing offset
        ext             v3.16b, v2.16b, v2.16b, #7
        str             d3, [x0]
        add             x0, x0, x3

        ext             v3.16b, v2.16b, v2.16b, #6
        str             d3, [x0]
        add             x0, x0, x3

        ext             v3.16b, v2.16b, v2.16b, #5
        str             d3, [x0]
        add             x0, x0, x3

        ext             v3.16b, v2.16b, v2.16b, #4
        str             d3, [x0]
        add             x0, x0, x3

        ext             v3.16b, v2.16b, v2.16b, #3
        str             d3, [x0]
        add             x0, x0, x3

        ext             v3.16b, v2.16b, v2.16b, #2
        str             d3, [x0]
        add             x0, x0, x3

        ext             v3.16b, v2.16b, v2.16b, #1
        str             d3, [x0]

        ret
endfunc

// -----------------------------------------------------------------------------
// pred_angular_mode_18_16x16_8: Mode 18 prediction for 16x16 block
// ref[-16..-1] = left[15..0] reversed, ref[0..15] = top[-1..14]
// -----------------------------------------------------------------------------
function ff_hevc_pred_angular_mode_18_16x16_8_neon, export=1
        // Register-based approach using EXT to slide a window across {v0:v1}.
        // v0 = left[15..0] (reversed), v1 = top[-1..14]
        // Row k: need ref[-k..15-k] = EXT(v0, v1, #16-k) for k=1..15, row 0 = v1.

        ldr             q0, [x2]                // left[0..15]
        rev64           v0.16b, v0.16b          // reverse in 64-bit lanes
        ext             v0.16b, v0.16b, v0.16b, #8  // v0 = left[15..0]
        sub             x4, x1, #1
        ldr             q1, [x4]                // v1 = top[-1..14]

        // Row 0: ref[0..15] = v1
        str             q1, [x0]
        add             x0, x0, x3
        // Row 1: EXT(v0, v1, #15) = {v0[15], v1[0..14]} = {left[0], top[-1..13]}
        ext             v2.16b, v0.16b, v1.16b, #15
        str             q2, [x0]
        add             x0, x0, x3
        // Row 2
        ext             v2.16b, v0.16b, v1.16b, #14
        str             q2, [x0]
        add             x0, x0, x3
        // Row 3
        ext             v2.16b, v0.16b, v1.16b, #13
        str             q2, [x0]
        add             x0, x0, x3
        // Row 4
        ext             v2.16b, v0.16b, v1.16b, #12
        str             q2, [x0]
        add             x0, x0, x3
        // Row 5
        ext             v2.16b, v0.16b, v1.16b, #11
        str             q2, [x0]
        add             x0, x0, x3
        // Row 6
        ext             v2.16b, v0.16b, v1.16b, #10
        str             q2, [x0]
        add             x0, x0, x3
        // Row 7
        ext             v2.16b, v0.16b, v1.16b, #9
        str             q2, [x0]
        add             x0, x0, x3
        // Row 8
        ext             v2.16b, v0.16b, v1.16b, #8
        str             q2, [x0]
        add             x0, x0, x3
        // Row 9
        ext             v2.16b, v0.16b, v1.16b, #7
        str             q2, [x0]
        add             x0, x0, x3
        // Row 10
        ext             v2.16b, v0.16b, v1.16b, #6
        str             q2, [x0]
        add             x0, x0, x3
        // Row 11
        ext             v2.16b, v0.16b, v1.16b, #5
        str             q2, [x0]
        add             x0, x0, x3
        // Row 12
        ext             v2.16b, v0.16b, v1.16b, #4
        str             q2, [x0]
        add             x0, x0, x3
        // Row 13
        ext             v2.16b, v0.16b, v1.16b, #3
        str             q2, [x0]
        add             x0, x0, x3
        // Row 14
        ext             v2.16b, v0.16b, v1.16b, #2
        str             q2, [x0]
        add             x0, x0, x3
        // Row 15
        ext             v2.16b, v0.16b, v1.16b, #1
        str             q2, [x0]

        ret
endfunc

// -----------------------------------------------------------------------------
// pred_angular_mode_18_32x32_8: Mode 18 prediction for 32x32 block
// ref[-32..-1] = left[31..0] reversed, ref[0..31] = top[-1..30]
// -----------------------------------------------------------------------------
function ff_hevc_pred_angular_mode_18_32x32_8_neon, export=1
        // Use memory-based approach: load from combined memory layout
        // For row y: load 32 bytes from top[-1-y..30-y]
        // When y > 0, some bytes come from extended left reference

        // Build ref array on stack (64 bytes: ref[-32..31])
        sub             sp, sp, #64

        // Store left[31..0] reversed at sp[0..31] (ref[-32..-1])
        ldp             q0, q1, [x2]            // left[0..31]
        rev64           v0.16b, v0.16b
        ext             v0.16b, v0.16b, v0.16b, #8  // left[15..0]
        rev64           v1.16b, v1.16b
        ext             v1.16b, v1.16b, v1.16b, #8  // left[31..16]
        stp             q1, q0, [sp]            // {left[31..16], left[15..0]}

        // Store top[-1..30] at sp[32..63] (ref[0..31])
        sub             x4, x1, #1
        ldp             q2, q3, [x4]            // top[-1..30]
        stp             q2, q3, [sp, #32]

        // ref_base = sp + 32 (so ref[0] = sp[32], ref[-1] = sp[31], etc.)
        add             x4, sp, #32

        mov             w5, #0
1:
        // Row y: load from ref[-y..31-y] = &ref_base[-y]
        sub             x6, x4, w5, sxtw
        ldp             q0, q1, [x6]
        stp             q0, q1, [x0]
        add             x0, x0, x3

        add             w5, w5, #1
        cmp             w5, #32
        b.lt            1b

        add             sp, sp, #64
        ret
endfunc

// =============================================================================
// Angular Prediction - Vertical reference modes (Mode 27-34)
// =============================================================================

// Angle table for V reference positive angles (mode 27-34)
// angle = intra_pred_angle_v[mode - 27]
const intra_pred_angle_v, align=4
        .byte   2       // mode 27
        .byte   5       // mode 28
        .byte   9       // mode 29
        .byte   13      // mode 30
        .byte   17      // mode 31
        .byte   21      // mode 32
        .byte   26      // mode 33
        .byte   32      // mode 34
endconst

// -----------------------------------------------------------------------------
// pred_angular_v_pos_4x4_8: Vertical reference positive angle prediction (mode 27-34)
// Arguments:
// x0: src
// x1: top
// x2: left (unused for V reference modes)
// x3: stride
// w4: c_idx
// w5: mode
// -----------------------------------------------------------------------------
function ff_hevc_pred_angular_v_pos_4x4_8_neon, export=1
        // Load angle from table
        movrel          x6, intra_pred_angle_v
        sub             w7, w5, #27            // mode - 27 (index into angle table)
        ldrsb           w8, [x6, w7, sxtw]     // angle = intra_pred_angle_v[mode-27]

        // For mode 34 (angle=32), fact is always 0, optimize as pure copy
        cmp             w8, #32
        b.eq            .Lv_pos_4x4_mode34

        mov             w10, #0                 // angle_acc = 0
        movi            v18.16b, #32            // constant 32 for NEON-domain weight computation

.macro v_pos_4x4_row
        add             w10, w10, w8            // angle_acc = (y+1) * angle
        asr             w11, w10, #5            // idx = angle_acc >> 5
        and             w12, w10, #31           // fact = angle_acc & 31

        // Load reference pixels top[idx..idx+4]
        add             x13, x1, w11, sxtw      // x13 = top + idx
        ldr             s0, [x13]
        ldr             s1, [x13, #1]

        // Unconditional interpolation: ((32-fact)*ref[idx+1] + fact*ref[idx+2] + 16) >> 5
        // When fact=0, this simplifies to ref[idx+1] exactly due to rounding in rshrn.
        dup             v17.8b, w12             // broadcast fact
        sub             v16.8b, v18.8b, v17.8b

        umull           v20.8h, v0.8b, v16.8b   // (32-fact) * ref[idx+1]
        umlal           v20.8h, v1.8b, v17.8b   // + fact * ref[idx+2]
        rshrn           v0.8b, v20.8h, #5       // (result + 16) >> 5

        st1             {v0.s}[0], [x0], x3
.endm
        v_pos_4x4_row
        v_pos_4x4_row
        v_pos_4x4_row
        v_pos_4x4_row
.purgem v_pos_4x4_row

        ret

.Lv_pos_4x4_mode34:
        // Mode 34: angle=32, each row copies from top[y+1..y+4]
        // Row 0: top[1..4], Row 1: top[2..5], Row 2: top[3..6], Row 3: top[4..7]
        ldr             s0, [x1, #1]
        st1             {v0.s}[0], [x0], x3
        ldr             s0, [x1, #2]
        st1             {v0.s}[0], [x0], x3
        ldr             s0, [x1, #3]
        st1             {v0.s}[0], [x0], x3
        ldr             s0, [x1, #4]
        str             s0, [x0]
        ret
endfunc

// -----------------------------------------------------------------------------
// pred_angular_v_pos_8x8_8: Vertical reference positive angle prediction (mode 27-34)
// Arguments:
// x0: src
// x1: top
// x2: left (unused for V reference modes)
// x3: stride
// w4: c_idx
// w5: mode
// -----------------------------------------------------------------------------
function ff_hevc_pred_angular_v_pos_8x8_8_neon, export=1
        // Load angle from table
        movrel          x6, intra_pred_angle_v
        sub             w7, w5, #27            // mode - 27 (index into angle table)
        ldrsb           w8, [x6, w7, sxtw]     // angle = intra_pred_angle_v[mode-27]

        // Mode 34 optimization
        cmp             w8, #32
        b.eq            .Lv_pos_8x8_mode34

        mov             w9, #0                  // y = 0
        mov             w10, #0                 // angle_acc = 0
        movi            v18.16b, #32            // constant 32 for NEON-domain weight computation

.Lv_pos_8x8_row_loop:
        add             w10, w10, w8            // angle_acc = (y+1) * angle
        asr             w11, w10, #5            // idx
        and             w12, w10, #31           // fact

        add             x13, x1, w11, sxtw
        ldr             d0, [x13]               // ref[idx+1..idx+8]
        ldr             d1, [x13, #1]           // ref[idx+2..idx+9]

        // Unconditional interpolation: ((32-fact)*ref[idx+1] + fact*ref[idx+2] + 16) >> 5
        dup             v17.8b, w12
        sub             v16.8b, v18.8b, v17.8b

        umull           v20.8h, v0.8b, v16.8b
        umlal           v20.8h, v1.8b, v17.8b
        rshrn           v0.8b, v20.8h, #5

        st1             {v0.8b}, [x0], x3

        add             w9, w9, #1
        cmp             w9, #8
        b.lt            .Lv_pos_8x8_row_loop

        ret

.Lv_pos_8x8_mode34:
        // Mode 34: each row copies from top[y+1..y+8]
        ldr             d0, [x1, #1]
        st1             {v0.8b}, [x0], x3
        ldr             d0, [x1, #2]
        st1             {v0.8b}, [x0], x3
        ldr             d0, [x1, #3]
        st1             {v0.8b}, [x0], x3
        ldr             d0, [x1, #4]
        st1             {v0.8b}, [x0], x3
        ldr             d0, [x1, #5]
        st1             {v0.8b}, [x0], x3
        ldr             d0, [x1, #6]
        st1             {v0.8b}, [x0], x3
        ldr             d0, [x1, #7]
        st1             {v0.8b}, [x0], x3
        ldr             d0, [x1, #8]
        str             d0, [x0]
        ret
endfunc

// -----------------------------------------------------------------------------
// pred_angular_v_pos_16x16_8: Vertical reference positive angle prediction (mode 27-34)
// Arguments:
// x0: src
// x1: top
// x2: left (unused for V reference modes)
// x3: stride
// w4: c_idx
// w5: mode
// -----------------------------------------------------------------------------
function ff_hevc_pred_angular_v_pos_16x16_8_neon, export=1
        // Load angle from table
        movrel          x6, intra_pred_angle_v
        sub             w7, w5, #27            // mode - 27 (index into angle table)
        ldrsb           w8, [x6, w7, sxtw]     // angle = intra_pred_angle_v[mode-27]

        // Mode 34 optimization
        cmp             w8, #32
        b.eq            .Lv_pos_16x16_mode34

        mov             w9, #0                  // y = 0
        mov             w10, #0                 // angle_acc = 0
        movi            v18.16b, #32            // constant 32 for NEON-domain weight computation

.Lv_pos_16x16_row_loop:
        add             w10, w10, w8            // angle_acc = (y+1) * angle
        asr             w11, w10, #5            // idx
        and             w12, w10, #31           // fact

        add             x13, x1, w11, sxtw
        ldr             q0, [x13]               // ref[idx+1..idx+16]
        ldr             q1, [x13, #1]           // ref[idx+2..idx+17]

        // Unconditional interpolation: ((32-fact)*ref[idx+1] + fact*ref[idx+2] + 16) >> 5
        dup             v17.16b, w12
        sub             v16.16b, v18.16b, v17.16b

        // Low 8 bytes
        umull           v20.8h, v0.8b, v16.8b
        umlal           v20.8h, v1.8b, v17.8b
        rshrn           v2.8b, v20.8h, #5

        // High 8 bytes
        umull2          v21.8h, v0.16b, v16.16b
        umlal2          v21.8h, v1.16b, v17.16b
        rshrn2          v2.16b, v21.8h, #5

        st1             {v2.16b}, [x0], x3

        add             w9, w9, #1
        cmp             w9, #16
        b.lt            .Lv_pos_16x16_row_loop

        ret

.Lv_pos_16x16_mode34:
        // Mode 34: each row copies from top[y+1..y+16]
        mov             w9, #0
.Lv_pos_16x16_mode34_loop:
        add             w10, w9, #1
        add             x13, x1, w10, sxtw
        ldr             q0, [x13]
        st1             {v0.16b}, [x0], x3
        add             w9, w9, #1
        cmp             w9, #16
        b.lt            .Lv_pos_16x16_mode34_loop
        ret
endfunc

// -----------------------------------------------------------------------------
// pred_angular_v_pos_32x32_8: Vertical reference positive angle prediction (mode 27-34)
// Arguments:
// x0: src
// x1: top
// x2: left (unused for V reference modes)
// x3: stride
// w4: c_idx
// w5: mode
// -----------------------------------------------------------------------------
function ff_hevc_pred_angular_v_pos_32x32_8_neon, export=1
        // Load angle from table
        movrel          x6, intra_pred_angle_v
        sub             w7, w5, #27            // mode - 27 (index into angle table)
        ldrsb           w8, [x6, w7, sxtw]     // angle = intra_pred_angle_v[mode-27]

        // Mode 34 optimization
        cmp             w8, #32
        b.eq            .Lv_pos_32x32_mode34

        mov             w9, #0                  // y = 0
        mov             w10, #0                 // angle_acc = 0
        movi            v18.16b, #32            // constant 32 for NEON-domain weight computation

.Lv_pos_32x32_row_loop:
        add             w10, w10, w8            // angle_acc = (y+1) * angle
        asr             w11, w10, #5            // idx
        and             w12, w10, #31           // fact

        add             x13, x1, w11, sxtw

        // Load 32 bytes + 1 for interpolation (unconditionally)
        ldr             q0, [x13]               // ref[idx+1..idx+16]
        ldr             q1, [x13, #1]           // ref[idx+2..idx+17]
        ldr             q2, [x13, #16]          // ref[idx+17..idx+32]
        ldr             q3, [x13, #17]          // ref[idx+18..idx+33]

        // Unconditional interpolation: ((32-fact)*ref[idx+1] + fact*ref[idx+2] + 16) >> 5
        dup             v17.16b, w12
        sub             v16.16b, v18.16b, v17.16b

        // First 16 bytes
        umull           v20.8h, v0.8b, v16.8b
        umlal           v20.8h, v1.8b, v17.8b
        rshrn           v4.8b, v20.8h, #5

        umull2          v21.8h, v0.16b, v16.16b
        umlal2          v21.8h, v1.16b, v17.16b
        rshrn2          v4.16b, v21.8h, #5

        // Second 16 bytes
        umull           v22.8h, v2.8b, v16.8b
        umlal           v22.8h, v3.8b, v17.8b
        rshrn           v5.8b, v22.8h, #5

        umull2          v23.8h, v2.16b, v16.16b
        umlal2          v23.8h, v3.16b, v17.16b
        rshrn2          v5.16b, v23.8h, #5

        st1             {v4.16b, v5.16b}, [x0], x3

        add             w9, w9, #1
        cmp             w9, #32
        b.lt            .Lv_pos_32x32_row_loop

        ret

.Lv_pos_32x32_mode34:
        // Mode 34: each row copies from top[y+1..y+32]
        mov             w9, #0
.Lv_pos_32x32_mode34_loop:
        add             w10, w9, #1
        add             x13, x1, w10, sxtw
        ldr             q0, [x13]
        ldr             q1, [x13, #16]
        st1             {v0.16b, v1.16b}, [x0], x3
        add             w9, w9, #1
        cmp             w9, #32
        b.lt            .Lv_pos_32x32_mode34_loop
        ret
endfunc
