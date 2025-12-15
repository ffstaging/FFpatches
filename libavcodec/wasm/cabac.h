/*
 * CABAC helpers for WebAssembly
 *
 * Copyright (c) 2025 Xia Tao
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

#ifndef AVCODEC_WASM_CABAC_H
#define AVCODEC_WASM_CABAC_H

#include "libavcodec/cabac.h"

int ff_get_cabac_wasm(CABACContext *c, uint8_t *state);
int ff_get_cabac_bypass_wasm(CABACContext *c);
int ff_get_cabac_bypass_sign_wasm(CABACContext *c, int val);

#endif /* AVCODEC_WASM_CABAC_H */
