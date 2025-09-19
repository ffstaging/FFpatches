/*
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

#ifndef AVCODEC_HW_BASE_ENCODE_AV1_H
#define AVCODEC_HW_BASE_ENCODE_AV1_H

#include "hw_base_encode.h"
#include "cbs_av1.h"

typedef struct FFHWBaseEncodeAV1 {
    AV1RawOBU    raw_sequence_header;
    AV1RawOBU raw_temporal_delimiter;
    AV1RawOBU       raw_frame_header;
    AV1RawOBU          raw_metadata;
    AV1RawOBU         raw_tile_group;

    int metadata_hdr_cll_present;
    int metadata_hdr_mdcv_present;
    int metadata_scalability_present;
    int metadata_itut_t35_present;
    int metadata_timecode_present;
} FFHWBaseEncodeAV1;


typedef struct FFHWBaseEncodeAV1Opts {
    int tier;                     // 0: Main tier, 1: High tier
    int level;                    // AV1 level (2.0-7.3 map to 0-23)

    int tile_cols_log2;           // log2(tile columns
    int tile_rows_log2;           // log2(tile rows)
    int nb_tiles;                 // Tile number (1-64)

    int enable_cdef;              // Constrained Directional Enhancement Filter
    int enable_restoration;       // loop restoration
    int enable_superres;          // super-resolution
    int enable_ref_frame_mvs;

    int enable_jnt_comp;
    int enable_128x128_superblock;

    int enable_warped_motion;
    int enable_intra_edge_filter;
    int enable_interintra_compound;
    int enable_masked_compound;
    int enable_filter_intra;

    int enable_loop_filter;
    int enable_loop_filter_delta;
    int enable_dual_filter;

    int enable_palette;
    int enable_intra_block_copy;
} FFHWBaseEncodeAV1Opts;


int ff_hw_base_encode_init_params_av1(FFHWBaseEncodeContext *base_ctx,
                                       AVCodecContext *avctx,
                                       FFHWBaseEncodeAV1 *common,
                                       FFHWBaseEncodeAV1Opts *opts);

#endif /* AVCODEC_HW_BASE_ENCODE_AV1_H */
