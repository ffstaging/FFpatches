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
#include "libavutil/fifo.h"

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

typedef struct OpaqueData {
    int64_t pkt_pos;
    void *opaque;
    AVBufferRef *opaque_ref;
} OpaqueData;

typedef struct XCoderDecContext {
    AVClass *avclass;

    /* from the command line, which resource allocation method we use */
    char *dev_xcoder;
    char *dev_xcoder_name;          /* dev name of the xcoder card to use */
    char *blk_xcoder_name;          /* blk name of the xcoder card to use */
    int dev_dec_idx;                /* user-specified decoder index */
    char *dev_blk_name;             /* user-specified decoder block device name */
    int keep_alive_timeout;         /* keep alive timeout setting */
    ni_device_context_t *rsrc_ctx;  /* resource management context */

    ni_session_context_t api_ctx;
    ni_xcoder_params_t api_param;
    ni_session_data_io_t api_pkt;

    AVPacket buffered_pkt;
    AVPacket lone_sei_pkt;

    // stream header copied/saved from AVCodecContext.extradata
    int got_first_key_frame;
    uint8_t *extradata;
    int extradata_size;

    int64_t current_pts;
    unsigned long long offset;
    int svct_skip_next_packet;

    int started;
    int draining;
    int flushing;
    int is_lone_sei_pkt;
    int eos;
    AVHWFramesContext    *frames;

    /* for temporarily storing the opaque pointers when AV_CODEC_FLAG_COPY_OPAQUE is set */
    OpaqueData *opaque_data_array;
    int opaque_data_nb;
    int opaque_data_pos;

    /* below are all command line options */
    char *xcoder_opts;
    int low_delay;
    int pkt_nal_bitmap;
} XCoderDecContext;

typedef struct XCoderEncContext {
    AVClass *avclass;

    /* from the command line, which resource allocation method we use */
    char *dev_xcoder;
    char *dev_xcoder_name;          /* dev name of the xcoder card to use */
    char *blk_xcoder_name;          /* blk name of the xcoder card to use */
    int dev_enc_idx;                /* user-specified encoder index */
    char *dev_blk_name;             /* user-specified encoder block device name */
    int nvme_io_size;               /* custom nvme io size */
    int keep_alive_timeout;         /* keep alive timeout setting */
    ni_device_context_t *rsrc_ctx;  /* resource management context */
    uint64_t xcode_load_pixel; /* xcode load in pixels by this encode task */

    AVFifo *fme_fifo;
    int eos_fme_received;
    AVFrame buffered_fme; // buffered frame for sequence change handling

    ni_session_data_io_t  api_pkt; /* used for receiving bitstream from xcoder */
    ni_session_data_io_t   api_fme; /* used for sending YUV data to xcoder */
    ni_session_context_t api_ctx;
    ni_xcoder_params_t api_param;

    int started;
    uint8_t *p_spsPpsHdr;
    int spsPpsHdrLen;
    int spsPpsArrived;
    int firstPktArrived;
    int64_t dtsOffset;
    int gop_offset_count;/*this is a counter to guess the pts only dtsOffset times*/
    uint64_t total_frames_received;
    int64_t first_frame_pts;
    int64_t latest_dts;

    int encoder_flushing;
    int encoder_eof;

    // ROI
    int roi_side_data_size;
    AVRegionOfInterest *av_rois;  // last passed in AVRegionOfInterest
    int nb_rois;

    /* backup copy of original values of -enc command line option */
    int  orig_dev_enc_idx;

    AVFrame *sframe_pool[MAX_NUM_FRAMEPOOL_HWAVFRAME];
    int aFree_Avframes_list[MAX_NUM_FRAMEPOOL_HWAVFRAME + 1];
    int freeHead;
    int freeTail;

    /* below are all command line options */
    char *xcoder_opts;
    char *xcoder_gop;
    int gen_global_headers;
    int udu_sei;

    int reconfigCount;
    int seqChangeCount;
    // actual enc_change_params is in ni_session_context !

} XCoderEncContext;

// copy maximum number of bytes of a string from src to dst, ensuring null byte
// terminated
static inline void ff_xcoder_strncpy(char *dst, const char *src, int max) {
    if (dst && src && max) {
        *dst = '\0';
        strncpy(dst, src, max);
        *(dst + max - 1) = '\0';
    }
}

int ff_xcoder_dec_close(AVCodecContext *avctx,
                        XCoderDecContext *s);

int ff_xcoder_dec_init(AVCodecContext *avctx,
                       XCoderDecContext *s);

int ff_xcoder_dec_send(AVCodecContext *avctx,
                       XCoderDecContext *s,
                       AVPacket *pkt);

int ff_xcoder_dec_receive(AVCodecContext *avctx,
                          XCoderDecContext *s,
                          AVFrame *frame,
                          bool wait);

int ff_xcoder_dec_is_flushing(AVCodecContext *avctx,
                              XCoderDecContext *s);

int ff_xcoder_dec_flush(AVCodecContext *avctx,
                        XCoderDecContext *s);

int parse_symbolic_decoder_param(XCoderDecContext *s);

int retrieve_frame(AVCodecContext *avctx, AVFrame *data, int *got_frame,
                   ni_frame_t *xfme);
int ff_xcoder_add_headers(AVCodecContext *avctx, AVPacket *pkt,
                          uint8_t *extradata, int extradata_size);
#endif /* AVCODEC_NICODEC_H */
