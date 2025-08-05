/*
 * Blu-ray Disc Movie (BDMV) demuxer, powered by libbluray
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

#include <libbluray/bluray.h>
#include <libbluray/filesystem.h>

#include "libavutil/avassert.h"
#include "libavutil/avutil.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avformat.h"
#include "avio_internal.h"
#include "avlanguage.h"
#include "demux.h"
#include "internal.h"
#include "url.h"

#define BDMV_CLIP_PATH_LEN      23
#define BDMV_PTS_WRAP_BITS      64
#define BDMV_TIME_BASE_Q        (AVRational) { 1, 90000 }
#define BDMV_UNIT_SIZE          6144

enum BDMVDemuxDomain {
    BDMV_DOMAIN_MPLS,
    BDMV_DOMAIN_M2TS
};

enum BDMVPendingUnitState {
    BDMV_PENDING_UNIT_NONE,
    BDMV_PENDING_UNIT_PLAYITEM_TRANSITIONAL,
};

typedef struct BDMVVideoStreamEntry {
    int                   pid;
    enum AVCodecID        codec_id;
    int                   width;
    int                   height;
    AVRational            dar;
    AVRational            framerate;
} BDMVVideoStreamEntry;

typedef struct BDMVAudioStreamEntry {
    int                   pid;
    enum AVCodecID        codec_id;
    int                   sample_rate;
    const char            *lang_iso;
} BDMVAudioStreamEntry;

typedef struct BDMVSubtitleStreamEntry {
    int                   pid;
    enum AVCodecID        codec_id;
    const char            *lang_iso;
} BDMVSubtitleStreamEntry;

typedef struct BDMVDemuxContext {
    const AVClass               *class;

    /* options */
    int                         opt_domain;
    int                         opt_angle;
    int                         opt_item;

    /* MPEG-TS subdemuxer */
    AVFormatContext             *mpegts_ctx;
    uint8_t                     *mpegts_buf;
    FFIOContext                 mpegts_pb;

    /* BD disc handle */
    BLURAY                      *bd;
    BLURAY_TITLE_INFO           *bd_mpls;
    int                         bd_nb_titles;

    /* BD clip handle */
    int                         cur_chapter;
    BD_FILE_H                   *cur_clip_file;
    BLURAY_CLIP_INFO            cur_clip_info;
    int64_t                     clip_pts_offset;

    /* pending unit data if event queue requires us to interrupt flow */
    uint8_t                     pending_unit_data[BDMV_UNIT_SIZE];
    size_t                      pending_unit_size;
    enum BDMVPendingUnitState   pending_unit_state;

    /* playback control */
    int                         play_ended;
    int64_t                     pts_offset;
    int64_t                     seek_offset;
    int                         seek_warned;
    int                         subdemux_end;
    int                         subdemux_reset;
} BDMVDemuxContext;

static inline void bdmv_clip_format_m2ts_path(char* dst, const int m2ts_id)
{
    snprintf(dst, BDMV_CLIP_PATH_LEN, "BDMV/STREAM/%05d.m2ts", m2ts_id);
}

static int bdmv_clip_video_stream_analyze(AVFormatContext *s, const BLURAY_STREAM_INFO bd_st_video,
                                          BDMVVideoStreamEntry *entry)
{
    enum AVCodecID codec_id = AV_CODEC_ID_NONE;
    int height           = 0;
    int width            = 0;
    AVRational dar       = (AVRational) { 0, 0 };
    AVRational framerate = (AVRational) { 0, 0 };

    switch (bd_st_video.coding_type) {
        case BLURAY_STREAM_TYPE_VIDEO_MPEG1:
            codec_id = AV_CODEC_ID_MPEG1VIDEO;
            break;
        case BLURAY_STREAM_TYPE_VIDEO_MPEG2:
            codec_id = AV_CODEC_ID_MPEG2VIDEO;
            break;
        case BLURAY_STREAM_TYPE_VIDEO_VC1:
            codec_id = AV_CODEC_ID_VC1;
            break;
        case BLURAY_STREAM_TYPE_VIDEO_H264:
            codec_id = AV_CODEC_ID_H264;
            break;
        case BLURAY_STREAM_TYPE_VIDEO_HEVC:
            codec_id = AV_CODEC_ID_HEVC;
            break;
    }

    switch (bd_st_video.format) {
        case BLURAY_VIDEO_FORMAT_480I:
        case BLURAY_VIDEO_FORMAT_480P:
            height = 720;
            width  = 480;
            break;
        case BLURAY_VIDEO_FORMAT_576I:
        case BLURAY_VIDEO_FORMAT_576P:
            height = 720;
            width  = 576;
            break;
        case BLURAY_VIDEO_FORMAT_720P:
            height = 1280;
            width  = 720;
            break;
        case BLURAY_VIDEO_FORMAT_1080I:
        case BLURAY_VIDEO_FORMAT_1080P:
            height = 1920;
            width  = 1080;
            break;
        case BLURAY_VIDEO_FORMAT_2160P:
            height = 3840;
            width  = 2160;
            break;
    }

    switch (bd_st_video.rate) {
        case BLURAY_VIDEO_RATE_24000_1001:
            framerate = (AVRational) { 24000, 1001 };
            break;
        case BLURAY_VIDEO_RATE_24:
            framerate = (AVRational) { 24, 1 };
            break;
        case BLURAY_VIDEO_RATE_25:
            framerate = (AVRational) { 25, 1 };
            break;
        case BLURAY_VIDEO_RATE_30000_1001:
            framerate = (AVRational) { 30000, 1001 };
            break;
        case BLURAY_VIDEO_RATE_50:
            framerate = (AVRational) { 50, 1 };
            break;
        case BLURAY_VIDEO_RATE_60000_1001:
            framerate = (AVRational) { 60000, 1001 };
            break;
    }

    switch (bd_st_video.aspect) {
        case BLURAY_ASPECT_RATIO_4_3:
            dar = (AVRational) { 4, 3 };
            break;
        case BLURAY_ASPECT_RATIO_16_9:
            dar = (AVRational) { 16, 9 };
            break;
    }

    if (codec_id == AV_CODEC_ID_NONE || !width || !height || framerate.num == 0 || dar.num == 0) {
        av_log(s, AV_LOG_ERROR, "Invalid video stream parameters for PID %02x\n", bd_st_video.pid);

        return AVERROR_INVALIDDATA;
    }

    entry->pid       = bd_st_video.pid;
    entry->codec_id  = codec_id;
    entry->width     = width;
    entry->height    = height;
    entry->dar       = dar;
    entry->framerate = framerate;

    return 0;
}

static int bdmv_clip_video_stream_add(AVFormatContext *s, const BDMVVideoStreamEntry *entry)
{
    AVStream *st;
    FFStream *sti;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->id                    = entry->pid;
    st->codecpar->codec_type  = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id    = entry->codec_id;
    st->codecpar->width       = entry->width;
    st->codecpar->height      = entry->height;
    st->codecpar->format      = AV_PIX_FMT_YUV420P;
    st->codecpar->color_range = AVCOL_RANGE_MPEG;

#if FF_API_R_FRAME_RATE
    st->r_frame_rate          = entry->framerate;
#endif
    st->avg_frame_rate        = entry->framerate;

    sti = ffstream(st);
    sti->request_probe        = 0;
    sti->need_parsing         = AVSTREAM_PARSE_FULL;
    sti->display_aspect_ratio = entry->dar;

    avpriv_set_pts_info(st, BDMV_PTS_WRAP_BITS,
                        BDMV_TIME_BASE_Q.num, BDMV_TIME_BASE_Q.den);

    return 0;
}

static int bdmv_clip_video_stream_add_group(AVFormatContext *s, const int nb_bd_streams,
                                            const BLURAY_STREAM_INFO *bd_streams)
{
    for (int i = 0; i < nb_bd_streams; i++) {
        BDMVVideoStreamEntry entry           = {0};
        const BLURAY_STREAM_INFO bd_st_video = bd_streams[i];
        int ret;

        ret = bdmv_clip_video_stream_analyze(s, bd_st_video, &entry);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to analyze video stream: invalid parameters\n");
            return ret;
        }

        ret = bdmv_clip_video_stream_add(s, &entry);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to add video stream\n");
            return ret;
        }
    }

    return 0;
}

static int bdmv_clip_video_stream_add_all(AVFormatContext *s, const BLURAY_CLIP_INFO bd_clip)
{
    int ret;

    /* add the primary streams */
    ret = bdmv_clip_video_stream_add_group(s, bd_clip.video_stream_count,
                                           bd_clip.video_streams);
    if (ret < 0)
        return ret;

    /* add the secondary streams */
    ret = bdmv_clip_video_stream_add_group(s, bd_clip.sec_video_stream_count,
                                           bd_clip.sec_video_streams);
    if (ret < 0)
        return ret;

    return 0;
}

static int bdmv_clip_audio_stream_analyze(AVFormatContext *s, const BLURAY_STREAM_INFO bd_st_audio,
                                          BDMVAudioStreamEntry *entry)
{
    enum AVCodecID codec_id   = AV_CODEC_ID_NONE;
    int sample_rate           = 0;

    switch (bd_st_audio.coding_type) {
        case BLURAY_STREAM_TYPE_AUDIO_MPEG1:
            codec_id = AV_CODEC_ID_MP1;
            break;
        case BLURAY_STREAM_TYPE_AUDIO_MPEG2:
            codec_id = AV_CODEC_ID_MP2;
            break;
        case BLURAY_STREAM_TYPE_AUDIO_AC3:
            codec_id = AV_CODEC_ID_AC3;
            break;
        case BLURAY_STREAM_TYPE_AUDIO_AC3PLUS:
        case BLURAY_STREAM_TYPE_AUDIO_AC3PLUS_SECONDARY:
            codec_id = AV_CODEC_ID_EAC3;
            break;
        case BLURAY_STREAM_TYPE_AUDIO_TRUHD:
            codec_id = AV_CODEC_ID_TRUEHD;
            break;
        case BLURAY_STREAM_TYPE_AUDIO_DTS:
        case BLURAY_STREAM_TYPE_AUDIO_DTSHD:
        case BLURAY_STREAM_TYPE_AUDIO_DTSHD_MASTER:
        case BLURAY_STREAM_TYPE_AUDIO_DTSHD_SECONDARY:
            codec_id = AV_CODEC_ID_DTS;
            break;
        case BLURAY_STREAM_TYPE_AUDIO_LPCM:
            codec_id = AV_CODEC_ID_PCM_BLURAY;
            break;
    }

    switch (bd_st_audio.rate) {
        case BLURAY_AUDIO_RATE_48:
            sample_rate = 48000;
            break;
        case BLURAY_AUDIO_RATE_96:
        case BLURAY_AUDIO_RATE_96_COMBO:
            sample_rate = 96000;
            break;
        case BLURAY_AUDIO_RATE_192:
        case BLURAY_AUDIO_RATE_192_COMBO:
            sample_rate = 192000;
            break;
    }

    if (codec_id == AV_CODEC_ID_NONE || !sample_rate) {
        av_log(s, AV_LOG_ERROR, "Invalid audio stream parameters for PID %02x\n", bd_st_audio.pid);

        return AVERROR_INVALIDDATA;
    }

    entry->pid         = bd_st_audio.pid;
    entry->codec_id    = codec_id;
    entry->sample_rate = sample_rate;
    entry->lang_iso    = ff_convert_lang_to(bd_st_audio.lang, AV_LANG_ISO639_2_BIBL);

    return 0;
}

static int bdmv_clip_audio_stream_add(AVFormatContext *s, const BDMVAudioStreamEntry *entry)
{
    AVStream *st;
    FFStream *sti;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->id                    = entry->pid;
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = entry->codec_id;
    st->codecpar->sample_rate = entry->sample_rate;

    if (entry->lang_iso)
        av_dict_set(&st->metadata, "language", entry->lang_iso, 0);

    sti = ffstream(st);
    sti->request_probe = 0;
    sti->need_parsing  = AVSTREAM_PARSE_FULL;

    avpriv_set_pts_info(st, BDMV_PTS_WRAP_BITS,
                        BDMV_TIME_BASE_Q.num, BDMV_TIME_BASE_Q.den);

    return 0;
}

static int bdmv_clip_audio_stream_add_group(AVFormatContext *s, const int nb_bd_streams,
                                            const BLURAY_STREAM_INFO *bd_streams)
{
    for (int i = 0; i < nb_bd_streams; i++) {
        BDMVAudioStreamEntry entry             = {0};
        BDMVAudioStreamEntry entry_truehd_core = {0};
        const BLURAY_STREAM_INFO bd_st_audio   = bd_streams[i];
        int ret;

        ret = bdmv_clip_audio_stream_analyze(s, bd_st_audio, &entry);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to analyze audio stream: invalid parameters\n");
            return ret;
        }

        ret = bdmv_clip_audio_stream_add(s, &entry);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to add audio stream\n");
            return ret;
        }

        /* TrueHD will have an AC3 core stream with the same PID */
        if (entry.codec_id == AV_CODEC_ID_TRUEHD) {
            entry_truehd_core.pid      = entry.pid;
            entry_truehd_core.codec_id = AV_CODEC_ID_AC3;
            entry_truehd_core.lang_iso = entry.lang_iso;

            ret = bdmv_clip_audio_stream_add(s, &entry_truehd_core);
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "Unable to add core audio stream\n");
                return ret;
            }
        }
    }

    return 0;
}

static int bdmv_clip_audio_stream_add_all(AVFormatContext *s, const BLURAY_CLIP_INFO bd_clip)
{
    int ret;

    /* add the primary streams */
    ret = bdmv_clip_audio_stream_add_group(s, bd_clip.audio_stream_count,
                                           bd_clip.audio_streams);
    if (ret < 0)
        return ret;

    /* add the secondary streams */
    ret = bdmv_clip_audio_stream_add_group(s, bd_clip.sec_audio_stream_count,
                                           bd_clip.sec_audio_streams);
    if (ret < 0)
        return ret;

    return 0;
}

static int bdmv_clip_subtitle_stream_analyze(AVFormatContext *s, const BLURAY_STREAM_INFO bd_st_sub,
                                             BDMVSubtitleStreamEntry *entry)
{
    enum AVCodecID codec_id = AV_CODEC_ID_NONE;

    switch (bd_st_sub.coding_type) {
        case BLURAY_STREAM_TYPE_SUB_TEXT:
            codec_id = AV_CODEC_ID_HDMV_TEXT_SUBTITLE;
            break;
        case BLURAY_STREAM_TYPE_SUB_PG:
            codec_id = AV_CODEC_ID_HDMV_PGS_SUBTITLE;
            break;
    }

    if (codec_id == AV_CODEC_ID_NONE) {
        av_log(s, AV_LOG_ERROR, "Invalid subtitle stream parameters for PID %02x\n", bd_st_sub.pid);

        return AVERROR_INVALIDDATA;
    }

    entry->pid       = bd_st_sub.pid;
    entry->codec_id  = codec_id;
    entry->lang_iso  = ff_convert_lang_to(bd_st_sub.lang, AV_LANG_ISO639_2_BIBL);

    return 0;
}

static int bdmv_clip_subtitle_stream_add(AVFormatContext *s, const BDMVSubtitleStreamEntry *entry)
{
    AVStream *st;
    FFStream *sti;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->id                   = entry->pid;
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = entry->codec_id;

    if (entry->lang_iso)
        av_dict_set(&st->metadata, "language", entry->lang_iso, 0);

    sti = ffstream(st);
    sti->request_probe = 0;
    sti->need_parsing  = AVSTREAM_PARSE_HEADERS;

    avpriv_set_pts_info(st, BDMV_PTS_WRAP_BITS,
                        BDMV_TIME_BASE_Q.num, BDMV_TIME_BASE_Q.den);

    return 0;
}

static int bdmv_clip_subtitle_stream_add_all(AVFormatContext *s, const BLURAY_CLIP_INFO bd_clip)
{
    for (int i = 0; i < bd_clip.pg_stream_count; i++) {
        BDMVSubtitleStreamEntry entry      = {0};
        const BLURAY_STREAM_INFO bd_st_sub = bd_clip.pg_streams[i];
        int ret;

        ret = bdmv_clip_subtitle_stream_analyze(s, bd_st_sub, &entry);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to analyze subtitle stream: invalid parameters\n");
            return ret;
        }

        ret = bdmv_clip_subtitle_stream_add(s, &entry);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to add subtitle stream\n");
            return ret;
        }
    }

    return 0;
}

static int bdmv_mpls_next_ts_unit(AVFormatContext *s, int *need_reset,
                                            uint8_t *buf, int buf_size)
{
    BDMVDemuxContext *c = s->priv_data;

    uint8_t read_buf[BDMV_UNIT_SIZE] = {0};
    int     read_ret;

    if (buf_size != BDMV_UNIT_SIZE)
        return AVERROR(ENOMEM);

    if (c->play_ended)
        return AVERROR_EOF;

    if (c->pending_unit_state == BDMV_PENDING_UNIT_NONE) {
        read_ret = bd_read(c->bd, read_buf, BDMV_UNIT_SIZE);
        if (read_ret < 0)
            return read_ret;
    } else {
        /*
         * the event queue is not loaded until after the unit is read,
         * but the event may require us to interrupt flow and send the unit later
         */
        read_ret = c->pending_unit_size;
        memcpy(buf, &c->pending_unit_data, read_ret);

        memset(c->pending_unit_data, 0, BDMV_UNIT_SIZE);
        c->pending_unit_state = BDMV_PENDING_UNIT_NONE;
        c->pending_unit_size  = 0;

        av_log(s, AV_LOG_DEBUG, "emitting pended unit\n");

        return read_ret;
    }

    /* process the event queue */
    BD_EVENT bd_event;
    while (bd_get_event(c->bd, &bd_event)) {
        switch (bd_event.event) {
            case BD_EVENT_PLAYITEM:
                /* we are shifting clips and need to reset the subdemuxer */
                c->clip_pts_offset = c->bd_mpls->clips[bd_event.param].start_time -
                                     c->bd_mpls->clips[bd_event.param].in_time;

                memcpy(c->pending_unit_data, &read_buf, read_ret);
                c->pending_unit_state = BDMV_PENDING_UNIT_PLAYITEM_TRANSITIONAL;
                c->pending_unit_size  = read_ret;

                av_log(s, AV_LOG_DEBUG, "stored PTS offset and pending unit for clip change\n");

                (*need_reset) = 1;

                return AVERROR_EOF;
            case BD_EVENT_END_OF_TITLE:
                c->play_ended = 1;

                break;
            default:
                av_log(s, AV_LOG_DEBUG, "bd_event emitted: event=%d param=%d\n",
                                        bd_event.event, bd_event.param);
                continue;
        }
    }

    memcpy(buf, &read_buf, BDMV_UNIT_SIZE);

    return read_ret;
}

static int bdmv_mpls_chapters_setup(AVFormatContext *s)
{
    BDMVDemuxContext *c = s->priv_data;

    for (int i = 0; i < c->bd_mpls->chapter_count; i++) {
        const BLURAY_TITLE_CHAPTER bd_chapter = c->bd_mpls->chapters[i];
        uint64_t bd_chapter_end;

        if (!bd_chapter.duration)
            continue;

        bd_chapter_end = bd_chapter.start + bd_chapter.duration;

        if (!avpriv_new_chapter(s, i, BDMV_TIME_BASE_Q, bd_chapter.start, bd_chapter_end, NULL))
            return AVERROR(ENOMEM);
    }

    s->duration = av_rescale_q(c->bd_mpls->duration, BDMV_TIME_BASE_Q, AV_TIME_BASE_Q);

    return 0;
}

static int bdmv_mpls_open(AVFormatContext *s)
{
    BDMVDemuxContext *c = s->priv_data;
    int ret;
    int main_title_id;
    BLURAY_TITLE_INFO *title_info = NULL;

    if (!c->opt_item) {
        main_title_id = bd_get_main_title(c->bd);
        if (main_title_id < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to detect main playlist, please set it manually\n");

            return AVERROR_STREAM_NOT_FOUND;
        }

        title_info = bd_get_title_info(c->bd, main_title_id, c->opt_angle);
        if (title_info)
            c->opt_item = title_info->playlist;
    } else {
        /* find our MPLS */
        for (int i = 0; i < c->bd_nb_titles; i++) {
            BLURAY_TITLE_INFO *cur_title_info = bd_get_title_info(c->bd, i, c->opt_angle);
            if (!cur_title_info)
                continue;

            if (cur_title_info->playlist == c->opt_item) {
                title_info = cur_title_info;
                break;
            } else {
                bd_free_title_info(cur_title_info);
            }
        }
    }

    if (!title_info || title_info->clip_count < 1) {
        av_log(s, AV_LOG_ERROR, "Unable to load the selected MPLS, it is invalid or not found\n");

        return AVERROR_INVALIDDATA;
    }

    c->bd_mpls = title_info;

    ret = bdmv_mpls_chapters_setup(s);
    if (ret < 0)
        return ret;

    ret = bdmv_clip_video_stream_add_all(s, title_info->clips[0]);
    if (ret < 0)
        return ret;

    ret = bdmv_clip_audio_stream_add_all(s, title_info->clips[0]);
    if (ret < 0)
        return ret;

    ret = bdmv_clip_subtitle_stream_add_all(s, title_info->clips[0]);
    if (ret < 0)
        return ret;

    bd_select_playlist(c->bd, c->opt_item);
    bd_select_angle(c->bd, c->opt_angle);
    bd_get_event(c->bd, NULL);

    /*
     * first clip is always at index 0, this is hardcoded in libbluray's nav_next_clip();
     * we need to set this offset now, because a PLAYITEM event will not be triggered
     * for the first clip and timestamps will be off when the first discontinuity is handled
     */
    c->pts_offset = c->bd_mpls->clips[0].start_time - c->bd_mpls->clips[0].in_time;

    return 0;
}

static int bdmv_m2ts_next_ts_unit(AVFormatContext *s, int *need_reset,
                                   uint8_t *buf, int buf_size)
{
    BDMVDemuxContext *c = s->priv_data;

    char cur_clip_path[BDMV_CLIP_PATH_LEN];
    BD_FILE_H *cur_clip_file;
    int ret;

    if (buf_size != BDMV_UNIT_SIZE)
        return AVERROR(ENOMEM);

    /* open the segment */
    if (!c->cur_clip_file) {
        bdmv_clip_format_m2ts_path(cur_clip_path, c->opt_item);

        cur_clip_file = bd_open_file_dec(c->bd, cur_clip_path);
        if (!cur_clip_file) {
            av_log(s, AV_LOG_ERROR, "Unable to open the specified M2TS clip\n");

            return AVERROR_EXTERNAL;
        }

        c->cur_clip_file = cur_clip_file;
    }

    /* read the next unit */
    ret = c->cur_clip_file->read(c->cur_clip_file, buf, BDMV_UNIT_SIZE);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Unable to read next unit\n");

        return AVERROR_EXTERNAL;
    }

    /* we have a unit of the transport stream, pass it along */
    if (ret > 0)
        return ret;

    /* we are at EOF */
    c->cur_clip_file->close(c->cur_clip_file);
    c->cur_clip_file = NULL;

    return AVERROR_EOF;
}

static int bdmv_m2ts_open(AVFormatContext *s)
{
    BDMVDemuxContext *c = s->priv_data;

    /*
     * TODO(PATCHWELCOME):
     * when the appropriate functions in libbluray are available,
     * we can read the stream table in the CLPI file and set them up accurately;
     * currently, bd_get_clpi() does not work with ISOs and furthermore
     * returns raw data structures that would need duplicated parsing code
     */
    for (int i = 0; i < c->mpegts_ctx->nb_streams; i++) {
        int ret;

        AVStream *st  = avformat_new_stream(s, NULL);
        AVStream *ist = c->mpegts_ctx->streams[i];
        if (!st)
            return AVERROR(ENOMEM);

        st->id = i;
        ret = avcodec_parameters_copy(st->codecpar, ist->codecpar);
        if (ret < 0)
            return ret;

        avpriv_set_pts_info(st, ist->pts_wrap_bits, ist->time_base.num, ist->time_base.den);
    }

    return 0;
}

static int bdmv_subdemux_read_data(void *opaque, uint8_t *buf, int buf_size)
{
    AVFormatContext *s  = opaque;
    BDMVDemuxContext *c = s->priv_data;
    int need_reset = 0;
    int ret;

    if (c->opt_domain == BDMV_DOMAIN_M2TS)
        ret = bdmv_m2ts_next_ts_unit(s, &need_reset, buf, buf_size);
    else
        ret = bdmv_mpls_next_ts_unit(s, &need_reset, buf, buf_size);

    if (ret < 0) {
        c->subdemux_reset = ret == AVERROR_EOF && need_reset;

        goto subdemux_eof;
    }

    return ret;

subdemux_eof:
    c->mpegts_pb.pub.eof_reached = 1;
    c->mpegts_pb.pub.error       = ret;
    c->mpegts_pb.pub.read_packet = NULL;
    c->mpegts_pb.pub.buf_end     = c->mpegts_pb.pub.buf_ptr = c->mpegts_pb.pub.buffer;

    return ret;
}

static void bdmv_subdemux_close(AVFormatContext *s)
{
    BDMVDemuxContext *c = s->priv_data;

    av_freep(&c->mpegts_pb.pub.buffer);
    avformat_close_input(&c->mpegts_ctx);
}

static int bdmv_subdemux_open(AVFormatContext *s)
{
    BDMVDemuxContext *c = s->priv_data;
    extern const FFInputFormat ff_mpegts_demuxer;
    int ret;

    if (!(c->mpegts_buf = av_mallocz(BDMV_UNIT_SIZE)))
        return AVERROR(ENOMEM);

    ffio_init_context(&c->mpegts_pb, c->mpegts_buf, BDMV_UNIT_SIZE, 0, s,
                      bdmv_subdemux_read_data, NULL, NULL);
    c->mpegts_pb.pub.seekable = 0;

    if (!(c->mpegts_ctx = avformat_alloc_context()))
        return AVERROR(ENOMEM);

    ret = ff_copy_whiteblacklists(c->mpegts_ctx, s);
    if (ret < 0) {
        avformat_free_context(c->mpegts_ctx);
        c->mpegts_ctx = NULL;

        return ret;
    }

    c->mpegts_ctx->flags                = AVFMT_FLAG_CUSTOM_IO;
    c->mpegts_ctx->ctx_flags           |= AVFMTCTX_UNSEEKABLE;
    c->mpegts_ctx->probesize            = c->opt_domain == BDMV_DOMAIN_M2TS ? s->probesize : 0;
    c->mpegts_ctx->max_analyze_duration = c->opt_domain == BDMV_DOMAIN_M2TS ? s->max_analyze_duration : 0;
    c->mpegts_ctx->interrupt_callback   = s->interrupt_callback;
    c->mpegts_ctx->pb                   = &c->mpegts_pb.pub;
    c->mpegts_ctx->io_open              = NULL;

    return avformat_open_input(&c->mpegts_ctx, "", &ff_mpegts_demuxer.p, NULL);
}

static int bdmv_subdemux_reset(AVFormatContext *s)
{
    int ret;

    av_log(s, AV_LOG_VERBOSE, "Resetting sub-demuxer\n");

    bdmv_subdemux_close(s);

    ret = bdmv_subdemux_open(s);
    if (ret < 0)
        return ret;

    return 0;
}

static int bdmv_structure_open(AVFormatContext *s)
{
    BDMVDemuxContext        *c = s->priv_data;
    const BLURAY_DISC_INFO  *disc_info;

    c->bd = bd_open(s->url, NULL);
    if (!c->bd) {
        av_log(s, AV_LOG_ERROR, "Unable to open BDMV structure\n");

        return AVERROR_EXTERNAL;
    }

    disc_info = bd_get_disc_info(c->bd);
    if (!disc_info || !disc_info->bluray_detected) {
        av_log(s, AV_LOG_ERROR, "Invalid BDMV structure\n");

        return AVERROR_EXTERNAL;
    }

    if ((disc_info->aacs_detected   && !disc_info->aacs_handled) ||
        (disc_info->bdplus_detected && !disc_info->bdplus_handled)) {
        av_log(s, AV_LOG_ERROR, "Protected BDMV structures are not supported\n");

        return AVERROR_EXTERNAL;
    }

    if (c->opt_domain == BDMV_DOMAIN_MPLS) {
        /* needed before bd_get_main_title() and bd_get_title_info() */
        c->bd_nb_titles = bd_get_titles(c->bd, TITLES_ALL, 0);
        if (c->bd_nb_titles < 1) {
            av_log(s, AV_LOG_ERROR, "Disc structure has no usable MPLS playlists\n");

            return AVERROR_EXTERNAL;
        }
    }

    return 0;
}

static int bdmv_read_header(AVFormatContext *s)
{
    const BDMVDemuxContext *c = s->priv_data;
    int ret;

    /* feed an M2TS file to the subdemuxer */
    if (c->opt_domain == BDMV_DOMAIN_M2TS) {
        ret = bdmv_structure_open(s);
        if (ret < 0)
            return ret;

        ret = bdmv_subdemux_open(s);
        if (ret < 0)
            return ret;

        ret = bdmv_m2ts_open(s);
        if (ret < 0)
            return ret;

        return 0;
    }

    /* feed an MPLS playlist to the subdemuxer */
    ret = bdmv_structure_open(s);
    if (ret < 0)
        return ret;

    ret = bdmv_mpls_open(s);
    if (ret < 0)
        return ret;

    ret = bdmv_subdemux_open(s);
    if (ret < 0)
        return ret;

    return 0;
}

static int bdmv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    BDMVDemuxContext *c = s->priv_data;

    int is_key     = 0;
    int st_mapped = 0;
    AVStream *st_subdemux;
    int ret;

    if (c->subdemux_end)
        return AVERROR_EOF;

    ret = av_read_frame(c->mpegts_ctx, pkt);
    if (c->opt_domain == BDMV_DOMAIN_M2TS)
        return ret < 0 ? ret : 0;

    if (ret < 0) {
        if (c->subdemux_reset && ret == AVERROR_EOF) {
            c->subdemux_reset = 0;
            c->pts_offset     = c->clip_pts_offset;

            ret = bdmv_subdemux_reset(s);
            if (ret < 0)
                return ret;

            return FFERROR_REDO;
        }

        return ret;
    }

    st_subdemux = c->mpegts_ctx->streams[pkt->stream_index];
    is_key      = pkt->flags & AV_PKT_FLAG_KEY;

    /* map the subdemuxer stream to the parent demuxer's stream (by PID and codec) */
    for (int i = 0; i < s->nb_streams; i++) {
        if (s->streams[i]->id == st_subdemux->id &&
            s->streams[i]->codecpar->codec_id == st_subdemux->codecpar->codec_id) {

            pkt->stream_index = s->streams[i]->index;
            st_mapped         = 1;
            break;
        }
    }

    if (!st_mapped || pkt->pts == AV_NOPTS_VALUE || pkt->dts == AV_NOPTS_VALUE)
        goto discard;

    if (c->seek_offset != 0) {
        if (st_subdemux->codecpar->codec_type != AVMEDIA_TYPE_VIDEO || !is_key)
            goto discard;

        c->pts_offset = c->seek_offset - pkt->pts;
    }
    c->seek_offset = 0;

    pkt->pts += c->pts_offset;
    pkt->dts += c->pts_offset;

    if (pkt->pts < 0)
        goto discard;

    av_log(s, AV_LOG_TRACE, "st=%d pts=%" PRId64 " dts=%" PRId64 " "
                            "pts_offset=%" PRId64 "\n",
                            pkt->stream_index, pkt->pts, pkt->dts,
                            c->pts_offset);

    return 0;

discard:
    av_log(s, st_mapped ? AV_LOG_VERBOSE : AV_LOG_DEBUG,
           "Discarding frame @ st=%d pts=%" PRId64 " dts=%" PRId64 " st_mapped=%d\n",
           st_mapped ? pkt->stream_index : -1, pkt->pts, pkt->dts, st_mapped);

    return FFERROR_REDO;
}

static int bdmv_read_close(AVFormatContext *s)
{
    BDMVDemuxContext *c = s->priv_data;

    if (c->bd_mpls)
        bd_free_title_info(c->bd_mpls);

    if (c->bd)
        bd_close(c->bd);

    return 0;
}

static int bdmv_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    BDMVDemuxContext *c = s->priv_data;
    int64_t result_seek;
    uint64_t result_tell;

    if (c->opt_domain != BDMV_DOMAIN_MPLS) {
        av_log(s, AV_LOG_ERROR, "Seeking is currently only supported with MPLS demuxing\n");

        return AVERROR_PATCHWELCOME;
    }

    if ((flags & AVSEEK_FLAG_BYTE))
        return AVERROR(ENOSYS);

    if (timestamp < 0 || timestamp > s->duration)
        return AVERROR(EINVAL);

    if (!c->seek_warned) {
        av_log(s, AV_LOG_WARNING, "Seeking is experimental and will result "
                                  "in imprecise timecodes from this point\n");

        c->seek_warned = 1;
    }

    result_seek = bd_seek_time(c->bd, timestamp);
    if (result_seek < 0) {
        av_log(s, AV_LOG_ERROR, "libbluray: seeking to %" PRId64 " failed\n", timestamp);

        return AVERROR_EXTERNAL;
    }

    result_tell = bd_tell_time(c->bd);

    c->pts_offset      = 0;
    c->clip_pts_offset = 0;
    c->seek_offset     = timestamp;
    c->subdemux_reset  = 0;

    memset(c->pending_unit_data, 0, BDMV_UNIT_SIZE);
    c->pending_unit_state = BDMV_PENDING_UNIT_NONE;
    c->pending_unit_size  = 0;

    avio_flush(&c->mpegts_pb.pub);
    ff_read_frame_flush(c->mpegts_ctx);

    av_log(s, AV_LOG_DEBUG, "seeking: requested=%" PRId64 " "
                            "result_seek=%" PRId64 " result_tell=%" PRId64 "\n",
                            timestamp, result_seek, result_tell);

    return 0;
}

#define OFFSET(x) offsetof(BDMVDemuxContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption bdmv_options[] = {
    { "domain",             "domain within the BDMV structure",   OFFSET(opt_domain),         AV_OPT_TYPE_INT,   { .i64=BDMV_DOMAIN_MPLS }, BDMV_DOMAIN_MPLS, BDMV_DOMAIN_M2TS, AV_OPT_FLAG_DECODING_PARAM, .unit = "domain" },
       { "mpls",            "open a MPLS",                        0,                          AV_OPT_TYPE_CONST, { .i64=BDMV_DOMAIN_MPLS }, .flags = AV_OPT_FLAG_DECODING_PARAM, .unit = "domain" },
       { "m2ts",            "open an M2TS segment by ID",         1,                          AV_OPT_TYPE_CONST, { .i64=BDMV_DOMAIN_M2TS }, .flags = AV_OPT_FLAG_DECODING_PARAM, .unit = "domain" },
    {"angle",              "angle number for MPLS",               OFFSET(opt_angle),          AV_OPT_TYPE_INT,   { .i64=1 },                  1,                99,        AV_OPT_FLAG_DECODING_PARAM },
    {"item",               "item number for domain (0=auto)",     OFFSET(opt_item),           AV_OPT_TYPE_INT,   { .i64=0 },                  0,                9999,      AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass bdmv_demuxer_class = {
    .class_name = "BDMV demuxer",
    .item_name  = av_default_item_name,
    .option     = bdmv_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFInputFormat ff_bdmv_demuxer = {
    .p.name         = "bdmv",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Blu-ray Disc Movie (BDMV)"),
    .p.flags        = AVFMT_SHOW_IDS | AVFMT_TS_DISCONT   | AVFMT_SEEK_TO_PTS |
                      AVFMT_NOFILE   | AVFMT_NO_BYTE_SEEK | AVFMT_NOGENSEARCH | AVFMT_NOBINSEARCH,
    .p.priv_class   = &bdmv_demuxer_class,
    .priv_data_size = sizeof(BDMVDemuxContext),
    .read_header    = bdmv_read_header,
    .read_packet    = bdmv_read_packet,
    .read_close     = bdmv_read_close,
    .read_seek      = bdmv_read_seek
};
