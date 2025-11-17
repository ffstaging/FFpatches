/*
 * Copyright (c) 2025 Lynne <dev@lynne.ee>
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

#ifndef AVCODEC_AAC_AACDEC_USAC_MPS212_H
#define AVCODEC_AAC_AACDEC_USAC_MPS212_H

#include "libavcodec/get_bits.h"

enum MPSDataType {
    MPS_CLD,
    MPS_ICC,
    MPS_IPD,
};

int ff_aac_ec_pair_dec(GetBitContext *gb,
                       int *data[2], int16_t *last,
                       enum MPSDataType data_type, int start_band, int nb_bands,
                       int pair, int coarse,
                       int allowDiffTimeBack_flag);

int ff_aac_huff_dec_reshape(GetBitContext *gb, int16_t *out_data,
                            int nb_val);

#endif /* AVCODEC_AAC_AACDEC_USAC_MPS212_H */
