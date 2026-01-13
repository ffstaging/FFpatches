/*
 * ARM NEON-optimized NAL functions
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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/arm/cpu.h"
#include "libavutil/cpu.h"

const uint8_t *ff_nal_find_startcode_neon(const uint8_t *p, const uint8_t *end);

/* External function pointer from nal.c */
extern const uint8_t *(*ff_nal_find_startcode_internal)(const uint8_t *p, const uint8_t *end);

void ff_nal_init_arm(void);

void ff_nal_init_arm(void)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags))
        ff_nal_find_startcode_internal = ff_nal_find_startcode_neon;
}
