/*
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2014 Clément Bœsch <u pkh me>
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
 * Codec debug viewer filter.
 *
 * All the MV drawing code from Michael Niedermayer is extracted from
 * libavcodec/mpegvideo.c.
 *
 * TODO: segmentation
 */

#include "libavutil/mem.h"
#include "libavutil/motion_vector.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/video_enc_params.h"
#include "avfilter.h"
#include "filters.h"
#include "qp_table.h"
#include "video.h"

#include "libavcodec/h264.h"
#include "libavcodec/h264pred.h"
#include "libavcodec/h264_mb_info.h"
#include "libavcodec/mpegutils.h"

#define MV_P_FOR  (1<<0)
#define MV_B_FOR  (1<<1)
#define MV_B_BACK (1<<2)
#define MV_TYPE_FOR  (1<<0)
#define MV_TYPE_BACK (1<<1)
#define FRAME_TYPE_I (1<<0)
#define FRAME_TYPE_P (1<<1)
#define FRAME_TYPE_B (1<<2)

typedef struct CodecViewContext {
    const AVClass *class;
    unsigned mv;
    unsigned frame_type;
    unsigned mv_type;
    int hsub, vsub;
    int qp;
    int block;
    int show_modes;
    int frame_count;
} CodecViewContext;

#define OFFSET(x) offsetof(CodecViewContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define CONST(name, help, val, u) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, 0, 0, FLAGS, .unit = u }

static const AVOption codecview_options[] = {
    { "mv", "set motion vectors to visualize", OFFSET(mv), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, INT_MAX, FLAGS, .unit = "mv" },
        CONST("pf", "forward predicted MVs of P-frames",  MV_P_FOR,  "mv"),
        CONST("bf", "forward predicted MVs of B-frames",  MV_B_FOR,  "mv"),
        CONST("bb", "backward predicted MVs of B-frames", MV_B_BACK, "mv"),
    { "qp", NULL, OFFSET(qp), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, .flags = FLAGS },
    { "mv_type", "set motion vectors type", OFFSET(mv_type), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, INT_MAX, FLAGS, .unit = "mv_type" },
    { "mvt",     "set motion vectors type", OFFSET(mv_type), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, INT_MAX, FLAGS, .unit = "mv_type" },
        CONST("fp", "forward predicted MVs",  MV_TYPE_FOR,  "mv_type"),
        CONST("bp", "backward predicted MVs", MV_TYPE_BACK, "mv_type"),
    { "frame_type", "set frame types to visualize motion vectors of", OFFSET(frame_type), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, INT_MAX, FLAGS, .unit = "frame_type" },
    { "ft",         "set frame types to visualize motion vectors of", OFFSET(frame_type), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, INT_MAX, FLAGS, .unit = "frame_type" },
        CONST("if", "I-frames", FRAME_TYPE_I, "frame_type"),
        CONST("pf", "P-frames", FRAME_TYPE_P, "frame_type"),
        CONST("bf", "B-frames", FRAME_TYPE_B, "frame_type"),
    { "block",      "set block partitioning structure to visualize", OFFSET(block), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "show_modes", "Visualize macroblock modes", OFFSET(show_modes), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { NULL }
};

static const char *get_intra_4x4_mode_name(int mode) {
    switch (mode) {
    case VERT_PRED:            return "V";
    case HOR_PRED:             return "H";
    case DC_PRED:              return "DC";
    case DIAG_DOWN_LEFT_PRED:  return "DL";
    case DIAG_DOWN_RIGHT_PRED: return "DR";
    case VERT_RIGHT_PRED:      return "VR";
    case HOR_DOWN_PRED:        return "HD";
    case VERT_LEFT_PRED:       return "VL";
    case HOR_UP_PRED:          return "HU";
    default:                   return "?";
    }
}

static const char *get_intra_16x16_mode_name(int mode) {
    switch (mode) {
    case VERT_PRED8x8:   return "Vertical";
    case HOR_PRED8x8:    return "Horizontal";
    case DC_PRED8x8:     return "DC";
    case PLANE_PRED8x8:  return "Plane";
    default:             return "Unknown";
    }
}

static const char *get_chroma_mode_name(int mode) {
    switch (mode) {
    case DC_PRED8x8:    return "DC";
    case HOR_PRED8x8:   return "H";
    case VERT_PRED8x8:  return "V";
    case PLANE_PRED8x8: return "Plane";
    default:            return "Unknown";
    }
}

static const char *get_inter_sub_mb_type_name(uint8_t type) {
    switch(type){
        case 0: return "D";  // Direct
        case 1: return "L0";
        case 2: return "L1";
        case 3: return "BI";
        default: return "?";
    }
}

AVFILTER_DEFINE_CLASS(codecview);

static int clip_line(int *sx, int *sy, int *ex, int *ey, int maxx)
{
    if(*sx > *ex)
        return clip_line(ex, ey, sx, sy, maxx);

    if (*sx < 0) {
        if (*ex < 0)
            return 1;
        *sy = *ey + (*sy - *ey) * (int64_t)*ex / (*ex - *sx);
        *sx = 0;
    }

    if (*ex > maxx) {
        if (*sx > maxx)
            return 1;
        *ey = *sy + (*ey - *sy) * (int64_t)(maxx - *sx) / (*ex - *sx);
        *ex = maxx;
    }
    return 0;
}

/**
 * Draw a line from (ex, ey) -> (sx, sy).
 * @param w width of the image
 * @param h height of the image
 * @param stride stride/linesize of the image
 * @param color color of the arrow
 */
static void draw_line(uint8_t *buf, int sx, int sy, int ex, int ey,
                      int w, int h, ptrdiff_t stride, int color)
{
    int x, y, fr, f;

    if (clip_line(&sx, &sy, &ex, &ey, w - 1))
        return;
    if (clip_line(&sy, &sx, &ey, &ex, h - 1))
        return;

    sx = av_clip(sx, 0, w - 1);
    sy = av_clip(sy, 0, h - 1);
    ex = av_clip(ex, 0, w - 1);
    ey = av_clip(ey, 0, h - 1);

    buf[sy * stride + sx] += color;

    if (FFABS(ex - sx) > FFABS(ey - sy)) {
        if (sx > ex) {
            FFSWAP(int, sx, ex);
            FFSWAP(int, sy, ey);
        }
        buf += sx + sy * stride;
        ex  -= sx;
        f    = ((ey - sy) * (1 << 16)) / ex;
        for (x = 0; x <= ex; x++) {
            y  = (x * f) >> 16;
            fr = (x * f) & 0xFFFF;
                   buf[ y      * stride + x] += (color * (0x10000 - fr)) >> 16;
            if(fr) buf[(y + 1) * stride + x] += (color *            fr ) >> 16;
        }
    } else {
        if (sy > ey) {
            FFSWAP(int, sx, ex);
            FFSWAP(int, sy, ey);
        }
        buf += sx + sy * stride;
        ey  -= sy;
        if (ey)
            f = ((ex - sx) * (1 << 16)) / ey;
        else
            f = 0;
        for(y= 0; y <= ey; y++){
            x  = (y*f) >> 16;
            fr = (y*f) & 0xFFFF;
                   buf[y * stride + x    ] += (color * (0x10000 - fr)) >> 16;
            if(fr) buf[y * stride + x + 1] += (color *            fr ) >> 16;
        }
    }
}

/**
 * Draw an arrow from (ex, ey) -> (sx, sy).
 * @param w width of the image
 * @param h height of the image
 * @param stride stride/linesize of the image
 * @param color color of the arrow
 */
static void draw_arrow(uint8_t *buf, int sx, int sy, int ex,
                       int ey, int w, int h, ptrdiff_t stride, int color, int tail, int direction)
{
    int dx,dy;

    if (direction) {
        FFSWAP(int, sx, ex);
        FFSWAP(int, sy, ey);
    }

    sx = av_clip(sx, -100, w + 100);
    sy = av_clip(sy, -100, h + 100);
    ex = av_clip(ex, -100, w + 100);
    ey = av_clip(ey, -100, h + 100);

    dx = ex - sx;
    dy = ey - sy;

    if (dx * dx + dy * dy > 3 * 3) {
        int rx =  dx + dy;
        int ry = -dx + dy;
        int length = sqrt((rx * rx + ry * ry) << 8);

        // FIXME subpixel accuracy
        rx = ROUNDED_DIV(rx * (3 << 4), length);
        ry = ROUNDED_DIV(ry * (3 << 4), length);

        if (tail) {
            rx = -rx;
            ry = -ry;
        }

        draw_line(buf, sx, sy, sx + rx, sy + ry, w, h, stride, color);
        draw_line(buf, sx, sy, sx - ry, sy + rx, w, h, stride, color);
    }
    draw_line(buf, sx, sy, ex, ey, w, h, stride, color);
}

static void draw_block_rectangle(uint8_t *buf, int sx, int sy, int w, int h, ptrdiff_t stride, int color)
{
    for (int x = sx; x < sx + w; x++)
        buf[x] = color;

    for (int y = sy; y < sy + h; y++) {
        buf[sx] = color;
        buf[sx + w - 1] = color;
        buf += stride;
    }
}

static void log_mb_info(AVFilterContext *ctx, AVFrame *frame, int64_t frame_num)
{
    AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_H264_MB_INFO);
    if (!sd)
        return;

    const H264MBInfo *mb_info = (const H264MBInfo *)sd->data;
    int nb_mb = sd->size / sizeof(H264MBInfo);
    int mb_w = (frame->width + 15) / 16;

    // Allocate a large buffer to build the log string.
    size_t buf_size = 32768;
    char *log_buf = av_malloc(buf_size);
    if (!log_buf)
        return;

    char *p = log_buf;
    size_t remaining = buf_size;
    int ret;

    // Write the main header for the frame into the buffer
    ret = snprintf(p, remaining, "H.264 Modes for frame_num %"PRId64" (pts: %"PRId64", type: %c):\n",
                   frame_num, frame->pts, av_get_picture_type_char(frame->pict_type));
    if (ret > 0 && ret < remaining) {
        p += ret;
        remaining -= ret;
    }

    for (int i = 0; i < nb_mb; i++) {
        const H264MBInfo *info = &mb_info[i];
        int mb_x = i % mb_w;
        int mb_y = i / mb_w;

        if (remaining < 256) // Safety check, break if buffer is almost full
            break;

        if (IS_INTRA(info->mb_type)) {
            if (IS_INTRA4x4(info->mb_type)) {
                ret = snprintf(p, remaining, "MB(%2d,%2d): I_4x4  C:%-5s P:[%s,%s,%s,%s|%s,%s,%s,%s|%s,%s,%s,%s|%s,%s,%s,%s]\n",
                       mb_x, mb_y, get_chroma_mode_name(info->intra.chroma_pred_mode),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[0]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[1]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[2]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[3]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[4]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[5]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[6]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[7]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[8]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[9]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[10]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[11]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[12]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[13]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[14]),
                       get_intra_4x4_mode_name(info->intra.intra4x4_pred_mode[15]));
            } else if (IS_INTRA16x16(info->mb_type)) {
                ret = snprintf(p, remaining, "MB(%2d,%2d): I_16x16 Mode:%-10s ChromaMode:%s\n",
                       mb_x, mb_y, get_intra_16x16_mode_name(info->intra.intra16x16_pred_mode),
                       get_chroma_mode_name(info->intra.chroma_pred_mode));
            } else if (IS_INTRA_PCM(info->mb_type)) {
                ret = snprintf(p, remaining, "MB(%2d,%2d): I_PCM\n", mb_x, mb_y);
            }
        } else { // Inter
            if (IS_SKIP(info->mb_type)) {
                ret = snprintf(p, remaining, "MB(%2d,%2d): Skip\n", mb_x, mb_y);
            } else if (IS_16X16(info->mb_type)) {
                ret = snprintf(p, remaining, "MB(%2d,%2d): P_16x16 L0:[%d %4d,%4d] L1:[%d %4d,%4d]\n", mb_x, mb_y,
                       info->inter.ref_idx[0][0], info->inter.mv[0][0][0], info->inter.mv[0][0][1],
                       info->inter.ref_idx[1][0], info->inter.mv[1][0][0], info->inter.mv[1][0][1]);
            } else if (IS_16X8(info->mb_type)) {
                ret = snprintf(p, remaining, "MB(%2d,%2d): P_16x8 T: L0:[%d %4d,%4d] L1:[%d %4d,%4d] B: L0:[%d %4d,%4d] L1:[%d %4d,%4d]\n", mb_x, mb_y,
                       info->inter.ref_idx[0][0], info->inter.mv[0][0][0], info->inter.mv[0][0][1],
                       info->inter.ref_idx[1][0], info->inter.mv[1][0][0], info->inter.mv[1][0][1],
                       info->inter.ref_idx[0][8], info->inter.mv[0][8][0], info->inter.mv[0][8][1],
                       info->inter.ref_idx[1][8], info->inter.mv[1][8][0], info->inter.mv[1][8][1]);
            } else if (IS_8X16(info->mb_type)) {
                ret = snprintf(p, remaining, "MB(%2d,%2d): P_8x16  L: L0:[%d %4d,%4d] L1:[%d %4d,%4d] R: L0:[%d %4d,%4d] L1:[%d %4d,%4d]\n", mb_x, mb_y,
                       info->inter.ref_idx[0][0], info->inter.mv[0][0][0], info->inter.mv[0][0][1],
                       info->inter.ref_idx[1][0], info->inter.mv[1][0][0], info->inter.mv[1][0][1],
                       info->inter.ref_idx[0][4], info->inter.mv[0][4][0], info->inter.mv[0][4][1],
                       info->inter.ref_idx[1][4], info->inter.mv[1][4][0], info->inter.mv[1][4][1]);
            } else if (IS_8X8(info->mb_type)) {
                ret = snprintf(p, remaining, "MB(%2d,%2d): P_8x8\n", mb_x, mb_y);
                if (ret > 0 && ret < remaining) {
                    p += ret;
                    remaining -= ret;
                }
                for (int j = 0; j < 4; j++) {
                    if (remaining < 128) break;
                    ret = snprintf(p, remaining, "\tBlk %d: %-2s L0:[%d %4d,%4d] L1:[%d %4d,%4d]\n", j,
                           get_inter_sub_mb_type_name(info->inter.sub_mb_type[j]),
                           info->inter.ref_idx[0][j*4], info->inter.mv[0][j*4][0], info->inter.mv[0][j*4][1],
                           info->inter.ref_idx[1][j*4], info->inter.mv[1][j*4][0], info->inter.mv[1][j*4][1]);
                    if (ret > 0 && ret < remaining) {
                        p += ret;
                        remaining -= ret;
                    }
                }
                ret = 0;
            }
        }

        if (ret > 0 && ret < remaining) {
            p += ret;
            remaining -= ret;
        }
    }

    // Print the entire buffer in one go.
    av_log(ctx, AV_LOG_INFO, "%s", log_buf);
    av_free(log_buf);
}
static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    CodecViewContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    if (s->show_modes) {
        log_mb_info(ctx, frame, s->frame_count);
    }

    s->frame_count++;

    if (s->qp) {
        enum AVVideoEncParamsType qp_type;
        int qstride, ret;
        int8_t *qp_table;

        ret = ff_qp_table_extract(frame, &qp_table, &qstride, NULL, &qp_type);
        if (ret < 0) {
            av_frame_free(&frame);
            return ret;
        }

        if (qp_table) {
            int x, y;
            const int w = AV_CEIL_RSHIFT(frame->width,  s->hsub);
            const int h = AV_CEIL_RSHIFT(frame->height, s->vsub);
            uint8_t *pu = frame->data[1];
            uint8_t *pv = frame->data[2];
            const ptrdiff_t lzu = frame->linesize[1];
            const ptrdiff_t lzv = frame->linesize[2];

            for (y = 0; y < h; y++) {
                for (x = 0; x < w; x++) {
                    const int qp = ff_norm_qscale(qp_table[(y >> 3) * qstride + (x >> 3)], qp_type) * 128/31;
                    pu[x] = pv[x] = qp;
                }
                pu += lzu;
                pv += lzv;
            }
        }
        av_freep(&qp_table);
    }

    if (s->block) {
        AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_VIDEO_ENC_PARAMS);
        if (sd) {
            AVVideoEncParams *par = (AVVideoEncParams*)sd->data;
            const ptrdiff_t stride = frame->linesize[0];

            if (par->nb_blocks) {
                for (int block_idx = 0; block_idx < par->nb_blocks; block_idx++) {
                    AVVideoBlockParams *b = av_video_enc_params_block(par, block_idx);
                    uint8_t *buf = frame->data[0] + b->src_y * stride;

                    draw_block_rectangle(buf, b->src_x, b->src_y, b->w, b->h, stride, 100);
                }
            }
        }
    }

    if (s->mv || s->mv_type) {
        AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_MOTION_VECTORS);
        if (sd) {
            int i;
            const AVMotionVector *mvs = (const AVMotionVector *)sd->data;
            const int is_iframe = (s->frame_type & FRAME_TYPE_I) && frame->pict_type == AV_PICTURE_TYPE_I;
            const int is_pframe = (s->frame_type & FRAME_TYPE_P) && frame->pict_type == AV_PICTURE_TYPE_P;
            const int is_bframe = (s->frame_type & FRAME_TYPE_B) && frame->pict_type == AV_PICTURE_TYPE_B;

            for (i = 0; i < sd->size / sizeof(*mvs); i++) {
                const AVMotionVector *mv = &mvs[i];
                const int direction = mv->source > 0;

                if (s->mv_type) {
                    const int is_fp = direction == 0 && (s->mv_type & MV_TYPE_FOR);
                    const int is_bp = direction == 1 && (s->mv_type & MV_TYPE_BACK);

                    if ((!s->frame_type && (is_fp || is_bp)) ||
                        is_iframe && is_fp || is_iframe && is_bp ||
                        is_pframe && is_fp ||
                        is_bframe && is_fp || is_bframe && is_bp)
                        draw_arrow(frame->data[0], mv->dst_x, mv->dst_y, mv->src_x, mv->src_y,
                                   frame->width, frame->height, frame->linesize[0],
                                   100, 0, direction);
                } else if (s->mv)
                    if ((direction == 0 && (s->mv & MV_P_FOR)  && frame->pict_type == AV_PICTURE_TYPE_P) ||
                        (direction == 0 && (s->mv & MV_B_FOR)  && frame->pict_type == AV_PICTURE_TYPE_B) ||
                        (direction == 1 && (s->mv & MV_B_BACK) && frame->pict_type == AV_PICTURE_TYPE_B))
                        draw_arrow(frame->data[0], mv->dst_x, mv->dst_y, mv->src_x, mv->src_y,
                                   frame->width, frame->height, frame->linesize[0],
                                   100, 0, direction);
            }
        }
    }

    return ff_filter_frame(outlink, frame);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    CodecViewContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;
    return 0;
}

static const AVFilterPad codecview_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

const FFFilter ff_vf_codecview = {
    .p.name        = "codecview",
    .p.description = NULL_IF_CONFIG_SMALL("Visualize information about some codecs."),
    .p.priv_class  = &codecview_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .priv_size     = sizeof(CodecViewContext),
    FILTER_INPUTS(codecview_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    // TODO: we can probably add way more pixel formats without any other
    // changes; anything with 8-bit luma in first plane should be working
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_YUV420P),
};
