/*
 * XCoder Codec Lib Wrapper
 * Copyright (c) 2018 NetInt
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

/**
 * @file
 * XCoder codec lib wrapper header.
 */

#ifndef AVCODEC_NICODEC_H
#define AVCODEC_NICODEC_H

#include <stdbool.h>
#include <time.h>
#include "avcodec.h"
#include "startcode.h"
#include "bsf.h"

#include <ni_device_api.h>
#include "libavutil/hwcontext_ni_quad.h"
#include "libavutil/hwcontext.h"

#define NI_NAL_VPS_BIT (0x01)
#define NI_NAL_SPS_BIT (0x01 << 1)
#define NI_NAL_PPS_BIT (0x01 << 2)
#define NI_GENERATE_ALL_NAL_HEADER_BIT (0x01 << 3)

/* enum for specifying xcoder device/coder index; can be specified in either
   decoder or encoder options. */
enum {
    BEST_DEVICE_INST = -2,
    BEST_DEVICE_LOAD = -1
};

enum {
    HW_FRAMES_OFF = 0,
    HW_FRAMES_ON = 1
};

enum {
    GEN_GLOBAL_HEADERS_AUTO = -1,
    GEN_GLOBAL_HEADERS_OFF = 0,
    GEN_GLOBAL_HEADERS_ON = 1
};

#endif /* AVCODEC_NICODEC_H */
