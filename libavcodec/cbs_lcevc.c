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

#include "libavutil/mem.h"
#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_lcevc.h"
#include "libavutil/refstruct.h"

static void free_picture_config(AVRefStructOpaque unused, void *obj)
{
    LCEVCRawPictureConfig *picture_config = obj;

    av_refstruct_unref(&picture_config->gc);
}

static void free_encoded_data(AVRefStructOpaque unused, void *obj)
{
    LCEVCRawEncodedData *slice = obj;

    av_buffer_unref(&slice->data_ref);

    av_refstruct_unref(&slice->sc);
    av_refstruct_unref(&slice->gc);
    av_refstruct_unref(&slice->pc);
}

static void free_additional_info(AVRefStructOpaque unused, void *obj)
{
    LCEVCRawAdditionalInfo *additional_info = obj;
    SEIRawMessage *message = &additional_info->sei;

    av_refstruct_unref(&additional_info->payload_ref);
    av_refstruct_unref(&message->payload_ref);
    av_refstruct_unref(&message->extension_data);
}

int ff_cbs_lcevc_alloc_process_block_payload(LCEVCRawProcessBlock *block,
                                             const LCEVCProcessBlockTypeDescriptor *desc)
{
    void (*free_func)(AVRefStructOpaque, void*);

    av_assert0(block->payload     == NULL &&
               block->payload_ref == NULL);
    block->payload_type = desc->payload_type;

    if (desc->payload_type == LCEVC_PAYLOAD_TYPE_PICTURE_CONFIG)
        free_func = &free_picture_config;
    else if (desc->payload_type == LCEVC_PAYLOAD_TYPE_ENCODED_DATA)
        free_func = &free_encoded_data;
    else if (desc->payload_type == LCEVC_PAYLOAD_TYPE_ADDITIONAL_INFO)
        free_func = &free_additional_info;
    else
        free_func = NULL;

    block->payload_ref = av_refstruct_alloc_ext(desc->payload_size, 0,
                                                NULL, free_func);
    if (!block->payload_ref)
        return AVERROR(ENOMEM);
    block->payload = block->payload_ref;

    return 0;
}

int ff_cbs_lcevc_list_add(LCEVCRawProcessBlockList *list)
{
    void *ptr;
    int old_count = list->nb_blocks_allocated;

    av_assert0(list->nb_blocks <= old_count);
    if (list->nb_blocks + 1 > old_count) {
        int new_count = 2 * old_count + 1;

        ptr = av_realloc_array(list->blocks,
                               new_count, sizeof(*list->blocks));
        if (!ptr)
            return AVERROR(ENOMEM);

        list->blocks = ptr;
        list->nb_blocks_allocated = new_count;

        // Zero the newly-added entries.
        memset(list->blocks + old_count, 0,
               (new_count - old_count) * sizeof(*list->blocks));
    }
    ++list->nb_blocks;
    return 0;
}

void ff_cbs_lcevc_free_process_block_list(LCEVCRawProcessBlockList *list)
{
    for (int i = 0; i < list->nb_blocks; i++) {
        LCEVCRawProcessBlock *block = &list->blocks[i];
        av_refstruct_unref(&block->payload_ref);
    }
    av_free(list->blocks);
}
