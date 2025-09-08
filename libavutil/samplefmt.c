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

#include "error.h"
#include "macros.h"
#include "mem.h"
#include "samplefmt.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/** this table gives more information about formats */
static const AVSampleFmtDescriptor sample_fmt_info[AV_SAMPLE_FMT_NB] = {
    [AV_SAMPLE_FMT_U8]   = {
        .name    = "u8",
        .bits    = 8,
        .flags   = AV_SAMPLE_FMT_FLAG_UNSIGNED,
    },
    [AV_SAMPLE_FMT_S16]  = {
        .name    = "s16",
        .bits    = 16,
        .flags   = 0,
    },
    [AV_SAMPLE_FMT_S32]  = {
        .name    = "s32",
        .bits    = 32,
        .flags   = 0,
    },
    [AV_SAMPLE_FMT_S64]  = {
        .name    = "s64",
        .bits    = 64,
        .flags   = 0,
    },
    [AV_SAMPLE_FMT_FLT]  = {
        .name    = "flt",
        .bits    = 32,
        .flags   = AV_SAMPLE_FMT_FLAG_FLOAT,
    },
    [AV_SAMPLE_FMT_DBL]  = {
        .name    = "dbl",
        .bits    = 64,
        .flags   = AV_SAMPLE_FMT_FLAG_FLOAT,
    },
    [AV_SAMPLE_FMT_U8P]  = {
        .name    = "u8p",
        .bits    = 8,
        .flags   = AV_SAMPLE_FMT_FLAG_PLANAR | AV_SAMPLE_FMT_FLAG_UNSIGNED,
    },
    [AV_SAMPLE_FMT_S16P] = {
        .name    = "s16p",
        .bits    = 16,
        .flags   = AV_SAMPLE_FMT_FLAG_PLANAR,
    },
    [AV_SAMPLE_FMT_S32P] = {
        .name    = "s32p",
        .bits    = 32,
        .flags   = AV_SAMPLE_FMT_FLAG_PLANAR,
    },
    [AV_SAMPLE_FMT_S64P] = {
        .name    = "s64p",
        .bits    = 64,
        .flags   = AV_SAMPLE_FMT_FLAG_PLANAR,
    },
    [AV_SAMPLE_FMT_FLTP] = {
        .name    = "fltp",
        .bits    = 32,
        .flags   = AV_SAMPLE_FMT_FLAG_PLANAR | AV_SAMPLE_FMT_FLAG_FLOAT,
    },
    [AV_SAMPLE_FMT_DBLP] = {
        .name    = "dblp",
        .bits    = 64,
        .flags   = AV_SAMPLE_FMT_FLAG_PLANAR | AV_SAMPLE_FMT_FLAG_FLOAT,
    },
};

const AVSampleFmtDescriptor *av_sample_fmt_desc_get(enum AVSampleFormat sample_fmt)
{
    if (sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB)
        return NULL;
    return &sample_fmt_info[sample_fmt];
}

const AVSampleFmtDescriptor *av_sample_fmt_desc_next(const AVSampleFmtDescriptor *prev)
{
    if (!prev)
        return &sample_fmt_info[0];
    while (prev - sample_fmt_info < FF_ARRAY_ELEMS(sample_fmt_info) - 1) {
        prev++;
        if (prev->name)
            return prev;
    }
    return NULL;
}

enum AVSampleFormat av_sample_fmt_desc_get_id(const AVSampleFmtDescriptor *desc)
{
    if (desc < sample_fmt_info ||
        desc >= sample_fmt_info + FF_ARRAY_ELEMS(sample_fmt_info))
        return AV_SAMPLE_FMT_NONE;

    return desc - sample_fmt_info;
}

const char *av_get_sample_fmt_name(enum AVSampleFormat sample_fmt)
{
    if (sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB)
        return NULL;
    return sample_fmt_info[sample_fmt].name;
}

enum AVSampleFormat av_get_sample_fmt(const char *name)
{
    int i;

    for (i = 0; i < AV_SAMPLE_FMT_NB; i++)
        if (!strcmp(sample_fmt_info[i].name, name))
            return i;
    return AV_SAMPLE_FMT_NONE;
}

static enum AVSampleFormat get_alt_sample_fmt(const AVSampleFmtDescriptor *desc, uint64_t flags)
{
    const AVSampleFmtDescriptor *iter = NULL;

    while ((iter = av_sample_fmt_desc_next(iter))) {
        if (iter->bits == desc->bits && iter->flags == flags)
            return av_sample_fmt_desc_get_id(iter);
    }

    return AV_SAMPLE_FMT_NONE;
}

enum AVSampleFormat av_get_alt_sample_fmt(enum AVSampleFormat sample_fmt, int planar)
{
    const AVSampleFmtDescriptor *desc = av_sample_fmt_desc_get(sample_fmt);
    uint64_t flags;

    if (!desc)
        return AV_SAMPLE_FMT_NONE;
    flags = desc->flags ^ (!!planar * AV_SAMPLE_FMT_FLAG_PLANAR);

    return get_alt_sample_fmt(desc, flags);
}

enum AVSampleFormat av_get_packed_sample_fmt(enum AVSampleFormat sample_fmt)
{
    const AVSampleFmtDescriptor *desc = av_sample_fmt_desc_get(sample_fmt);
    uint64_t flags;

    if (!desc)
        return AV_SAMPLE_FMT_NONE;
    flags = desc->flags & ~AV_SAMPLE_FMT_FLAG_PLANAR;

    return get_alt_sample_fmt(desc, flags);
}

enum AVSampleFormat av_get_planar_sample_fmt(enum AVSampleFormat sample_fmt)
{
    const AVSampleFmtDescriptor *desc = av_sample_fmt_desc_get(sample_fmt);
    uint64_t flags;

    if (!desc)
        return AV_SAMPLE_FMT_NONE;
    flags = desc->flags | AV_SAMPLE_FMT_FLAG_PLANAR;

    return get_alt_sample_fmt(desc, flags);
}

char *av_get_sample_fmt_string (char *buf, int buf_size, enum AVSampleFormat sample_fmt)
{
    /* print header */
    if (sample_fmt < 0)
        snprintf(buf, buf_size, "name  " " depth");
    else if (sample_fmt < AV_SAMPLE_FMT_NB) {
        const AVSampleFmtDescriptor *info = &sample_fmt_info[sample_fmt];
        snprintf (buf, buf_size, "%-6s" "   %2d ", info->name, info->bits);
    }

    return buf;
}

int av_get_bytes_per_sample(enum AVSampleFormat sample_fmt)
{
     return sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB ?
        0 : sample_fmt_info[sample_fmt].bits >> 3;
}

int av_sample_fmt_is_planar(enum AVSampleFormat sample_fmt)
{
     if (sample_fmt < 0 || sample_fmt >= AV_SAMPLE_FMT_NB)
         return 0;
     return !!(sample_fmt_info[sample_fmt].flags & AV_SAMPLE_FMT_FLAG_PLANAR);
}

int av_samples_get_buffer_size(int *linesize, int nb_channels, int nb_samples,
                               enum AVSampleFormat sample_fmt, int align)
{
    const AVSampleFmtDescriptor *desc = av_sample_fmt_desc_get(sample_fmt);
    int line_size;

    /* validate parameter ranges */
    if (!desc || nb_samples <= 0 || nb_channels <= 0)
        return AVERROR(EINVAL);

    int sample_size = desc->bits >> 3;
    int planar = desc->flags & AV_SAMPLE_FMT_FLAG_PLANAR;

    /* auto-select alignment if not specified */
    if (!align) {
        if (nb_samples > INT_MAX - 31)
            return AVERROR(EINVAL);
        align = 1;
        nb_samples = FFALIGN(nb_samples, 32);
    }

    /* check for integer overflow */
    if (nb_channels > INT_MAX / align ||
        (int64_t)nb_channels * nb_samples > (INT_MAX - (align * nb_channels)) / sample_size)
        return AVERROR(EINVAL);

    line_size = planar ? FFALIGN(nb_samples * sample_size,               align) :
                         FFALIGN(nb_samples * sample_size * nb_channels, align);
    if (linesize)
        *linesize = line_size;

    return planar ? line_size * nb_channels : line_size;
}

int av_samples_fill_arrays(uint8_t **audio_data, int *linesize,
                           const uint8_t *buf, int nb_channels, int nb_samples,
                           enum AVSampleFormat sample_fmt, int align)
{
    const AVSampleFmtDescriptor *desc = av_sample_fmt_desc_get(sample_fmt);
    int ch, planar, buf_size, line_size;

    if (!desc)
        return AVERROR(EINVAL);

    buf_size = av_samples_get_buffer_size(&line_size, nb_channels, nb_samples,
                                          sample_fmt, align);
    if (buf_size < 0)
        return buf_size;

    if (linesize)
        *linesize = line_size;

    planar = desc->flags & AV_SAMPLE_FMT_FLAG_PLANAR;
    memset(audio_data, 0, planar
                          ? sizeof(*audio_data) * nb_channels
                          : sizeof(*audio_data));

    if (!buf)
        return buf_size;

    audio_data[0] = (uint8_t *)buf;
    for (ch = 1; planar && ch < nb_channels; ch++)
        audio_data[ch] = audio_data[ch-1] + line_size;

    return buf_size;
}

int av_samples_alloc(uint8_t **audio_data, int *linesize, int nb_channels,
                     int nb_samples, enum AVSampleFormat sample_fmt, int align)
{
    uint8_t *buf;
    int size = av_samples_get_buffer_size(NULL, nb_channels, nb_samples,
                                          sample_fmt, align);
    if (size < 0)
        return size;

    buf = av_malloc(size);
    if (!buf)
        return AVERROR(ENOMEM);

    size = av_samples_fill_arrays(audio_data, linesize, buf, nb_channels,
                                  nb_samples, sample_fmt, align);
    if (size < 0) {
        av_free(buf);
        return size;
    }

    av_samples_set_silence(audio_data, 0, nb_samples, nb_channels, sample_fmt);

    return size;
}

int av_samples_alloc_array_and_samples(uint8_t ***audio_data, int *linesize, int nb_channels,
                                       int nb_samples, enum AVSampleFormat sample_fmt, int align)
{
    const AVSampleFmtDescriptor *desc = av_sample_fmt_desc_get(sample_fmt);
    int ret;

    if (!desc)
        return AVERROR(EINVAL);

    int nb_planes = (desc->flags & AV_SAMPLE_FMT_FLAG_PLANAR) ? nb_channels : 1;
    *audio_data = av_calloc(nb_planes, sizeof(**audio_data));
    if (!*audio_data)
        return AVERROR(ENOMEM);
    ret = av_samples_alloc(*audio_data, linesize, nb_channels,
                           nb_samples, sample_fmt, align);
    if (ret < 0)
        av_freep(audio_data);
    return ret;
}

int av_samples_copy(uint8_t * const *dst, uint8_t * const *src, int dst_offset,
                    int src_offset, int nb_samples, int nb_channels,
                    enum AVSampleFormat sample_fmt)
{
    const AVSampleFmtDescriptor *desc = av_sample_fmt_desc_get(sample_fmt);

    if (!desc)
        return AVERROR(EINVAL);

    int planar      = desc->flags & AV_SAMPLE_FMT_FLAG_PLANAR;
    int planes      = planar ? nb_channels : 1;
    int block_align = (desc->bits >> 3) * (planar ? 1 : nb_channels);
    int data_size   = nb_samples * block_align;

    dst_offset *= block_align;
    src_offset *= block_align;

    if((dst[0] < src[0] ? src[0] - dst[0] : dst[0] - src[0]) >= data_size) {
        for (int i = 0; i < planes; i++)
            memcpy(dst[i] + dst_offset, src[i] + src_offset, data_size);
    } else {
        for (int i = 0; i < planes; i++)
            memmove(dst[i] + dst_offset, src[i] + src_offset, data_size);
    }

    return 0;
}

int av_samples_set_silence(uint8_t * const *audio_data, int offset, int nb_samples,
                           int nb_channels, enum AVSampleFormat sample_fmt)
{
    const AVSampleFmtDescriptor *desc = av_sample_fmt_desc_get(sample_fmt);
    int fill_char   = (sample_fmt == AV_SAMPLE_FMT_U8 ||
                     sample_fmt == AV_SAMPLE_FMT_U8P) ? 0x80 : 0x00;

    if (!desc)
        return AVERROR(EINVAL);

    int planar      = desc->flags & AV_SAMPLE_FMT_FLAG_PLANAR;
    int planes      = planar ? nb_channels : 1;
    int block_align = (desc->bits >> 3) * (planar ? 1 : nb_channels);
    int data_size   = nb_samples * block_align;

    offset *= block_align;

    for (int i = 0; i < planes; i++)
        memset(audio_data[i] + offset, fill_char, data_size);

    return 0;
}
