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

 #include <d3d12.h>
#include <d3d12video.h>

#include "hw_base_encode_av1.h"
#include "av1_levels.h"
#include "libavutil/pixdesc.h"

int ff_hw_base_encode_init_params_av1(FFHWBaseEncodeContext *base_ctx,
                                      AVCodecContext *avctx,
                                      FFHWBaseEncodeAV1 *common,
                                      FFHWBaseEncodeAV1Opts *opts)
{
    AV1RawOBU      *seqheader_obu = &common->raw_sequence_header;
    AV1RawSequenceHeader     *seq = &seqheader_obu->obu.sequence_header;
    const AVPixFmtDescriptor *desc;

    seq->seq_profile  = avctx->profile;
    if (!seq->seq_force_screen_content_tools)
        seq->seq_force_integer_mv = AV1_SELECT_INTEGER_MV;
    seq->seq_tier[0]               = opts->tier;

    desc = av_pix_fmt_desc_get(base_ctx->input_frames->sw_format);
    seq->color_config = (AV1RawColorConfig) {
        .high_bitdepth                  = desc->comp[0].depth == 8 ? 0 : 1,
        .color_primaries                = avctx->color_primaries,
        .transfer_characteristics       = avctx->color_trc,
        .matrix_coefficients            = avctx->colorspace,
        .color_description_present_flag = (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
                                           avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
                                           avctx->colorspace      != AVCOL_SPC_UNSPECIFIED),
        .color_range                    = avctx->color_range == AVCOL_RANGE_JPEG,
        .subsampling_x                  = desc->log2_chroma_w,
        .subsampling_y                  = desc->log2_chroma_h,
    };

    switch (avctx->chroma_sample_location) {
        case AVCHROMA_LOC_LEFT:
            seq->color_config.chroma_sample_position = AV1_CSP_VERTICAL;
            break;
        case AVCHROMA_LOC_TOPLEFT:
            seq->color_config.chroma_sample_position = AV1_CSP_COLOCATED;
            break;
        default:
            seq->color_config.chroma_sample_position = AV1_CSP_UNKNOWN;
            break;
    }

    if (avctx->level != AV_LEVEL_UNKNOWN) {
        seq->seq_level_idx[0] = avctx->level;
    } else {
        const AV1LevelDescriptor *level;
        float framerate;

        if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
            framerate = avctx->framerate.num / avctx->framerate.den;
        else
            framerate = 0;

        // Currently only supporting 1 tile
        level = ff_av1_guess_level(avctx->bit_rate, opts->tier,
                                   base_ctx->surface_width, base_ctx->surface_height,
                                   /*priv->tile_rows*/1 * 1/*priv->tile_cols*/,
                                   /*priv->tile_cols*/1, framerate);
        if (level) {
            av_log(avctx, AV_LOG_VERBOSE, "Using level %s.\n", level->name);
            seq->seq_level_idx[0] = level->level_idx;
        } else {
            av_log(avctx, AV_LOG_VERBOSE, "Stream will not conform to "
                   "any normal level, using maximum parameters level by default.\n");
            seq->seq_level_idx[0] = 31;
            seq->seq_tier[0] = 1;
        }
    }

    // Still picture mode
    seq->still_picture = (base_ctx->gop_size == 1);
    seq->reduced_still_picture_header = seq->still_picture;

    // Feature flags
    seq->enable_filter_intra = opts->enable_filter_intra;
    seq->enable_intra_edge_filter = opts->enable_intra_edge_filter;
    seq->enable_interintra_compound = opts->enable_interintra_compound;
    seq->enable_masked_compound = opts->enable_masked_compound;
    seq->enable_warped_motion = opts->enable_warped_motion;
    seq->enable_dual_filter = opts->enable_dual_filter;
    seq->enable_order_hint = !seq->still_picture;
    if (seq->enable_order_hint)
        seq->order_hint_bits_minus_1 = 7;

    seq->enable_jnt_comp = opts->enable_jnt_comp && seq->enable_order_hint;
    seq->enable_ref_frame_mvs = opts->enable_ref_frame_mvs && seq->enable_order_hint;
    seq->enable_superres = opts->enable_superres;
    seq->enable_cdef = opts->enable_cdef;
    seq->enable_restoration = opts->enable_restoration;

    return 0;
}
