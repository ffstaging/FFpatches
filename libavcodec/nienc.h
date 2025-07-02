/*
 * NetInt XCoder H.264/HEVC Encoder common code header
 * Copyright (c) 2018-2019 NetInt
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

#ifndef AVCODEC_NIENC_H
#define AVCODEC_NIENC_H

#include <ni_rsrc_api.h>
#include <ni_device_api.h>
#include <ni_util.h>

#include "libavutil/internal.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "internal.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"

#include "hwconfig.h"
#include "nicodec.h"

#define OFFSETENC(x) offsetof(XCoderEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

// Common Netint encoder options
#define NI_ENC_OPTIONS\
    { "xcoder", "Select which XCoder card to use.", OFFSETENC(dev_xcoder), \
      AV_OPT_TYPE_STRING, { .str = NI_BEST_MODEL_LOAD_STR }, CHAR_MIN, CHAR_MAX, VE, "xcoder" }, \
    {     "bestmodelload", "Pick the least model load XCoder/encoder available.", 0, AV_OPT_TYPE_CONST, \
          { .str = NI_BEST_MODEL_LOAD_STR }, 0, 0, VE, "xcoder" }, \
    {     "bestload", "Pick the least real load XCoder/encoder available.", 0, AV_OPT_TYPE_CONST, \
          { .str = NI_BEST_REAL_LOAD_STR }, 0, 0, VE, "xcoder" }, \
    \
    { "ni_enc_idx", "Select which encoder to use by index. First is 0, second is 1, and so on.", \
      OFFSETENC(dev_enc_idx), AV_OPT_TYPE_INT, { .i64 = BEST_DEVICE_LOAD }, -1, INT_MAX, VE }, \
    \
    { "ni_enc_name", "Select which encoder to use by NVMe block device name, e.g. /dev/nvme0n1.", \
      OFFSETENC(dev_blk_name), AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE }, \
    \
    { "encname", "Select which encoder to use by NVMe block device name, e.g. /dev/nvme0n1.", \
      OFFSETENC(dev_blk_name), AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE }, \
    \
    { "iosize", "Specify a custom NVMe IO transfer size (multiples of 4096 only).", \
      OFFSETENC(nvme_io_size), AV_OPT_TYPE_INT, { .i64 = BEST_DEVICE_LOAD }, -1, INT_MAX, VE }, \
    \
    { "xcoder-params", "Set the XCoder configuration using a :-separated list of key=value parameters.", \
      OFFSETENC(xcoder_opts), AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE }, \
    \
    { "xcoder-gop", "Set the XCoder custom gop using a :-separated list of key=value parameters.", \
      OFFSETENC(xcoder_gop), AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE }, \
    \
    { "keep_alive_timeout", "Specify a custom session keep alive timeout in seconds.", \
      OFFSETENC(keep_alive_timeout), AV_OPT_TYPE_INT, { .i64 = NI_DEFAULT_KEEP_ALIVE_TIMEOUT }, \
      NI_MIN_KEEP_ALIVE_TIMEOUT, NI_MAX_KEEP_ALIVE_TIMEOUT, VE }

// "gen_global_headers" encoder options
#define NI_ENC_OPTION_GEN_GLOBAL_HEADERS\
    { "gen_global_headers", "Generate SPS and PPS headers during codec initialization.", \
      OFFSETENC(gen_global_headers), AV_OPT_TYPE_INT, { .i64 = GEN_GLOBAL_HEADERS_AUTO }, \
      GEN_GLOBAL_HEADERS_AUTO, GEN_GLOBAL_HEADERS_ON, VE, "gen_global_headers" }, \
    {     "auto", NULL, 0, AV_OPT_TYPE_CONST, \
          { .i64 = GEN_GLOBAL_HEADERS_AUTO }, 0, 0, VE, "gen_global_headers" }, \
    {     "off", NULL, 0, AV_OPT_TYPE_CONST, \
          { .i64 = GEN_GLOBAL_HEADERS_OFF }, 0, 0, VE, "gen_global_headers" }, \
    {     "on", NULL, 0, AV_OPT_TYPE_CONST, \
          { .i64 = GEN_GLOBAL_HEADERS_ON }, 0, 0, VE, "gen_global_headers" }

#define NI_ENC_OPTION_UDU_SEI \
    { "udu_sei", "Pass through user data unregistered SEI if available", OFFSETENC(udu_sei), \
      AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE }

int xcoder_encode_init(AVCodecContext *avctx);

int xcoder_encode_close(AVCodecContext *avctx);

int xcoder_encode_sequence_change(AVCodecContext *avctx, int width, int height, int bit_depth_factor);

int xcoder_send_frame(AVCodecContext *avctx, const AVFrame *frame);

int xcoder_receive_packet(AVCodecContext *avctx, AVPacket *pkt);

int ff_xcoder_receive_packet(AVCodecContext *avctx, AVPacket *pkt);

bool free_frames_isempty(XCoderEncContext *ctx);

bool free_frames_isfull(XCoderEncContext *ctx);

int deq_free_frames(XCoderEncContext *ctx);

int enq_free_frames(XCoderEncContext *ctx, int idx);

int recycle_index_2_avframe_index(XCoderEncContext *ctx, uint32_t recycleIndex);

extern const AVCodecHWConfigInternal *ff_ni_enc_hw_configs[];

#endif /* AVCODEC_NIENC_H */
