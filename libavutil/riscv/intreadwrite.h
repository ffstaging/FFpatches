 /*
 * Copyright (C) 2026 Alibaba Group Holding Limited.
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
#ifndef AVUTIL_RISCV_INTREADWRITE_H
#define AVUTIL_RISCV_INTREADWRITE_H

#if HAVE_RVV
#include <riscv_vector.h>

#define AV_COPY128 AV_COPY128
static av_always_inline void AV_COPY128(void *d, const void *s)
{
    vuint8m1_t tmp = __riscv_vle8_v_u8m1((const uint8_t *)s, 16);
    __riscv_vse8_v_u8m1((uint8_t *)d, tmp, 16);
}

#define AV_COPY128U AV_COPY128U
static av_always_inline void AV_COPY128U(void *d, const void *s)
{
    vuint8m1_t tmp = __riscv_vle8_v_u8m1((const uint8_t *)s, 16);
    __riscv_vse8_v_u8m1((uint8_t *)d, tmp, 16);
}

#define AV_ZERO128 AV_ZERO128
static av_always_inline void AV_ZERO128(void *d)
{
    vuint8m1_t zero = __riscv_vmv_v_x_u8m1(0, 16);
    __riscv_vse8_v_u8m1((uint8_t *)d, zero, 16);
}
#endif
#endif