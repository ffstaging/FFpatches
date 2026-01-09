/*
 * liboapv encoder
 * Advanced Professional Video codec library
 *
 * Copyright (C) 2025 Dawid Kozinski <d.kozinski@samsung.com>
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
#include <stdlib.h>

#include <oapv/oapv.h>

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/mastering_display_metadata.h"

#include "avcodec.h"
#include "apv.h"
#include "codec_internal.h"
#include "encode.h"
#include "profiles.h"

#define MAX_BS_BUF   (128 * 1024 * 1024)
#define MAX_NUM_FRMS (1)           // supports only 1-frame in an access unit
#define FRM_IDX      (0)           // supports only 1-frame in an access unit
#define MAX_NUM_CC   (OAPV_MAX_CC) // Max number of color components (upto 4:4:4:4)

#define MAX_METADATA_PAYLOADS (8)

// ne2be ...  native-endian to big-endian
#if AV_HAVE_BIGENDIAN
#define oapv_ne2be16(x) (x)
#define oapv_ne2be32(x) (x)
#else
#define oapv_ne2be16(x) av_bswap16(x)
#define oapv_ne2be32(x) av_bswap32(x)
#endif

typedef struct apv_metadata apv_metadata_t;
struct apv_metadata {
    uint32_t num_plds;
    oapvm_payload_t payloads[MAX_METADATA_PAYLOADS];
};

/**
 * The structure stores all the states associated with the instance of APV encoder
 */
typedef struct ApvEncContext {
    const AVClass *class;

    oapve_t id;             // APV instance identifier
    oapvm_t mid;
    oapve_cdesc_t   cdsc;   // coding parameters i.e profile, width & height of input frame, num of therads, frame rate ...
    oapv_bitb_t     bitb;   // bitstream buffer (output)
    oapve_stat_t    stat;   // encoding status (output)

    oapv_frms_t ifrms;      // frames for input

    int num_frames;         // number of frames in an access unit

    int preset_id;          // preset of apv ( fastest, fast, medium, slow, placebo)

    int qp;                 // quantization parameter (QP) [0,63]

    oapvm_payload_mdcv_t mdcv;
    oapvm_payload_cll_t cll;

    apv_metadata_t* metadata;

    char *mastering_display_string;
    char *content_light_string;

    AVDictionary *oapv_params;
} ApvEncContext;

static int apv_imgb_release(oapv_imgb_t *imgb)
{
    int refcnt = --imgb->refcnt;
    if (refcnt == 0) {
        for (int i = 0; i < imgb->np; i++)
            av_freep(&imgb->baddr[i]);
        av_free(imgb);
    }

    return refcnt;
}

static int apv_imgb_addref(oapv_imgb_t * imgb)
{
    int refcnt = ++imgb->refcnt;
    return refcnt;
}

static int apv_imgb_getref(oapv_imgb_t * imgb)
{
    return imgb->refcnt;
}

/**
 * Convert FFmpeg pixel format (AVPixelFormat) into APV pre-defined color format
 *
 * @return APV pre-defined color format (@see oapv.h) on success, OAPV_CF_UNKNOWN on failure
 */
static inline int get_color_format(enum AVPixelFormat pix_fmt)
{
    int cf = OAPV_CF_UNKNOWN;

    switch (pix_fmt) {
    case AV_PIX_FMT_GRAY10:
        cf = OAPV_CF_YCBCR400;
        break;
    case AV_PIX_FMT_YUV422P10:
        cf = OAPV_CF_YCBCR422;
        break;
    case AV_PIX_FMT_YUV422P12:
        cf = OAPV_CF_YCBCR422;
        break;
    case AV_PIX_FMT_YUV444P10:
        cf = OAPV_CF_YCBCR444;
        break;
    case AV_PIX_FMT_YUV444P12:
        cf = OAPV_CF_YCBCR444;
        break;
    case AV_PIX_FMT_YUVA444P10:
        cf = OAPV_CF_YCBCR4444;
        break;
    case AV_PIX_FMT_YUVA444P12:
        cf = OAPV_CF_YCBCR4444;
        break;
    default:
        av_assert0(cf != OAPV_CF_UNKNOWN);
    }

    return cf;
}

static oapv_imgb_t *apv_imgb_create(AVCodecContext *avctx)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    oapv_imgb_t *imgb;
    int input_depth;
    int cfmt;  // color format
    int cs;

    av_assert0(desc);

    imgb = av_mallocz(sizeof(oapv_imgb_t));
    if (!imgb)
        goto fail;

    input_depth = desc->comp[0].depth;
    cfmt = get_color_format(avctx->pix_fmt);
    cs = OAPV_CS_SET(cfmt, input_depth, AV_HAVE_BIGENDIAN);

    imgb->np = desc->nb_components;

    for (int i = 0; i < imgb->np; i++) {
        imgb->w[i]  = avctx->width >> ((i == 1 || i == 2) ? desc->log2_chroma_w : 0);
        imgb->h[i]  = avctx->height;
        imgb->aw[i] = FFALIGN(imgb->w[i], OAPV_MB_W);
        imgb->ah[i] = FFALIGN(imgb->h[i], OAPV_MB_H);
        imgb->s[i]  = imgb->aw[i] * OAPV_CS_GET_BYTE_DEPTH(cs);

        imgb->bsize[i] = imgb->e[i] = imgb->s[i] * imgb->ah[i];
        imgb->a[i] = imgb->baddr[i] = av_mallocz(imgb->bsize[i]);
        if (imgb->a[i] == NULL)
            goto fail;
    }

    imgb->cs = cs;
    imgb->addref = apv_imgb_addref;
    imgb->getref = apv_imgb_getref;
    imgb->release = apv_imgb_release;
    imgb->refcnt = 1;

    return imgb;
fail:
    av_log(avctx, AV_LOG_ERROR, "cannot create image buffer\n");
    if (imgb) {
        for (int i = 0; i < imgb->np; i++)
            av_freep(&imgb->a[i]);
        av_freep(&imgb);
    }
    return NULL;
}

/**
 * Parses the SMPTE ST 2086 mastering display color volume metadata string.
 * The expected format is "G(x,y)B(x,y)R(x,y)WP(x,y)L(max,min)",
 * where x,y are CIE 1931 coordinates (floats) and max,min are luminance
 * values (floats in cd/m^2).
 *
 * @param m The oapvm_payload_mdcv_t structure to fill.
 * @param str The input string containing the metadata.
 * @return 0 on success, AVERROR_INVALIDDATA on parsing error or invalid values.
 */
static int parse_mdcv_string_metadata(oapvm_payload_mdcv_t *mdcv, const char *str) {
    if (!str || !mdcv) {
        return AVERROR_INVALIDDATA;
    }

    uint16_t gx, gy, bx, by, rx, ry, wpx, wpy;
    uint32_t max_l, min_l;
    int ret;

    // Use sscanf to extract the 10 floating-point values from the specific format string.
    ret = sscanf(str, "G(%hu,%hu)B(%hu,%hu)R(%hu,%hu)WP(%hu,%hu)L(%u,%u)",
                 &gx, &gy, &bx, &by, &rx, &ry, &wpx, &wpy, &max_l, &min_l);

    // Check if sscanf successfully assigned all expected fields (10 numerical values).
    const int expected_fields = 10;
    if (ret != expected_fields) {
        // Parsing failed, incorrect format provided.
        return AVERROR_INVALIDDATA;
    }

    // According to APV specification, i = 0, 1, 2 specifies Red, Green, Blue respectively
    // Store the parsed values in the correct order for APV
    mdcv->primary_chromaticity_x[0] = rx; // Red X
    mdcv->primary_chromaticity_y[0] = ry; // Red Y
    mdcv->primary_chromaticity_x[1] = gx; // Green X
    mdcv->primary_chromaticity_y[1] = gy; // Green Y
    mdcv->primary_chromaticity_x[2] = bx; // Blue X
    mdcv->primary_chromaticity_y[2] = by; // Blue Y

    mdcv->white_point_chromaticity_x = wpx;
    mdcv->white_point_chromaticity_y = wpy;

    // Luminance values (max/min nits) are also stored in the same 10000 denominator format.
    mdcv->max_mastering_luminance = max_l;
    mdcv->min_mastering_luminance = min_l;
    
    return 0; // Success
}

/**
 * Parses the CTA-861.3 content light level metadata string into an AVContentLightMetadata structure.
 * The expected format is "<max_cll>,<max_fall>" (e.g., "1000,400").
 *
 * @param cl The oapvm_payload_cll_t structure to fill.
 * @param str The input string containing the metadata.
 * @return 0 on success, AVERROR_INVALIDDATA on parsing error.
 */

static int parse_cll_string_metadata(oapvm_payload_cll_t *cll, const char *str) {
    if (!str || !cll) {
        return AVERROR_INVALIDDATA;
    }

    // Use sscanf to extract the two integer values separated by a comma.
    // MaxCLL and MaxFALL values in the standard are represented as unsigned 16-bit integers (cd/m^2).
    // sscanf is a simple way to parse this fixed format.
    uint16_t max_cll_val, max_fall_val;
    int ret = sscanf(str, "%hu,%hu", &max_cll_val, &max_fall_val);

    // Check if both values were successfully parsed.
    const int expected_fields = 2;
    if (ret != expected_fields) {
        // Parsing failed, incorrect format provided.
        return AVERROR_INVALIDDATA;
    }

    // Assign the parsed values to the structure fields.
    // The fields in AVContentLightMetadata are already in cd/m^2 (nits), 
    // so no AVRational conversion is needed, unlike with mastering display metadata.
    cll->max_cll = max_cll_val;
    cll->max_fall = max_fall_val;

    return 0; // Success
}

static int serialize_metadata_mdcv(const oapvm_payload_mdcv_t* mdcv, uint8_t** buffer, size_t *size) {
    int i;
    uint8_t* current_ptr = NULL;
    uint16_t beu16_val;
    uint32_t beu32_val;

    *size = 6*sizeof(uint16_t) + 2*sizeof(uint16_t) + 2*sizeof(uint32_t);
    *buffer = (uint8_t*)av_mallocz(*size);
    if(*buffer == NULL) {
        return -1;
    }

    current_ptr = *buffer;

    // OAPV structure uses primary_chromaticity_x[0-2] and primary_chromaticity_y[0-2]
    // where 0=Red, 1=Green, 2=Blue according to APV specification
    for (i = 0; i < 3; i++) {
        beu16_val = oapv_ne2be16(mdcv->primary_chromaticity_x[i]);
        memcpy(current_ptr, &beu16_val, sizeof(uint16_t));
        current_ptr += sizeof(uint16_t);

        beu16_val = oapv_ne2be16(mdcv->primary_chromaticity_y[i]);
        memcpy(current_ptr, &beu16_val, sizeof(uint16_t));
        current_ptr += sizeof(uint16_t);
    }

    beu16_val = oapv_ne2be16(mdcv->white_point_chromaticity_x);
    memcpy(current_ptr, &beu16_val, sizeof(uint16_t));
    current_ptr += sizeof(uint16_t);

    beu16_val = oapv_ne2be16(mdcv->white_point_chromaticity_y);
    memcpy(current_ptr, &beu16_val, sizeof(uint16_t));
    current_ptr += sizeof(uint16_t);

    beu32_val = oapv_ne2be32(mdcv->max_mastering_luminance);
    memcpy(current_ptr, &beu32_val, sizeof(uint32_t));
    current_ptr += sizeof(uint32_t);

    beu32_val = oapv_ne2be32(mdcv->min_mastering_luminance);
    memcpy(current_ptr, &beu32_val, sizeof(uint32_t));
    current_ptr += sizeof(uint32_t);

    return 0;
}

static int serialize_metadata_cll(const oapvm_payload_cll_t* cll, uint8_t** buffer, size_t* size) {
    uint8_t* current_ptr = NULL;
    uint16_t beu16_val;

    *size = 2*sizeof(uint16_t);
    *buffer = (uint8_t*)av_mallocz(*size);
    if(*buffer == NULL) {
        return -1;
    }

    current_ptr = *buffer;

    beu16_val = oapv_ne2be16(cll->max_cll);
    memcpy(current_ptr, &beu16_val, sizeof(uint16_t));
    current_ptr += sizeof(uint16_t);

    beu16_val = oapv_ne2be16(cll->max_fall);
    memcpy(current_ptr, &beu16_val, sizeof(uint16_t));
    current_ptr += sizeof(uint16_t);

    return 0;
}

static apv_metadata_t* apv_metadata_create(void) {
    apv_metadata_t* metadata = (apv_metadata_t*)av_malloc(sizeof(apv_metadata_t));
    if (metadata) {
        metadata->num_plds = 0;

        for (int i = 0; i < MAX_METADATA_PAYLOADS; i++) {
            metadata->payloads[i].data = NULL;
        }
    }
    return metadata;
}

static void apv_metadata_destroy(apv_metadata_t* metadata) {
    if (metadata) {
        for (int i = 0; i < metadata->num_plds; i++) {
            if (metadata->payloads[i].data) {
                av_free(metadata->payloads[i].data);
                metadata->payloads[i].data = NULL;
            }
        }
        av_free(metadata);
    }
}

static int apv_metadata_add_payload(apv_metadata_t* metadata, uint32_t group_id, uint32_t type, uint32_t size, void* data) {
    if (!metadata || metadata->num_plds >= MAX_METADATA_PAYLOADS || !data || size == 0) {
        return AVERROR_INVALIDDATA;
    }

    // Copying data into internal buffer
    void* new_data = av_malloc(size);
    if (!new_data) {
        return AVERROR_INVALIDDATA;
    }
    memcpy(new_data, data, size);

    metadata->payloads[metadata->num_plds].group_id = group_id;
    metadata->payloads[metadata->num_plds].type = type;
    metadata->payloads[metadata->num_plds].size = size;
    metadata->payloads[metadata->num_plds].data = new_data;
    metadata->num_plds++;

    return 0; // Success
}


/**
 * The function returns a pointer to the object of the oapve_cdesc_t type.
 * oapve_cdesc_t contains all encoder parameters that should be initialized before the encoder is used.
 *
 * The field values of the oapve_cdesc_t structure are populated based on:
 * - the corresponding field values of the AvCodecConetxt structure,
 * - the apv encoder specific option values,
 *
 * The order of processing input data and populating the apve_cdsc structure
 * 1) first, the fields of the AVCodecContext structure corresponding to the provided input options are processed,
 *    (i.e -pix_fmt yuv422p -s:v 1920x1080 -r 30 -profile:v 0)
 * 2) then apve-specific options added as AVOption to the apv AVCodec implementation
 *    (i.e -preset 0)
 *
 * Keep in mind that, there are options that can be set in different ways.
 * In this case, please follow the above-mentioned order of processing.
 * The most recent assignments overwrite the previous values.
 *
 * @param[in] avctx codec context (AVCodecContext)
 * @param[out] cdsc contains all APV encoder encoder parameters that should be initialized before the encoder is use
 *
 * @return 0 on success, negative error code on failure
 */
static int get_conf(AVCodecContext *avctx, oapve_cdesc_t *cdsc)
{
    ApvEncContext *apv = avctx->priv_data;

    /* initialize apv_param struct with default values */
    int ret = oapve_param_default(&cdsc->param[FRM_IDX]);
    if (OAPV_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Cannot set default parameter\n");
        return AVERROR_EXTERNAL;
    }

    /* read options from AVCodecContext */
    if (avctx->width > 0)
        cdsc->param[FRM_IDX].w = avctx->width;

    if (avctx->height > 0)
        cdsc->param[FRM_IDX].h = avctx->height;

    if (avctx->framerate.num > 0) {
        cdsc->param[FRM_IDX].fps_num = avctx->framerate.num;
        cdsc->param[FRM_IDX].fps_den = avctx->framerate.den;
    } else if (avctx->time_base.num > 0) {
        cdsc->param[FRM_IDX].fps_num = avctx->time_base.den;
        cdsc->param[FRM_IDX].fps_den = avctx->time_base.num;
    }

    cdsc->param[FRM_IDX].preset = apv->preset_id;
    cdsc->param[FRM_IDX].qp = apv->qp;
    if (avctx->bit_rate / 1000 > INT_MAX || avctx->rc_max_rate / 1000 > INT_MAX) {
        av_log(avctx, AV_LOG_ERROR, "bit_rate and rc_max_rate > %d000 is not supported\n", INT_MAX);
        return AVERROR(EINVAL);
    }
    cdsc->param[FRM_IDX].bitrate = (int)(avctx->bit_rate / 1000);
    if (cdsc->param[FRM_IDX].bitrate) {
        if (cdsc->param[FRM_IDX].qp) {
            av_log(avctx, AV_LOG_WARNING, "You cannot set both the bitrate and the QP parameter at the same time.\n"
                                          "If the bitrate is set, the rate control type is set to ABR, which means that the QP value is ignored.\n");
        }
        cdsc->param[FRM_IDX].rc_type = OAPV_RC_ABR;
    }

    cdsc->threads = avctx->thread_count;

    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED) {
        cdsc->param[FRM_IDX].color_primaries = avctx->color_primaries;
        cdsc->param[FRM_IDX].color_description_present_flag = 1;
    }

    if (avctx->color_trc != AVCOL_TRC_UNSPECIFIED) {
        cdsc->param[FRM_IDX].transfer_characteristics = avctx->color_trc;
        cdsc->param[FRM_IDX].color_description_present_flag = 1;
    }

    if (avctx->colorspace != AVCOL_SPC_UNSPECIFIED) {
        cdsc->param[FRM_IDX].matrix_coefficients = avctx->colorspace;
        cdsc->param[FRM_IDX].color_description_present_flag = 1;
    }

    if (avctx->color_range != AVCOL_RANGE_UNSPECIFIED) {
        cdsc->param[FRM_IDX].full_range_flag = (avctx->color_range == AVCOL_RANGE_JPEG);
        cdsc->param[FRM_IDX].color_description_present_flag = 1;
    }

    cdsc->max_bs_buf_size = MAX_BS_BUF; /* maximum bitstream buffer size */
    cdsc->max_num_frms = MAX_NUM_FRMS;

    const AVDictionaryEntry *en = NULL;
    while (en = av_dict_iterate(apv->oapv_params, en)) {
        ret = oapve_param_parse(&cdsc->param[FRM_IDX], en->key, en->value);
        if (ret < 0)
            av_log(avctx, AV_LOG_WARNING, "Error parsing option '%s = %s'.\n", en->key, en->value);
    }

    return 0;
}

/**
 * @brief Initialize APV codec
 * Create an encoder instance and allocate all the needed resources
 *
 * @param avctx codec context
 * @return 0 on success, negative error code on failure
 */
static av_cold int liboapve_init(AVCodecContext *avctx)
{
    ApvEncContext *apv = avctx->priv_data;
    oapve_cdesc_t *cdsc = &apv->cdsc;
    unsigned char *bs_buf;
    int ret;

    apv->metadata = apv_metadata_create();

    /* allocate bitstream buffer */
    bs_buf = (unsigned char *)av_malloc(MAX_BS_BUF);
    if (bs_buf == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate bitstream buffer, size=%d\n", MAX_BS_BUF);
        return AVERROR(ENOMEM);
    }
    apv->bitb.addr = bs_buf;
    apv->bitb.bsize = MAX_BS_BUF;

    /* read configurations and set values for created descriptor (APV_CDSC) */
    ret = get_conf(avctx, cdsc);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Cannot get OAPV configuration\n");
        return ret;
    }

    /* create encoder */
    apv->id = oapve_create(cdsc, &ret);
    if (apv->id == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Cannot create OAPV encoder\n");
        if (ret == OAPV_ERR_INVALID_LEVEL)
            av_log(avctx, AV_LOG_ERROR, "Invalid level idc: %d\n", cdsc->param[0].level_idc);
        return AVERROR_EXTERNAL;
    }

    /* create metadata handler */
    apv->mid = oapvm_create(&ret);
    if (apv->mid == NULL || OAPV_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "cannot create OAPV metadata handler\n");
        return AVERROR_EXTERNAL;
    }

    if(apv->mastering_display_string) {
        oapvm_payload_mdcv_t* mastering = av_mallocz(sizeof(oapvm_payload_mdcv_t));
        if (!mastering) {
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate memory for mastering display metadata\n");
            return AVERROR(ENOMEM);
        }

        ret = parse_mdcv_string_metadata(mastering, apv->mastering_display_string);
        if (ret < 0) {
            av_log(avctx, AV_LOG_WARNING, "Error parsing master-display %s'.\n", apv->mastering_display_string);
            av_free(mastering);
        } else {
            size_t mdcv_buffer_size = 0;
            uint8_t* mdcv_buffer = NULL;

            if(!serialize_metadata_mdcv(mastering, &mdcv_buffer, &mdcv_buffer_size)) {
                ret = apv_metadata_add_payload(apv->metadata, 1, OAPV_METADATA_MDCV, mdcv_buffer_size, mdcv_buffer);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_WARNING, "Error adding mastering display metadata\n");
                }
            }
            av_free(mdcv_buffer);
            av_free(mastering);
        }
    }

    if(apv->content_light_string) {
        oapvm_payload_cll_t* content_light = av_mallocz(sizeof(oapvm_payload_cll_t));
        if (!content_light) {
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate memory for content light metadata\n");
            return AVERROR(ENOMEM);
        }

        ret = parse_cll_string_metadata(content_light, apv->content_light_string);
        if (ret < 0) {
            av_log(avctx, AV_LOG_WARNING, "Error parsing content-light %s'.\n", apv->content_light_string);
            av_free(content_light);
        } else {
            size_t cll_buffer_size = 0;
            uint8_t* cll_buffer = NULL;

            if(!serialize_metadata_cll(content_light, &cll_buffer, &cll_buffer_size)) {
                ret = apv_metadata_add_payload(apv->metadata, 1, OAPV_METADATA_CLL, cll_buffer_size, cll_buffer);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_WARNING, "Error adding content light metadata\n");
                }
            }
            av_free(cll_buffer);
            av_free(content_light);
        }
    }

    ret = oapvm_set_all(apv->mid, apv->metadata->payloads, apv->metadata->num_plds);
    if(OAPV_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "cannot set metadata\n");
        return AVERROR_EXTERNAL;
    }

    int value = OAPV_CFG_VAL_AU_BS_FMT_NONE;
    int size = 4;
    ret = oapve_config(apv->id, OAPV_CFG_SET_AU_BS_FMT, &value, &size);
    if (OAPV_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set config for using encoder output format\n");
        return AVERROR_EXTERNAL;
    }

    apv->ifrms.frm[FRM_IDX].imgb = apv_imgb_create(avctx);
    if (apv->ifrms.frm[FRM_IDX].imgb == NULL)
        return AVERROR(ENOMEM);
    apv->ifrms.num_frms++;

     /* color description values */
    if (cdsc->param[FRM_IDX].color_description_present_flag) {
        avctx->color_primaries = cdsc->param[FRM_IDX].color_primaries;
        avctx->color_trc = cdsc->param[FRM_IDX].transfer_characteristics;
        avctx->colorspace = cdsc->param[FRM_IDX].matrix_coefficients;
        avctx->color_range = (cdsc->param[FRM_IDX].full_range_flag) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    }

    return 0;
}

/**
  * Encode raw data frame into APV packet
  *
  * @param[in]  avctx codec context
  * @param[out] avpkt output AVPacket containing encoded data
  * @param[in]  frame AVFrame containing the raw data to be encoded
  * @param[out] got_packet encoder sets to 0 or 1 to indicate that a
  *                         non-empty packet was returned in pkt
  *
  * @return 0 on success, negative error code on failure
  */
static int liboapve_encode(AVCodecContext *avctx, AVPacket *avpkt,
                          const AVFrame *frame, int *got_packet)
{
    ApvEncContext *apv =  avctx->priv_data;
    const oapve_cdesc_t *cdsc = &apv->cdsc;
    oapv_frm_t *frm = &apv->ifrms.frm[FRM_IDX];
    oapv_imgb_t *imgb = frm->imgb;
    int ret;

    if (avctx->width != frame->width || avctx->height != frame->height || avctx->pix_fmt != frame->format) {
        av_log(avctx, AV_LOG_ERROR, "Dimension changes are not supported\n");
        return AVERROR(EINVAL);
    }

    av_image_copy((uint8_t **)imgb->a, imgb->s, (const uint8_t **)frame->data, frame->linesize,
                  frame->format, frame->width, frame->height);

    imgb->ts[0] = frame->pts;

    frm->group_id = 1; // @todo FIX-ME : need to set properly in case of multi-frame
    frm->pbu_type = OAPV_PBU_TYPE_PRIMARY_FRAME;

    ret = oapve_encode(apv->id, &apv->ifrms, apv->mid, &apv->bitb, &apv->stat, NULL);
    if (OAPV_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "oapve_encode() failed\n");
        return AVERROR_EXTERNAL;
    }

    /* store bitstream */
    if (OAPV_SUCCEEDED(ret) && apv->stat.write > 0) {
        uint8_t *data = apv->bitb.addr;
        int size = apv->stat.write;

        // The encoder may return a "Raw bitstream" formatted AU, including au_size.
        // Discard it as we only need the access_unit() structure.
        if (size > 4 && AV_RB32(data) != APV_SIGNATURE) {
            data += 4;
            size -= 4;
        }

        ret = ff_get_encode_buffer(avctx, avpkt, size, 0);
        if (ret < 0)
            return ret;

        memcpy(avpkt->data, data, size);
        avpkt->pts = avpkt->dts = frame->pts;
        avpkt->flags |= AV_PKT_FLAG_KEY;

        if (cdsc->param[FRM_IDX].qp)
            ff_encode_add_stats_side_data(avpkt, cdsc->param[FRM_IDX].qp * FF_QP2LAMBDA, NULL, 0, AV_PICTURE_TYPE_I);

        *got_packet = 1;
    }

    return 0;
}

/**
 * Destroy the encoder and release all the allocated resources
 *
 * @param avctx codec context
 * @return 0 on success, negative error code on failure
 */
static av_cold int liboapve_close(AVCodecContext *avctx)
{
    ApvEncContext *apv = avctx->priv_data;

    apv_metadata_destroy(apv->metadata);

    for (int i = 0; i < apv->num_frames; i++) {
        if (apv->ifrms.frm[i].imgb != NULL)
            apv->ifrms.frm[i].imgb->release(apv->ifrms.frm[i].imgb);
        apv->ifrms.frm[i].imgb = NULL;
    }

    if (apv->mid) {
        oapvm_rem_all(apv->mid);
    }

    if (apv->id) {
        oapve_delete(apv->id);
        apv->id = NULL;
    }

    if (apv->mid) {
        oapvm_delete(apv->mid);
        apv->mid = NULL;
    }

    av_freep(&apv->bitb.addr); /* release bitstream buffer */

    return 0;
}

#define OFFSET(x) offsetof(ApvEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const enum AVPixelFormat supported_pixel_formats[] = {
    AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV422P12,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_NONE
};

static const AVOption liboapv_options[] = {
    { "preset", "Encoding preset for setting encoding speed (optimization level control)", OFFSET(preset_id), AV_OPT_TYPE_INT, { .i64 = OAPV_PRESET_DEFAULT }, OAPV_PRESET_FASTEST, OAPV_PRESET_PLACEBO, VE, .unit = "preset" },
    { "fastest", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_FASTEST }, 0, 0, VE, .unit = "preset" },
    { "fast",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_FAST },    0, 0, VE, .unit = "preset" },
    { "medium",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_MEDIUM },  0, 0, VE, .unit = "preset" },
    { "slow",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_SLOW },    0, 0, VE, .unit = "preset" },
    { "placebo", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_PLACEBO }, 0, 0, VE, .unit = "preset" },
    { "default", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_DEFAULT }, 0, 0, VE, .unit = "preset" },

    { "qp", "Quantization parameter value for CQP rate control mode", OFFSET(qp), AV_OPT_TYPE_INT, { .i64 = 32 }, 0, 63, VE },
    { "oapv-params",  "Override the apv configuration using a :-separated list of key=value parameters", OFFSET(oapv_params), AV_OPT_TYPE_DICT, { 0 }, 0, 0, VE },

    { "master-display",
      "Mastering display color volume metadata (SMPTE 2086)"
      "Format: G(x,y)B(x,y)R(x,y)WP(x,y)L(max,min)",
      OFFSET(mastering_display_string),
        AV_OPT_TYPE_STRING,
        {.str = NULL},
        0,
        0,
        AV_OPT_FLAG_ENCODING_PARAM
    },

    {
        "max-cll",
        "Maximum Content Light Level (MaxCLL) and Maximum Frame-Average Light Level (MaxFALL) metadata (CTA-861.3). "
        "Format: <max_cll>,<max_fall> (e.g., 1000,400)",
        OFFSET(content_light_string),
        AV_OPT_TYPE_STRING,
        {.str = NULL},
        0,
        0,
        AV_OPT_FLAG_ENCODING_PARAM
    },

    { NULL }
};

static const AVClass liboapve_class = {
    .class_name = "liboapv",
    .item_name  = av_default_item_name,
    .option     = liboapv_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault liboapve_defaults[] = {
    { "b", "0" },       // bitrate in terms of kilo-bits per second (support for bit-rates from a few hundred Mbps to a few Gbps for 2K, 4K and 8K resolution content)
    { NULL },
};

const FFCodec ff_liboapv_encoder = {
    .p.name             = "liboapv",
    .p.long_name        = NULL_IF_CONFIG_SMALL("liboapv APV"),
    .p.type             = AVMEDIA_TYPE_VIDEO,
    .p.id               = AV_CODEC_ID_APV,
    .init               = liboapve_init,
    FF_CODEC_ENCODE_CB(liboapve_encode),
    .close              = liboapve_close,
    .priv_data_size     = sizeof(ApvEncContext),
    .p.priv_class       = &liboapve_class,
    .defaults           = liboapve_defaults,
    .p.capabilities     = AV_CODEC_CAP_OTHER_THREADS | AV_CODEC_CAP_DR1,
    .p.wrapper_name     = "liboapv",
    .p.pix_fmts         = supported_pixel_formats,
    .p.profiles         = NULL_IF_CONFIG_SMALL(ff_apv_profiles),
    .caps_internal      = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_AUTO_THREADS | FF_CODEC_CAP_NOT_INIT_THREADSAFE,
};
