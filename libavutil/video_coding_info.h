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

#ifndef AVUTIL_VIDEO_CODING_INFO_H
#define AVUTIL_VIDEO_CODING_INFO_H

#include <stdint.h>
#include <stddef.h>

/**
 * @file
 * @ingroup lavu_frame
 * Structures for describing block-level video coding information.
 */

/**
 * @defgroup lavu_video_coding_info Video Coding Info
 * @ingroup lavu_frame
 *
 * @{
 * Structures for describing block-level video coding information, to be
 * attached to an AVFrame as side data.
 *
 * All pointer-like members in these structures are offsets relative to the
 * start of the AVVideoCodingInfo struct to ensure the side data is
 * self-contained and relocatable. This is critical as the underlying buffer
 * may be moved in memory.
 */

/**
 * Structure to hold inter-prediction information for a block.
 */
typedef struct AVBlockInterInfo {
    /**
     * Offsets to motion vectors for list 0 and list 1, relative to the
     * start of the AVVideoCodingInfo struct.
     * The data for each list is an array of [x, y] pairs of int16_t.
     * The number of vectors is given by num_mv.
     * An offset of 0 indicates this data is not present.
     */
    size_t mv_offset[2];

    /**
     * Offsets to reference indices for list 0 and list 1, relative to the
     * start of the AVVideoCodingInfo struct.
     * The data is an array of int8_t. A value of -1 indicates the reference
     * is not used for a specific partition.
     * An offset of 0 indicates this data is not present.
     */
    size_t ref_idx_offset[2];
    /**
     * Number of motion vectors for list 0 and list 1.
     */
    uint8_t num_mv[2];
} AVBlockInterInfo;

/**
 * Structure to hold intra-prediction information for a block.
 */
typedef struct AVBlockIntraInfo {
    /**
     * Offset to an array of intra prediction modes, relative to the
     * start of the AVVideoCodingInfo struct.
     * The number of modes is given by num_pred_modes.
     */
    size_t pred_mode_offset;

    /**
     * Number of intra prediction modes.
     */
    uint8_t num_pred_modes;

    /**
     * Chroma intra prediction mode.
     */
    uint8_t chroma_pred_mode;
} AVBlockIntraInfo;

/**
 * Main structure for a single coding block.
 * This structure can be recursive for codecs that use tree-based partitioning.
 */
typedef struct AVVideoCodingInfoBlock {
    /**
     * Position (x, y) and size (w, h) of the block, in pixels,
     * relative to the top-left corner of the frame.
     */
    int16_t x, y;
    uint8_t w, h;

    /**
     * Flag indicating if the block is intra-coded.
     * 1 if intra, 0 if inter.
     */
    uint8_t is_intra;

    /**
     * The original, codec-specific type of this block or macroblock.
     * This allows a filter to have codec-specific logic for interpreting
     * the generic prediction information based on the source codec.
     * For example, for H.264, this would store the MB type flags (MB_TYPE_*).
     */
    uint32_t codec_specific_type;

    union {
        AVBlockIntraInfo intra;
        AVBlockInterInfo inter;
    };

    /**
     * Number of child blocks this block is partitioned into.
     * If 0, this is a leaf node in the partition tree.
     */
    uint8_t num_children;

    /**
     * Offset to an array of child AVVideoCodingInfoBlock structures, relative
     * to the start of the AVVideoCodingInfo struct.
     * This allows for recursive representation of coding structures.
     * An offset of 0 indicates there are no children.
     */
    size_t children_offset;
} AVVideoCodingInfoBlock;

/**
 * Top-level structure to be attached to an AVFrame as side data.
 * It contains an array of the highest-level coding blocks (e.g., CTUs or MBs).
 */
typedef struct AVVideoCodingInfo {
    /**
     * Number of top-level blocks in the frame.
     */
    uint32_t nb_blocks;

    /**
     * Offset to an array of top-level blocks, relative to the start of the
     * AVVideoCodingInfo struct.
     * The actual data for these blocks, and any child blocks or sub-data,
     * is stored contiguously in the AVBufferRef attached to the side data.
     */
    size_t blocks_offset;
} AVVideoCodingInfo;

/**
 * @}
 */

#endif /* AVUTIL_VIDEO_CODING_INFO_H */
