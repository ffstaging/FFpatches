/*
 * APV helper functions for muxers
 * Copyright (c) 2025 Dawid Kozinski <d.kozinski@samsung.com>
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

 #include <stdbool.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "libavcodec/golomb.h"
#include "avformat.h"
#include "avio.h"
#include "apv.h"
#include "cbs.h"
#include "libavcodec/cbs_apv.h"
#include "avio_internal.h"

/*****************************************************************************
 * Frame types
 *****************************************************************************/
#define APV_FRAME_TYPE_PRIMARY_FRAME     (0)
#define APV_FRAME_TYPE_NON_PRIMARY_FRAME (1)
#define APV_FRAME_TYPE_PREVIEW_FRAME     (2)
#define APV_FRAME_TYPE_DEPTH_FRAME       (3)
#define APV_FRAME_TYPE_ALPHA_FRAME       (4)
#define APV_FRAME_TYPE_NON_FRAME         (-1)
#define APV_PBU_FRAME_TYPE_NUM           (5)
#define CONFIGURATIONS_MAX               (APV_PBU_FRAME_TYPE_NUM)

typedef struct APVDecoderFrameInfo {
    uint8_t reserved_zero_6bits;                    // 6 bits
    uint8_t color_description_present_flag;         // 1 bit

    // The variable indicates whether the capture_time_distance value in the APV bitstream's frame header should be ignored during playback.
    // If capture_time_distance_ignored is set to true, the capture_time_distance information will not be utilized,
    // and timing information for playback should be calculated using an alternative method.
    // If set to false, the capture_time_distance value will be used as is from the frame header.
    // It is recommended to set this variable to true, allowing the use of MP4 timestamps for playback and recording,
    // which enables the conventional compression and playback methods based on the timestamp table defined by the ISO-based file format.
    uint8_t capture_time_distance_ignored;          // 1-bit

    uint8_t profile_idc;                            // 8 bits 
    uint8_t level_idc;                              // 8 bits
    uint8_t band_idc;                               // 8 bits
    uint32_t frame_width;                           // 32 bits
    uint32_t frame_height;                          // 32 bits
    uint8_t chroma_format_idc;                      // 4 bits
    uint8_t bit_depth_minus8;                       // 4 bits
    uint8_t capture_time_distance;                  // 8 bits

    // if (color_description_present_flag)
    uint8_t color_primaries;                        // 8 bits
    uint8_t transfer_characteristics;               // 8 bits
    uint8_t matrix_coefficients;                    // 8 bits
    uint8_t full_range_flag;                        // 1 bit
    uint8_t reserved_zero_7bits;                    // 7 bits

} APVDecoderFrameInfo;

typedef struct APVDecoderConfigurationEntry {
    uint8_t pbu_type;                   // 8 bits
    uint8_t number_of_frame_info;       // 8 bits

    APVDecoderFrameInfo** frame_info;   // An array of size number_of_frame_info storing elements of type APVDecoderFrameInfo*

} APVDecoderConfigurationEntry;

// ISOBMFF binding for APV
// @see https://github.com/openapv/openapv/blob/main/readme/apv_isobmff.md
typedef struct APVDecoderConfigurationRecord  {
    uint8_t configurationVersion;           // 8 bits
    uint8_t number_of_configuration_entry;  // 8 bits

    APVDecoderConfigurationEntry configuration_entry[CONFIGURATIONS_MAX]; // table of size number_of_configuration_entry

} APVDecoderConfigurationRecord ;

static void apvc_init(APVDecoderConfigurationRecord * apvc)
{
    memset(apvc, 0, sizeof(APVDecoderConfigurationRecord ));
    apvc->configurationVersion = 1;
}

static void apvc_close(APVDecoderConfigurationRecord *apvc)
{
    for(int i=0;i<apvc->number_of_configuration_entry;i++) {
        for(int j=0;j<apvc->configuration_entry[i].number_of_frame_info;j++) {
            free(apvc->configuration_entry[i].frame_info[j]);
        } 
        free(apvc->configuration_entry[i].frame_info);
        apvc->configuration_entry[i].number_of_frame_info = 0;
    }
    apvc->number_of_configuration_entry = 0;
}

static int apvc_write(AVIOContext *pb, APVDecoderConfigurationRecord * apvc)
{
    av_log(NULL, AV_LOG_TRACE, "configurationVersion:                           %"PRIu8"\n", 
    apvc->configurationVersion);
    
    av_log(NULL, AV_LOG_TRACE, "number_of_configuration_entry:                  %"PRIu8"\n", 
    apvc->number_of_configuration_entry);

    for(int i=0; i<apvc->number_of_configuration_entry;i++) {
        av_log(NULL, AV_LOG_TRACE, "pbu_type:                                   %"PRIu8"\n", 
        apvc->configuration_entry[i].pbu_type);

        av_log(NULL, AV_LOG_TRACE, "number_of_frame_info:                       %"PRIu8"\n", 
        apvc->configuration_entry[i].number_of_frame_info);

        for(int j=0; j < apvc->configuration_entry[i].number_of_frame_info; j++) {
            av_log(NULL, AV_LOG_TRACE, "color_description_present_flag:         %"PRIu8"\n", 
            apvc->configuration_entry[i].frame_info[j]->color_description_present_flag);

            av_log(NULL, AV_LOG_TRACE, "capture_time_distance_ignored:          %"PRIu8"\n", 
            apvc->configuration_entry[i].frame_info[j]->capture_time_distance_ignored);

            av_log(NULL, AV_LOG_TRACE, "profile_idc:                            %"PRIu8"\n", 
            apvc->configuration_entry[i].frame_info[j]->profile_idc);

            av_log(NULL, AV_LOG_TRACE, "level_idc:                              %"PRIu8"\n", 
            apvc->configuration_entry[i].frame_info[j]->level_idc);

            av_log(NULL, AV_LOG_TRACE, "band_idc:                               %"PRIu8"\n", 
            apvc->configuration_entry[i].frame_info[j]->band_idc);

            av_log(NULL, AV_LOG_TRACE, "frame_width:                            %"PRIu32"\n", 
            apvc->configuration_entry[i].frame_info[j]->frame_width);

            av_log(NULL, AV_LOG_TRACE, "frame_height:                           %"PRIu32"\n", 
            apvc->configuration_entry[i].frame_info[j]->frame_height);

            av_log(NULL, AV_LOG_TRACE, "chroma_format_idc:                      %"PRIu8"\n", 
            apvc->configuration_entry[i].frame_info[j]->chroma_format_idc);

            av_log(NULL, AV_LOG_TRACE, "bit_depth_minus8:                       %"PRIu8"\n", 
            apvc->configuration_entry[i].frame_info[j]->bit_depth_minus8);

            av_log(NULL, AV_LOG_TRACE, "capture_time_distance:                  %"PRIu8"\n", 
            apvc->configuration_entry[i].frame_info[j]->capture_time_distance);

            if(apvc->configuration_entry[i].frame_info[j]->color_description_present_flag) {
          
                av_log(NULL, AV_LOG_TRACE, "color_primaries:                    %"PRIu8"\n", 
                apvc->configuration_entry[i].frame_info[j]->color_primaries);
                
                av_log(NULL, AV_LOG_TRACE, "transfer_characteristics:           %"PRIu8"\n", 
                apvc->configuration_entry[i].frame_info[j]->transfer_characteristics);
                
                av_log(NULL, AV_LOG_TRACE, "matrix_coefficients:                %"PRIu8"\n", 
                apvc->configuration_entry[i].frame_info[j]->matrix_coefficients);

                av_log(NULL, AV_LOG_TRACE, "full_range_flag:                    %"PRIu8"\n", 
                apvc->configuration_entry[i].frame_info[j]->full_range_flag);
            }

        }
    }
    
    /* unsigned int(8) configurationVersion = 1; */
    avio_w8(pb, apvc->configurationVersion);

    avio_w8(pb, apvc->number_of_configuration_entry);
    
    for(int i=0; i<apvc->number_of_configuration_entry;i++) {
        avio_w8(pb, apvc->configuration_entry[i].pbu_type);
        avio_w8(pb, apvc->configuration_entry[i].number_of_frame_info);

        for(int j=0; j < apvc->configuration_entry[i].number_of_frame_info; j++) {

            /* unsigned int(6) reserved_zero_6bits
            * unsigned int(1) color_description_present_flag
            * unsigned int(1) capture_time_distance_ignored
            */
            avio_w8(pb, apvc->configuration_entry[i].frame_info[j]->reserved_zero_6bits << 2 |
                        apvc->configuration_entry[i].frame_info[j]->color_description_present_flag << 1 | 
                        apvc->configuration_entry[i].frame_info[j]->capture_time_distance_ignored);
            
            /* unsigned int(8) profile_idc */
            avio_w8(pb, apvc->configuration_entry[i].frame_info[j]->profile_idc);

            /* unsigned int(8) level_idc */
            avio_w8(pb, apvc->configuration_entry[i].frame_info[j]->level_idc);

            /* unsigned int(8) band_idc */
            avio_w8(pb, apvc->configuration_entry[i].frame_info[j]->band_idc);
            
            /* unsigned int(32) frame_width_minus1 */
            avio_wb32(pb, apvc->configuration_entry[i].frame_info[j]->frame_width);

            /* unsigned int(32) frame_height_minus1 */
            avio_wb32(pb, apvc->configuration_entry[i].frame_info[j]->frame_height);

            /* unsigned int(4) chroma_format_idc */
            /* unsigned int(4) bit_depth_minus8 */
            avio_w8(pb, apvc->configuration_entry[i].frame_info[j]->chroma_format_idc << 4 | apvc->configuration_entry[i].frame_info[j]->bit_depth_minus8);

            /* unsigned int(8) capture_time_distance */
            avio_w8(pb, apvc->configuration_entry[i].frame_info[j]->capture_time_distance);

            if(apvc->configuration_entry[i].frame_info[j]->color_description_present_flag) {
                /* unsigned int(8) color_primaries */
                avio_w8(pb, apvc->configuration_entry[i].frame_info[j]->color_primaries);
                
                /* unsigned int(8) transfer_characteristics */
                avio_w8(pb, apvc->configuration_entry[i].frame_info[j]->transfer_characteristics);

                /* unsigned int(8) matrix_coefficients */
                avio_w8(pb, apvc->configuration_entry[i].frame_info[j]->matrix_coefficients);

                /* unsigned int(1) full_range_flag */
                avio_w8(pb, apvc->configuration_entry[i].frame_info[j]->full_range_flag << 7 |
                            apvc->configuration_entry[i].frame_info[j]->reserved_zero_7bits);
            }
        }
    }

    return 0;
}

int ff_isom_write_apvc(AVIOContext *pb, const uint8_t *data,
                       int size, int ps_array_completeness)
{
    APVDecoderConfigurationRecord *apvc = (APVDecoderConfigurationRecord *)data;
    int ret = 0;
    
    if (size < 8) {
        /* We can't write a valid apvC from the provided data */
        return AVERROR_INVALIDDATA;
    }

    if(size!=sizeof(APVDecoderConfigurationRecord)) return -1;
    ret = apvc_write(pb, apvc);

    apvc_close(apvc);
    return ret;
}

static int apv_add_frameinfo(APVDecoderConfigurationEntry *configuration_entry, APVDecoderFrameInfo *frame_info) {
    APVDecoderFrameInfo **temp = NULL;
    if(configuration_entry->number_of_frame_info == 0) {
        temp = (APVDecoderFrameInfo **)malloc(sizeof(APVDecoderFrameInfo*));
        if (temp == NULL) {
            return AVERROR_INVALIDDATA;
        }
    } else {
        temp = (APVDecoderFrameInfo **)realloc(configuration_entry->frame_info, (configuration_entry->number_of_frame_info + 1) * sizeof(APVDecoderFrameInfo*));
        if (temp == NULL) {
            return AVERROR_INVALIDDATA;
        }
    }
    
    temp[configuration_entry->number_of_frame_info] = (APVDecoderFrameInfo*)malloc(sizeof(APVDecoderFrameInfo));
    memcpy(temp[configuration_entry->number_of_frame_info], frame_info, sizeof(APVDecoderFrameInfo));

    configuration_entry->frame_info = temp;

    configuration_entry->number_of_frame_info++;

    return 0;
}

static bool apv_cmp_frameinfo(const APVDecoderFrameInfo *a, const APVDecoderFrameInfo *b) {
    if (a->reserved_zero_6bits != b->reserved_zero_6bits) return false;
    if (a->color_description_present_flag != b->color_description_present_flag) return false;
    if (a->capture_time_distance_ignored != b->capture_time_distance_ignored) return false;
    if (a->profile_idc != b->profile_idc) return false;
    if (a->level_idc != b->level_idc) return false;
    if (a->band_idc != b->band_idc) return false;
    if (a->frame_width != b->frame_width) return false;
    if (a->frame_height != b->frame_height) return false;
    if (a->chroma_format_idc != b->chroma_format_idc) return false;
    if (a->bit_depth_minus8 != b->bit_depth_minus8) return false;
    if (a->capture_time_distance != b->capture_time_distance) return false;
    if (a->color_primaries != b->color_primaries) return false;
    if (a->transfer_characteristics != b->transfer_characteristics) return false;
    if (a->matrix_coefficients != b->matrix_coefficients) return false;
    if (a->full_range_flag != b->full_range_flag) return false;
    if (a->reserved_zero_7bits != b->reserved_zero_7bits) return false;

    return true;
}

int ff_isom_create_apv_dconf_record(uint8_t **data, int *size) {
    *size = sizeof(APVDecoderConfigurationRecord);
    *data = (uint8_t*)av_malloc(sizeof(APVDecoderConfigurationRecord));
    if(*data==NULL) {
        *size = 0;
         return AVERROR_INVALIDDATA;
    }
    apvc_init((APVDecoderConfigurationRecord*)*data);

    return 0;
}

void ff_isom_free_apv_dconf_record(uint8_t **data) {
    if (data != NULL && *data != NULL) {
        APVDecoderConfigurationRecord* apvc = (APVDecoderConfigurationRecord*)*data;
        apvc_close(apvc);

        free(*data);
        *data = NULL;
    }
}

static void dummy_free(void *opaque, uint8_t *data)
{
    av_assert0(opaque == data);
}

static const CodedBitstreamUnitType decompose_unit_types[] = {
    APV_PBU_PRIMARY_FRAME,
};

int ff_isom_fill_apv_dconf_record(const uint8_t *apvdcr, const uint8_t *data, int size, AVFormatContext *s) {

    uint32_t number_of_pbu_entry = 0;

    uint32_t frame_type = -1;
    APVDecoderFrameInfo frame_info;

    int bytes_to_read = size; // au size
    int ret = 0;
    CodedBitstreamContext *cbc = NULL;
    CodedBitstreamFragment au = {0};
    AVBufferRef *ref = NULL;

    APVDecoderConfigurationRecord* apvc = (APVDecoderConfigurationRecord*)apvdcr;
    if (size < 8) {
        /* We can't write a valid apvC from the provided data */
        return AVERROR_INVALIDDATA;
    }

    ref = av_buffer_create((uint8_t *)data, size, dummy_free,
                           (void *)data, AV_BUFFER_FLAG_READONLY);
    if (!ref)
        return AVERROR_INVALIDDATA;

    ret = ff_lavf_cbs_init(&cbc, AV_CODEC_ID_APV, NULL);
    if (ret < 0)
        return AVERROR_INVALIDDATA;

    cbc->decompose_unit_types    = decompose_unit_types;
    cbc->nb_decompose_unit_types = FF_ARRAY_ELEMS(decompose_unit_types);

    ret = ff_lavf_cbs_read(cbc, &au, ref, data, size);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to parse access unit.\n");
        goto end;
    }

    for (int i = 0; i < au.nb_units; i++) {
        const CodedBitstreamUnit *pbu = &au.units[i];

        switch (pbu->type)
        {
        case APV_PBU_PRIMARY_FRAME:
            frame_type = APV_FRAME_TYPE_PRIMARY_FRAME;
            break;
        case APV_PBU_NON_PRIMARY_FRAME:
            frame_type = APV_FRAME_TYPE_NON_PRIMARY_FRAME;
            break;
        case APV_PBU_PREVIEW_FRAME:
            frame_type = APV_FRAME_TYPE_PREVIEW_FRAME;
            break;
        case APV_PBU_DEPTH_FRAME:
            frame_type = APV_FRAME_TYPE_DEPTH_FRAME;
            break;
        case APV_PBU_ALPHA_FRAME:
            frame_type = APV_FRAME_TYPE_ALPHA_FRAME;
            break;
        default:
            frame_type = APV_FRAME_TYPE_NON_FRAME;
            break;
        };

        if(frame_type == APV_FRAME_TYPE_NON_FRAME) continue;
        
        const APVRawFrame *frame        = pbu->content;
        const APVRawFrameHeader *header = &frame->frame_header;
        const APVRawFrameInfo *info     = &header->frame_info;
        int bit_depth = info->bit_depth_minus8 + 8;

        if (bit_depth < 8 || bit_depth > 16 || bit_depth % 2)
            break;

        frame_info.profile_idc = info->profile_idc;
        frame_info.level_idc = info->level_idc;
        frame_info.band_idc = info->band_idc;
        
        frame_info.frame_width = info->frame_width;
        frame_info.frame_height =info->frame_height;
        frame_info.chroma_format_idc = info->chroma_format_idc;
        frame_info.bit_depth_minus8 = info->bit_depth_minus8;
        frame_info.capture_time_distance = info->capture_time_distance;
        
        frame_info.color_description_present_flag = header->color_description_present_flag;
        if(frame_info.color_description_present_flag) {
            frame_info.color_primaries = header->color_primaries;
            frame_info.transfer_characteristics = header->transfer_characteristics;
            frame_info.matrix_coefficients = header->matrix_coefficients;
            frame_info.full_range_flag = header->full_range_flag;
        }

        if(apvc->configuration_entry[frame_type].number_of_frame_info == 0) {
                apv_add_frameinfo(&apvc->configuration_entry[frame_type], &frame_info);
                apvc->number_of_configuration_entry++;
        } else {
            for(i=0; i<apvc->configuration_entry[frame_type].number_of_frame_info;i++) {
                if(!apv_cmp_frameinfo(apvc->configuration_entry[frame_type].frame_info[i], &frame_info)) {
                    apv_add_frameinfo(&apvc->configuration_entry[i], &frame_info);
                    break;
                }
            }
        } 
    }

end:
    
    ff_lavf_cbs_fragment_reset(&au);
    av_assert1(av_buffer_get_ref_count(ref) == 1);
    av_buffer_unref(&ref);
    cbc->log_ctx = NULL;

    ff_lavf_cbs_fragment_free(&au);
    ff_lavf_cbs_close(&cbc);

    return ret;
}
