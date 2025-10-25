/*
 * Copyright (c) 2025 Shreesh Adiga <16567adigashreesh@gmail.com>
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
#ifndef AVUTIL_X86_CRC_H
#define AVUTIL_X86_CRC_H

#include "libavutil/crc.h"

extern const AVCRC *(*av_crc_get_table_func)(AVCRCId crc_id);
extern uint32_t (*av_crc_func)(const AVCRC *ctx, uint32_t crc,
                               const uint8_t *buffer, size_t length);
extern int (*av_crc_init_func)(AVCRC *ctx, int le, int bits,
                               uint32_t poly, int ctx_size);

uint32_t ff_av_crc_clmul(const AVCRC *ctx, uint32_t crc,
                         const uint8_t *buffer, size_t length);
uint32_t ff_av_crc_le_clmul(const AVCRC *ctx, uint32_t crc,
                            const uint8_t *buffer, size_t length);
void av_crc_init_x86(void);
void av_crc_init_fn(void);

#endif /* AVUTIL_X86_CRC_H */
