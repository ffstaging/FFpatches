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

#include "libavcodec/avcodec.h"
#include "libavutil/log.h"

/*
 * H.261 Picture Start Code (PSC): 20 zero bits followed by 1 bit.
 * Byte layout: 0x00 0x01 0x00 ...
 * Next bits: temporal_ref(5), split_screen(1), camera(1), freeze(1),
 *            source_format(1): 0=QCIF, 1=CIF
 */

/* Minimal CIF frame: source_format bit = 1 */
static const uint8_t cif_frame[] = {
    0x00, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
};

/* Minimal QCIF frame: source_format bit = 0 */
static const uint8_t qcif_frame[] = {
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Garbage with no PSC */
static const uint8_t garbage[] = {
    0xff, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78,
};

/* PSC not at offset 0 */
static const uint8_t psc_mid[] = {
    0xff, 0xab,
    0x00, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
};

int main(void)
{
    AVCodecParserContext *parser;
    AVCodecContext       *avctx;
    uint8_t              *out;
    int                   out_size, ret;

    av_log_set_level(AV_LOG_QUIET);

    parser = av_parser_init(AV_CODEC_ID_H261);
    if (!parser)
        return 0; /* skip if not compiled in */

    /* A codec-independent context — no decoder required */
    avctx = avcodec_alloc_context3(NULL);
    if (!avctx) {
        av_parser_close(parser);
        return 1;
    }

    /* flush (buf_size == 0) */
    ret = av_parser_parse2(parser, avctx, &out, &out_size,
                           NULL, 0, AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    if (ret < 0)
        goto fail;

    /* garbage — no PSC */
    ret = av_parser_parse2(parser, avctx, &out, &out_size,
                           garbage, sizeof(garbage),
                           AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    if (ret < 0)
        goto fail;

    /* valid CIF frame */
    ret = av_parser_parse2(parser, avctx, &out, &out_size,
                           cif_frame, sizeof(cif_frame),
                           AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    if (ret < 0)
        goto fail;

    /* valid QCIF frame */
    ret = av_parser_parse2(parser, avctx, &out, &out_size,
                           qcif_frame, sizeof(qcif_frame),
                           AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    if (ret < 0)
        goto fail;

    /* PSC not at start of buffer */
    ret = av_parser_parse2(parser, avctx, &out, &out_size,
                           psc_mid, sizeof(psc_mid),
                           AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    if (ret < 0)
        goto fail;

    /* split input across two calls */
    ret = av_parser_parse2(parser, avctx, &out, &out_size,
                           cif_frame, 1,
                           AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    if (ret < 0)
        goto fail;

    ret = av_parser_parse2(parser, avctx, &out, &out_size,
                           cif_frame + 1, sizeof(cif_frame) - 1,
                           AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    if (ret < 0)
        goto fail;

    av_parser_close(parser);
    avcodec_free_context(&avctx);
    return 0;

fail:
    av_parser_close(parser);
    avcodec_free_context(&avctx);
    return 1;
}
