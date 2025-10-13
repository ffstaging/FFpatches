/*
 * WebRTC-HTTP ingestion protocol (WHIP) muxer
 * Copyright (c) 2023 The FFmpeg Project
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

#include "libavcodec/avcodec.h"
#include "libavcodec/codec_desc.h"
#include "libavcodec/h264.h"
#include "libavcodec/startcode.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/random_seed.h"
#include "libavutil/time.h"
#include "avc.h"
#include "nal.h"
#include "avio_internal.h"
#include "internal.h"
#include "mux.h"
#include "rtp.h"
#include "rtc.h"

/**
 * The maximum size of the Secure Real-time Transport Protocol (SRTP) HMAC checksum
 * and padding that is appended to the end of the packet. To calculate the maximum
 * size of the User Datagram Protocol (UDP) packet that can be sent out, subtract
 * this size from the `pkt_size`.
 */
#define DTLS_SRTP_CHECKSUM_LEN 16

/**
 * Refer to RFC 7675 5.1,
 *
 * To prevent expiry of consent, a STUN binding request can be sent periodically.
 * Implementations SHOULD set a default interval of 5 seconds(5000ms).
 *
 * Consent expires after 30 seconds(30000ms).
 */
#define WHIP_ICE_CONSENT_CHECK_INTERVAL 5000
#define WHIP_ICE_CONSENT_EXPIRED_TIMER 30000

/* Calculate the elapsed time from starttime to endtime in milliseconds. */
#define ELAPSED(starttime, endtime) ((float)(endtime - starttime) / 1000)

/**
 * When duplicating a stream, the demuxer has already set the extradata, profile, and
 * level of the par. Keep in mind that this function will not be invoked since the
 * profile and level are set.
 *
 * When utilizing an encoder, such as libx264, to encode a stream, the extradata in
 * par->extradata contains the SPS, which includes profile and level information.
 * However, the profile and level of par remain unspecified. Therefore, it is necessary
 * to extract the profile and level data from the extradata and assign it to the par's
 * profile and level. Keep in mind that AVFMT_GLOBALHEADER must be enabled; otherwise,
 * the extradata will remain empty.
 */
static int parse_profile_level(AVFormatContext *s, AVCodecParameters *par)
{
    int ret = 0;
    const uint8_t *r = par->extradata, *r1, *end = par->extradata + par->extradata_size;
    H264SPS seq, *const sps = &seq;
    uint32_t state;
    RTCContext *rtc = s->priv_data;

    if (par->codec_id != AV_CODEC_ID_H264)
        return ret;

    if (par->profile != AV_PROFILE_UNKNOWN && par->level != AV_LEVEL_UNKNOWN)
        return ret;

    if (!par->extradata || par->extradata_size <= 0) {
        av_log(rtc, AV_LOG_ERROR, "Unable to parse profile from empty extradata=%p, size=%d\n",
            par->extradata, par->extradata_size);
        return AVERROR(EINVAL);
    }

    while (1) {
        r = avpriv_find_start_code(r, end, &state);
        if (r >= end)
            break;

        r1 = ff_nal_find_startcode(r, end);
        if ((state & 0x1f) == H264_NAL_SPS) {
            ret = ff_avc_decode_sps(sps, r, r1 - r);
            if (ret < 0) {
                av_log(rtc, AV_LOG_ERROR, "Failed to decode SPS, state=%x, size=%d\n",
                    state, (int)(r1 - r));
                return ret;
            }

            av_log(rtc, AV_LOG_VERBOSE, "Parse profile=%d, level=%d from SPS\n",
                sps->profile_idc, sps->level_idc);
            par->profile = sps->profile_idc;
            par->level = sps->level_idc;
        }

        r = r1;
    }

    return ret;
}

/**
 * Parses video SPS/PPS from the extradata of codecpar and checks the codec.
 * Currently only supports video(h264) and audio(opus). Note that only baseline
 * and constrained baseline profiles of h264 are supported.
 *
 * If the profile is less than 0, the function considers the profile as baseline.
 * It may need to parse the profile from SPS/PPS. This situation occurs when ingesting
 * desktop and transcoding.
 *
 * @param s Pointer to the AVFormatContext
 * @returns Returns 0 if successful or AVERROR_xxx in case of an error.
 *
 * TODO: FIXME: There is an issue with the timestamp of OPUS audio, especially when
 *  the input is an MP4 file. The timestamp deviates from the expected value of 960,
 *  causing Chrome to play the audio stream with noise. This problem can be replicated
 *  by transcoding a specific file into MP4 format and publishing it using the WHIP
 *  muxer. However, when directly transcoding and publishing through the WHIP muxer,
 *  the issue is not present, and the audio timestamp remains consistent. The root
 *  cause is still unknown, and this comment has been added to address this issue
 *  in the future. Further research is needed to resolve the problem.
 */
static int parse_codec(AVFormatContext *s)
{
    int i, ret = 0;
    RTCContext *rtc = s->priv_data;

    for (i = 0; i < s->nb_streams; i++) {
        AVCodecParameters *par = s->streams[i]->codecpar;
        const AVCodecDescriptor *desc = avcodec_descriptor_get(par->codec_id);
        switch (par->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            if (rtc->video_par) {
                av_log(rtc, AV_LOG_ERROR, "Only one video stream is supported by RTC\n");
                return AVERROR(EINVAL);
            }
            rtc->video_par = par;

            if (par->codec_id != AV_CODEC_ID_H264) {
                av_log(rtc, AV_LOG_ERROR, "Unsupported video codec %s by RTC, choose h264\n",
                       desc ? desc->name : "unknown");
                return AVERROR_PATCHWELCOME;
            }

            if (par->video_delay > 0) {
                av_log(rtc, AV_LOG_ERROR, "Unsupported B frames by RTC\n");
                return AVERROR_PATCHWELCOME;
            }

            if ((ret = parse_profile_level(s, par)) < 0) {
                av_log(rtc, AV_LOG_ERROR, "Failed to parse SPS/PPS from extradata\n");
                return AVERROR(EINVAL);
            }

            if (par->profile == AV_PROFILE_UNKNOWN) {
                av_log(rtc, AV_LOG_WARNING, "No profile found in extradata, consider baseline\n");
                return AVERROR(EINVAL);
            }
            if (par->level == AV_LEVEL_UNKNOWN) {
                av_log(rtc, AV_LOG_WARNING, "No level found in extradata, consider 3.1\n");
                return AVERROR(EINVAL);
            }
            break;
        case AVMEDIA_TYPE_AUDIO:
            if (rtc->audio_par) {
                av_log(rtc, AV_LOG_ERROR, "Only one audio stream is supported by RTC\n");
                return AVERROR(EINVAL);
            }
            rtc->audio_par = par;

            if (par->codec_id != AV_CODEC_ID_OPUS) {
                av_log(rtc, AV_LOG_ERROR, "Unsupported audio codec %s by RTC, choose opus\n",
                    desc ? desc->name : "unknown");
                return AVERROR_PATCHWELCOME;
            }

            if (par->ch_layout.nb_channels != 2) {
                av_log(rtc, AV_LOG_ERROR, "Unsupported audio channels %d by RTC, choose stereo\n",
                    par->ch_layout.nb_channels);
                return AVERROR_PATCHWELCOME;
            }

            if (par->sample_rate != 48000) {
                av_log(rtc, AV_LOG_ERROR, "Unsupported audio sample rate %d by RTC, choose 48000\n", par->sample_rate);
                return AVERROR_PATCHWELCOME;
            }
            break;
        default:
            av_log(rtc, AV_LOG_ERROR, "Codec type '%s' for stream %d is not supported by RTC\n",
                   av_get_media_type_string(par->codec_type), i);
            return AVERROR_PATCHWELCOME;
        }
    }

    return ret;
}

/**
 * Callback triggered by the RTP muxer when it creates and sends out an RTP packet.
 *
 * This function modifies the video STAP packet, removing the markers, and updating the
 * NRI of the first NALU. Additionally, it uses the corresponding SRTP context to encrypt
 * the RTP packet, where the video packet is handled by the video SRTP context.
 */
static int on_rtp_write_packet(void *opaque, const uint8_t *buf, int buf_size)
{
    int ret, cipher_size, is_rtcp, is_video;
    uint8_t payload_type;
    AVFormatContext *s = opaque;
    RTCContext *rtc = s->priv_data;
    SRTPContext *srtp;

    /* Ignore if not RTP or RTCP packet. */
    if (!ff_rtc_media_is_rtp_rtcp(buf, buf_size))
        return 0;

    /* Only support audio, video and rtcp. */
    is_rtcp = ff_rtc_media_is_rtcp(buf, buf_size);
    payload_type = buf[1] & 0x7f;
    is_video = payload_type == rtc->video_payload_type;
    if (!is_rtcp && payload_type != rtc->video_payload_type && payload_type != rtc->audio_payload_type)
        return 0;

    /* Get the corresponding SRTP context. */
    srtp = is_rtcp ? &rtc->srtp_rtcp_send : (is_video? &rtc->srtp_video_send : &rtc->srtp_audio_send);

    /* Encrypt by SRTP and send out. */
    cipher_size = ff_srtp_encrypt(srtp, buf, buf_size, rtc->buf, rtc->bufsize);
    if (cipher_size <= 0 || cipher_size < buf_size) {
        av_log(rtc, AV_LOG_WARNING, "Failed to encrypt packet=%dB, cipher=%dB\n", buf_size, cipher_size);
        return 0;
    }

    ret = ffurl_write(rtc->udp, rtc->buf, cipher_size);
    if (ret < 0) {
        av_log(rtc, AV_LOG_ERROR, "Failed to write packet=%dB, ret=%d\n", cipher_size, ret);
        return ret;
    }

    return ret;
}

/**
 * Creates dedicated RTP muxers for each stream in the AVFormatContext to build RTP
 * packets from the encoded frames.
 *
 * The corresponding SRTP context is utilized to encrypt each stream's RTP packets. For
 * example, a video SRTP context is used for the video stream. Additionally, the
 * "on_rtp_write_packet" callback function is set as the write function for each RTP
 * muxer to send out encrypted RTP packets.
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int create_rtp_muxer(AVFormatContext *s)
{
    int ret, i, is_video, buffer_size, max_packet_size;
    AVFormatContext *rtp_ctx = NULL;
    AVDictionary *opts = NULL;
    uint8_t *buffer = NULL;
    char buf[64];
    RTCContext *rtc = s->priv_data;
    rtc->udp->flags |= AVIO_FLAG_NONBLOCK;

    const AVOutputFormat *rtp_format = av_guess_format("rtp", NULL, NULL);
    if (!rtp_format) {
        av_log(rtc, AV_LOG_ERROR, "Failed to guess rtp muxer\n");
        ret = AVERROR(ENOSYS);
        goto end;
    }

    /* The UDP buffer size, may greater than MTU. */
    buffer_size = MAX_UDP_BUFFER_SIZE;
    /* The RTP payload max size. Reserved some bytes for SRTP checksum and padding. */
    max_packet_size = rtc->pkt_size - DTLS_SRTP_CHECKSUM_LEN;

    for (i = 0; i < s->nb_streams; i++) {
        rtp_ctx = avformat_alloc_context();
        if (!rtp_ctx) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        rtp_ctx->oformat = rtp_format;
        if (!avformat_new_stream(rtp_ctx, NULL)) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        /* Pass the interrupt callback on */
        rtp_ctx->interrupt_callback = s->interrupt_callback;
        /* Copy the max delay setting; the rtp muxer reads this. */
        rtp_ctx->max_delay = s->max_delay;
        /* Copy other stream parameters. */
        rtp_ctx->streams[0]->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        rtp_ctx->flags |= s->flags & AVFMT_FLAG_BITEXACT;
        rtp_ctx->strict_std_compliance = s->strict_std_compliance;

        /* Set the synchronized start time. */
        rtp_ctx->start_time_realtime = s->start_time_realtime;

        avcodec_parameters_copy(rtp_ctx->streams[0]->codecpar, s->streams[i]->codecpar);
        rtp_ctx->streams[0]->time_base = s->streams[i]->time_base;

        /**
         * For H.264, consistently utilize the annexb format through the Bitstream Filter (BSF);
         * therefore, we deactivate the extradata detection for the RTP muxer.
         */
        if (s->streams[i]->codecpar->codec_id == AV_CODEC_ID_H264) {
            av_freep(&rtp_ctx->streams[i]->codecpar->extradata);
            rtp_ctx->streams[i]->codecpar->extradata_size = 0;
        }

        buffer = av_malloc(buffer_size);
        if (!buffer) {
            ret = AVERROR(ENOMEM);
            goto end;
        }

        rtp_ctx->pb = avio_alloc_context(buffer, buffer_size, 1, s, NULL, on_rtp_write_packet, NULL);
        if (!rtp_ctx->pb) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        rtp_ctx->pb->max_packet_size = max_packet_size;
        rtp_ctx->pb->av_class = &ff_avio_class;

        is_video = s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
        snprintf(buf, sizeof(buf), "%d", is_video? rtc->video_payload_type : rtc->audio_payload_type);
        av_dict_set(&opts, "payload_type", buf, 0);
        snprintf(buf, sizeof(buf), "%d", is_video? rtc->video_ssrc : rtc->audio_ssrc);
        av_dict_set(&opts, "ssrc", buf, 0);
        av_dict_set_int(&opts, "seq", is_video ? rtc->video_first_seq : rtc->audio_first_seq, 0);

        ret = avformat_write_header(rtp_ctx, &opts);
        if (ret < 0) {
            av_log(rtc, AV_LOG_ERROR, "Failed to write rtp header\n");
            goto end;
        }

        ff_format_set_url(rtp_ctx, av_strdup(s->url));
        s->streams[i]->time_base = rtp_ctx->streams[0]->time_base;
        s->streams[i]->priv_data = rtp_ctx;
        rtp_ctx = NULL;
    }

    if (rtc->state < RTC_STATE_READY)
        rtc->state = RTC_STATE_READY;
    av_log(rtc, AV_LOG_INFO, "Muxer state=%d, buffer_size=%d, max_packet_size=%d, "
                           "elapsed=%.2fms(init:%.2f,offer:%.2f,answer:%.2f,udp:%.2f,ice:%.2f,dtls:%.2f,srtp:%.2f)\n",
        rtc->state, buffer_size, max_packet_size, ELAPSED(rtc->rtc_starttime, av_gettime_relative()),
        ELAPSED(rtc->rtc_starttime,   rtc->rtc_init_time),
        ELAPSED(rtc->rtc_init_time,   rtc->rtc_offer_time),
        ELAPSED(rtc->rtc_offer_time,  rtc->rtc_answer_time),
        ELAPSED(rtc->rtc_answer_time, rtc->rtc_udp_time),
        ELAPSED(rtc->rtc_udp_time,    rtc->rtc_ice_time),
        ELAPSED(rtc->rtc_ice_time,    rtc->rtc_dtls_time),
        ELAPSED(rtc->rtc_dtls_time,   rtc->rtc_srtp_time));

end:
    if (rtp_ctx)
        avio_context_free(&rtp_ctx->pb);
    avformat_free_context(rtp_ctx);
    av_dict_free(&opts);
    return ret;
}

/**
 * Since the h264_mp4toannexb filter only processes the MP4 ISOM format and bypasses
 * the annexb format, it is necessary to manually insert encoder metadata before each
 * IDR when dealing with annexb format packets. For instance, in the case of H.264,
 * we must insert SPS and PPS before the IDR frame.
 */
static int h264_annexb_insert_sps_pps(AVFormatContext *s, AVPacket *pkt)
{
    int ret = 0;
    AVPacket *in = NULL;
    AVCodecParameters *par = s->streams[pkt->stream_index]->codecpar;
    uint32_t nal_size = 0, out_size = par ? par->extradata_size : 0;
    uint8_t unit_type, sps_seen = 0, pps_seen = 0, idr_seen = 0, *out;
    const uint8_t *buf, *buf_end, *r1;

    if (!par || !par->extradata || par->extradata_size <= 0)
        return ret;

    /* Discover NALU type from packet. */
    buf_end  = pkt->data + pkt->size;
    for (buf = ff_nal_find_startcode(pkt->data, buf_end); buf < buf_end; buf += nal_size) {
        while (!*(buf++));
        r1 = ff_nal_find_startcode(buf, buf_end);
        if ((nal_size = r1 - buf) > 0) {
            unit_type = *buf & 0x1f;
            if (unit_type == H264_NAL_SPS) {
                sps_seen = 1;
            } else if (unit_type == H264_NAL_PPS) {
                pps_seen = 1;
            } else if (unit_type == H264_NAL_IDR_SLICE) {
                idr_seen = 1;
            }

            out_size += 3 + nal_size;
        }
    }

    if (!idr_seen || (sps_seen && pps_seen))
        return ret;

    /* See av_bsf_send_packet */
    in = av_packet_alloc();
    if (!in)
        return AVERROR(ENOMEM);

    ret = av_packet_make_refcounted(pkt);
    if (ret < 0)
        goto fail;

    av_packet_move_ref(in, pkt);

    /* Create a new packet with sps/pps inserted. */
    ret = av_new_packet(pkt, out_size);
    if (ret < 0)
        goto fail;

    ret = av_packet_copy_props(pkt, in);
    if (ret < 0)
        goto fail;

    memcpy(pkt->data, par->extradata, par->extradata_size);
    out = pkt->data + par->extradata_size;
    buf_end  = in->data + in->size;
    for (buf = ff_nal_find_startcode(in->data, buf_end); buf < buf_end; buf += nal_size) {
        while (!*(buf++));
        r1 = ff_nal_find_startcode(buf, buf_end);
        if ((nal_size = r1 - buf) > 0) {
            AV_WB24(out, 0x00001);
            memcpy(out + 3, buf, nal_size);
            out += 3 + nal_size;
        }
    }

fail:
    if (ret < 0)
        av_packet_unref(pkt);
    av_packet_free(&in);

    return ret;
}

static av_cold int whip_init(AVFormatContext *s)
{
    int ret;
    RTCContext *rtc = s->priv_data;

    if ((ret = ff_rtc_initialize(s)) < 0)
        goto end;

    if ((ret = parse_codec(s)) < 0)
        goto end;

    if ((ret = ff_rtc_connect(s)) < 0)
        goto end;

    if ((ret = create_rtp_muxer(s)) < 0)
        goto end;

end:
    if (ret < 0)
        rtc->state = RTC_STATE_FAILED;
    return ret;
}

static void handle_nack_rtx(AVFormatContext *s, int size)
{
    int ret;
    RTCContext *rtc = s->priv_data;
    uint8_t *buf = NULL;
    int rtcp_len, srtcp_len, header_len = 12/*RFC 4585 6.1*/;

    /**
     * Refer to RFC 3550 6.4.1
     * The length of this RTCP packet in 32 bit words minus one,
     * including the header and any padding.
     */
    rtcp_len = (AV_RB16(&rtc->buf[2]) + 1) * 4;
    if (rtcp_len <= header_len) {
        av_log(rtc, AV_LOG_WARNING, "NACK packet is broken, size: %d\n", rtcp_len);
        goto error;
    }
    /* SRTCP index(4 bytes) + HMAC(SRTP_ARS128_CM_SHA1_80) 10bytes */
    srtcp_len = rtcp_len + 4 + 10;
    if (srtcp_len != size) {
        av_log(rtc, AV_LOG_WARNING, "NACK packet size not match, srtcp_len:%d, size:%d\n", srtcp_len, size);
        goto error;
    }
    buf = av_memdup(rtc->buf, srtcp_len);
    if (!buf)
        goto error;
    if ((ret = ff_srtp_decrypt(&rtc->srtp_recv, buf, &srtcp_len)) < 0) {
        av_log(rtc, AV_LOG_WARNING, "NACK packet decrypt failed: %d\n", ret);
        goto error;
    }
    goto end;
error:
    av_log(rtc, AV_LOG_WARNING, "Failed to handle NACK and RTX, Skip...\n");
end:
    av_freep(&buf);
}

static int whip_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    RTCContext *rtc = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    AVFormatContext *rtp_ctx = st->priv_data;

    int64_t now = av_gettime_relative();
    /**
     * Refer to RFC 7675
     * Periodically send Consent Freshness STUN Binding Request
     */
    if (now - rtc->rtc_last_consent_tx_time > WHIP_ICE_CONSENT_CHECK_INTERVAL * RTC_US_PER_MS) {
        int size;
        ret = ff_rtc_ice_create_request(s, rtc->buf, rtc->bufsize, &size);
        if (ret < 0) {
            av_log(rtc, AV_LOG_ERROR, "Failed to create STUN binding request, size=%d\n", size);
            goto end;
        }
        ret = ffurl_write(rtc->udp, rtc->buf, size);
        if (ret < 0) {
            av_log(rtc, AV_LOG_ERROR, "Failed to send STUN binding request, size=%d\n", size);
            goto end;
        }
        rtc->rtc_last_consent_tx_time = now;
        av_log(rtc, AV_LOG_DEBUG, "Consent Freshness check sent\n");
    }

    /**
     * Receive packets from the server such as ICE binding requests, DTLS messages,
     * and RTCP like PLI requests, then respond to them.
     */
    ret = ffurl_read(rtc->udp, rtc->buf, rtc->bufsize);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN))
            goto write_packet;
        av_log(rtc, AV_LOG_ERROR, "Failed to read from UDP socket\n");
        goto end;
    }
    if (!ret) {
        av_log(rtc, AV_LOG_ERROR, "Receive EOF from UDP socket\n");
        goto end;
    }

    if (ff_rtc_ice_is_binding_response(rtc->buf, ret)) {
        rtc->rtc_last_consent_rx_time = av_gettime_relative();
        av_log(rtc, AV_LOG_DEBUG, "Consent Freshness check received\n");
    }

    if (ff_rtc_is_dtls_packet(rtc->buf, ret)) {
        if ((ret = ffurl_write(rtc->dtls_uc, rtc->buf, ret)) < 0) {
            av_log(rtc, AV_LOG_ERROR, "Failed to handle DTLS message\n");
            goto end;
        }
    }
    if (ff_rtc_media_is_rtcp(rtc->buf, ret)) {
        uint8_t fmt = rtc->buf[0] & 0x1f;
        uint8_t pt = rtc->buf[1];
        /**
         * Handle RTCP NACK packet
         * Refer to RFC 4585 6.2.1
         * The Generic NACK message is identified by PT=RTPFB and FMT=1
         */
        if (pt != RTCP_RTPFB)
            goto write_packet;
        if (fmt == 1)
            handle_nack_rtx(s, ret);
    }
write_packet:
    now = av_gettime_relative();
    if (now - rtc->rtc_last_consent_rx_time > WHIP_ICE_CONSENT_EXPIRED_TIMER * RTC_US_PER_MS) {
        av_log(rtc, AV_LOG_ERROR,
            "Consent Freshness expired after %.2fms (limited %dms), terminate session\n",
            ELAPSED(now, rtc->rtc_last_consent_rx_time), WHIP_ICE_CONSENT_EXPIRED_TIMER);
        ret = AVERROR(ETIMEDOUT);
        goto end;
    }

    if (rtc->h264_annexb_insert_sps_pps && st->codecpar->codec_id == AV_CODEC_ID_H264) {
        if ((ret = h264_annexb_insert_sps_pps(s, pkt)) < 0) {
            av_log(rtc, AV_LOG_ERROR, "Failed to insert SPS/PPS before IDR\n");
            goto end;
        }
    }

    ret = ff_write_chained(rtp_ctx, 0, pkt, s, 0);
    if (ret < 0) {
        if (ret == AVERROR(EINVAL)) {
            av_log(rtc, AV_LOG_WARNING, "Ignore failed to write packet=%dB, ret=%d\n", pkt->size, ret);
            ret = 0;
        } else if (ret == AVERROR(EAGAIN)) {
            av_log(rtc, AV_LOG_ERROR, "UDP send blocked, please increase the buffer via -buffer_size\n");
        } else
            av_log(rtc, AV_LOG_ERROR, "Failed to write packet, size=%d, ret=%d\n", pkt->size, ret);
        goto end;
    }

end:
    if (ret < 0)
        rtc->state = RTC_STATE_FAILED;
    return ret;
}

static av_cold void whip_deinit(AVFormatContext *s)
{
    ff_rtc_close(s);
}

static int whip_check_bitstream(AVFormatContext *s, AVStream *st, const AVPacket *pkt)
{
    int ret = 1, extradata_isom = 0;
    uint8_t *b = pkt->data;
    RTCContext *rtc = s->priv_data;

    if (st->codecpar->codec_id == AV_CODEC_ID_H264) {
        extradata_isom = st->codecpar->extradata_size > 0 && st->codecpar->extradata[0] == 1;
        if (pkt->size >= 5 && AV_RB32(b) != 0x0000001 && (AV_RB24(b) != 0x000001 || extradata_isom)) {
            ret = ff_stream_add_bitstream_filter(st, "h264_mp4toannexb", NULL);
            av_log(rtc, AV_LOG_VERBOSE, "Enable BSF h264_mp4toannexb, packet=[%x %x %x %x %x ...], extradata_isom=%d\n",
                b[0], b[1], b[2], b[3], b[4], extradata_isom);
        } else
            rtc->h264_annexb_insert_sps_pps = 1;
    }

    return ret;
}

static const AVClass whip_muxer_class = {
    .class_name = "WHIP muxer",
    .item_name  = av_default_item_name,
    .option     = ff_rtc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFOutputFormat ff_whip_muxer = {
    .p.name             = "whip",
    .p.long_name        = NULL_IF_CONFIG_SMALL("WHIP(WebRTC-HTTP ingestion protocol) muxer"),
    .p.audio_codec      = AV_CODEC_ID_OPUS,
    .p.video_codec      = AV_CODEC_ID_H264,
    .p.flags            = AVFMT_GLOBALHEADER | AVFMT_NOFILE | AVFMT_EXPERIMENTAL,
    .p.priv_class       = &whip_muxer_class,
    .priv_data_size     = sizeof(RTCContext),
    .init               = whip_init,
    .write_packet       = whip_write_packet,
    .deinit             = whip_deinit,
    .check_bitstream    = whip_check_bitstream,
};
