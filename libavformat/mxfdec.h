/*
 * C-to-C++ bridge header for MXF demuxer.
 * Copyright (c) 2025 Tomas HÃ¤rdin
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

#ifndef AVFORMAT_MXFDEC_H
#define AVFORMAT_MXFDEC_H

#include "mxf.h"

typedef enum {
    OP1a = 1,
    OP1b,
    OP1c,
    OP2a,
    OP2b,
    OP2c,
    OP3a,
    OP3b,
    OP3c,
    OPAtom,
    OPSONYOpt,  /* FATE sample, violates the spec in places */
} MXFOP;

struct MXFIndexTable;
struct MXFMetadataSet;
struct MXFPartition;

typedef struct MXFMetadataSetGroup {
    struct MXFMetadataSet **metadata_sets;
    int metadata_sets_count;
} MXFMetadataSetGroup;

typedef struct MXFContext {
    const AVClass *_class;     /**< Class for private options. */
    struct MXFPartition *partitions;
    unsigned partitions_count;
    MXFOP op;
    UID *packages_refs;
    int packages_count;
    UID *essence_container_data_refs;
    int essence_container_data_count;
    MXFMetadataSetGroup metadata_set_groups[MetadataSetTypeNB];
    AVFormatContext *fc;
    struct AVAES *aesc;
    uint8_t *local_tags;
    int local_tags_count;
    uint64_t footer_partition;
    KLVPacket current_klv_data;
    int run_in;
    struct MXFPartition *current_partition;
    int parsing_backward;
    int64_t last_forward_tell;
    int last_forward_partition;
    int nb_index_tables;
    struct MXFIndexTable *index_tables;
    int eia608_extract;
} MXFContext;

int mxf_probe(const AVProbeData *p);
int mxf_read_header(AVFormatContext *s);
int mxf_read_packet(AVFormatContext *s, AVPacket *pkt);
int mxf_read_close(AVFormatContext *s);
int mxf_read_seek(AVFormatContext *s, int stream_index, int64_t sample_time, int flags);

#endif /* AVFORMAT_MXFDEC_H */
