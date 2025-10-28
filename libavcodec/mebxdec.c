/*
 * Metadata Boxed (mebx) decoder
 * Copyright (c) 2025 Lukas Holliger
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

#include "avcodec.h"
#include "codec_internal.h"
#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/macros.h"

/**
 * Metadata key definition with type information
 */
typedef struct {
    uint32_t key_id;             // 1-based key identifier, zeroes are "dropped"
    char *key_name;              // Full key name (e.g., "mdta:com.apple.quicktime.scene-illuminance")
    uint32_t datatype_namespace; // 0 for well-known types, 1 for custom/reverse-DNS
    uint32_t datatype_value;     // Type code (if namespace==0) or pointer to type string (if namespace==1)
    char *datatype_string;       // Type string for namespace==1
} MebxKeyDef;

typedef struct {
    AVDictionary *metadata;
    MebxKeyDef *keys;          // Array of key definitions
    int nb_keys;               // Number of keys
} MebxContext;

/**
 * Parse a keyd (key definition) box.
 * Returns the key definition and advances the pointer.
 *
 * Format:
 *   [4 bytes] box size
 *   [4 bytes] 'keyd' fourcc
 *   [4 bytes] key namespace (4 ASCII chars)
 *   [variable] key value (null-terminated string)
 */
static int mebx_parse_keyd_box(AVCodecContext *avctx, const uint8_t *ptr, const uint8_t *box_end,
                               uint32_t local_key_id, MebxKeyDef *key_def, const uint8_t **next_ptr)
{
    uint32_t box_size;
    uint32_t box_type;
    char key_namespace[5] = { 0 };
    char key_value[256] = { 0 };
    char key_name[512] = { 0 };

    if (ptr + 8 > box_end)
        return AVERROR_INVALIDDATA;

    box_size = AV_RB32(ptr);
    box_type = AV_RB32(ptr + 4);

    if (box_type != MKBETAG('k', 'e', 'y', 'd')) {
        av_log(avctx, AV_LOG_WARNING, "mebx_parse_keyd_box: Expected 'keyd', got 0x%08x\n", box_type);
        return AVERROR_INVALIDDATA;
    }

    if (box_size < 12 || ptr + box_size > box_end) {
        av_log(avctx, AV_LOG_WARNING, "mebx_parse_keyd_box: Invalid box size %u\n", box_size);
        return AVERROR_INVALIDDATA;
    }

    // Parse keyd content: namespace (4 bytes) + key value (rest)
    memcpy(key_namespace, ptr + 8, 4);
    int keyd_data_size = box_size - 12;
    if (keyd_data_size > 0) {
        memcpy(key_value, ptr + 12, FFMIN(keyd_data_size, (int)sizeof(key_value) - 1));
        key_value[FFMIN(keyd_data_size, (int)sizeof(key_value) - 1)] = '\0';
    }

    // Create full key name
    snprintf(key_name, sizeof(key_name), "%s:%s", key_namespace, key_value);

    // Initialize key definition with local_key_id from the MetadataKeyBox box type
    key_def->key_id = local_key_id;
    key_def->key_name = av_strdup(key_name);
    key_def->datatype_namespace = 0;  // Default to well-known types
    key_def->datatype_value = 0;      // Unknown type
    key_def->datatype_string = NULL;

    *next_ptr = ptr + box_size;
    return 0;
}

/**
 * Parse a dtyp (datatype definition) box.
 * Returns the type information and advances the pointer.
 *
 * Format:
 *   [4 bytes] box size
 *   [4 bytes] 'dtyp' fourcc
 *   [4 bytes] datatype namespace (0 for well-known, 1 for custom)
 *   [variable] datatype value (4-byte uint32 for namespace 0, UTF-8 string for namespace 1)
 */
static int mebx_parse_dtyp_box(AVCodecContext *avctx, const uint8_t *ptr, const uint8_t *box_end,
                               MebxKeyDef *key_def, const uint8_t **next_ptr)
{
    uint32_t box_size;
    uint32_t box_type;

    if (ptr + 8 > box_end)
        return AVERROR_INVALIDDATA;

    box_size = AV_RB32(ptr);
    box_type = AV_RB32(ptr + 4);

    // in 2022 spec it is undocumented so it is theoretically optional, only documented on Apple's docs iirc
    if (box_type != MKBETAG('d', 't', 'y', 'p')) {
        *next_ptr = ptr;
        return 0;
    }

    if (box_size < 12 || ptr + box_size > box_end) {
        av_log(avctx, AV_LOG_WARNING, "mebx_parse_dtyp_box: Invalid box size %u\n", box_size);
        return AVERROR_INVALIDDATA;
    }

    // Parse dtyp content: datatype namespace (4 bytes) + datatype value/string (rest)
    key_def->datatype_namespace = AV_RB32(ptr + 8);

    int dtyp_data_size = box_size - 12;
    if (key_def->datatype_namespace == 0) {
        // Well-known type
        if (dtyp_data_size >= 4) {
            key_def->datatype_value = AV_RB32(ptr + 12);
        } else {
            av_log(avctx, AV_LOG_WARNING, "mebx_parse_dtyp_box: Invalid dtyp datatype size %u\n", dtyp_data_size);

        }
    } else if (key_def->datatype_namespace == 1) {
        // Custom type: UTF-8 string (no null terminator in box)
        if (dtyp_data_size > 0) {
            key_def->datatype_string = av_malloc(dtyp_data_size + 1);
            if (key_def->datatype_string) {
                memcpy(key_def->datatype_string, ptr + 12, dtyp_data_size);
                key_def->datatype_string[dtyp_data_size] = '\0';
            }
        }
    }

    *next_ptr = ptr + box_size;
    return 0;
}

/**
 * Parse the keys box and extract metadata entries with type information.
 * The keys box contains a mapping of keys to numeric identifiers, and usually dtyp boxes for type info.
 *
 * Format:
 *   [4 bytes] box size
 *   [4 bytes] 'keys' fourcc
 *   For each entry:
 *     [MetadataKeyBox] - box ID
 *     [keyd box] - key definition
 *     [dtyp box] - type information
 */
static int mebx_parse_keys_box(AVCodecContext *avctx, const uint8_t *data, int size, MebxContext *ctx)
{
    const uint8_t *ptr = data;
    const uint8_t *end = data + size;

    if (size < 8)
        return AVERROR_INVALIDDATA;

    // Loop through all top-level boxes in the extradata
    while (ptr + 8 <= end) {
        uint32_t box_size = AV_RB32(ptr);
        uint32_t box_type = AV_RB32(ptr + 4);

        // Validate box size
        if (box_size < 8 || ptr + box_size > end) {
            av_log(avctx, AV_LOG_WARNING, "mebx_parse_keys_box: Invalid box size %u\n", box_size);
            break;
        }

        // Only process 'keys' boxes
        if (box_type == MKBETAG('k', 'e', 'y', 's')) {
            av_log(avctx, AV_LOG_TRACE, "mebx_parse_keys_box: Found 'keys' box, processing...\n");
            const uint8_t *box_ptr = ptr + 8;
            const uint8_t *box_end = ptr + box_size;

            // Each MetadataKeyBox: [size(4)][local_key_id(4)][keyd_box][dtyp_box]
            // First pass: count valid MetadataKeyBox entries (skip entries with local_key_id of 0)
            uint32_t count = 0;
            const uint8_t *scan_ptr = box_ptr;
            while (scan_ptr < box_end) {
                if (scan_ptr + 8 > box_end)
                    break;
                uint32_t key_box_size = AV_RB32(scan_ptr);
                if (key_box_size < 8 || scan_ptr + key_box_size > box_end)
                    break;
                uint32_t local_key_id = AV_RB32(scan_ptr + 4);
                // Only count non-zero key IDs (zero means disabled)
                if (local_key_id != 0)
                    count++;
                scan_ptr += key_box_size;
            }

            if (count == 0) {
                av_log(avctx, AV_LOG_DEBUG, "mebx_parse_keys_box: No MetadataKeyBox entries found\n");
            } else {
                av_log(avctx, AV_LOG_DEBUG, "mebx_parse_keys_box: found %u key entries\n", count);
                ctx->keys = av_malloc_array(count, sizeof(MebxKeyDef));
                if (!ctx->keys)
                    return AVERROR(ENOMEM);
                memset(ctx->keys, 0, count * sizeof(MebxKeyDef));
                ctx->nb_keys = count;

                // Process each MetadataKeyBox
                int key_idx = 0;
                while (key_idx < count && box_ptr < box_end) {
                    const uint8_t *next_ptr;
                    int ret;
                    uint32_t local_key_id;

                    // Extract local_key_id from the MetadataKeyBox box type
                    if (box_ptr + 8 > box_end) {
                        av_log(avctx, AV_LOG_ERROR, "mebx_parse_keys_box: Not enough data for MetadataKeyBox\n");
                        break;
                    }
                    uint32_t key_box_size = AV_RB32(box_ptr);
                    local_key_id = AV_RB32(box_ptr + 4);
                    box_ptr += 8;

                    // local_key_id of 0 is considered disabled
                    if (local_key_id == 0) {
                        av_log(avctx, AV_LOG_DEBUG, "mebx_parse_keys_box: Skipping MetadataKeyBox with local_key_id=0\n");
                        box_ptr += key_box_size - 8;
                        continue;
                    }

                    // Parse keyd box
                    ret = mebx_parse_keyd_box(avctx, box_ptr, box_ptr + key_box_size - 8, local_key_id, &ctx->keys[key_idx], &next_ptr);
                    if (ret < 0) {
                        av_log(avctx, AV_LOG_ERROR, "mebx_parse_keys_box: Failed to parse keyd box\n");
                        break;
                    }

                    box_ptr = next_ptr;

                    // Parse dtyp box following keyd
                    ret = mebx_parse_dtyp_box(avctx, box_ptr, box_end, &ctx->keys[key_idx], &next_ptr);
                    if (ret < 0) {
                        av_log(avctx, AV_LOG_ERROR, "mebx_parse_keys_box: Failed to parse dtyp box\n");
                        break;
                    }
                    box_ptr = next_ptr;

                    // Some logging
                    if (ctx->keys[key_idx].datatype_namespace == 0) {
                        av_log(avctx, AV_LOG_DEBUG, "mebx: key[%u,id=%u] %s (type=%u)\n",
                               key_idx, ctx->keys[key_idx].key_id, ctx->keys[key_idx].key_name, ctx->keys[key_idx].datatype_value);
                    } else if (ctx->keys[key_idx].datatype_namespace == 1) {
                        av_log(avctx, AV_LOG_DEBUG, "mebx: key[%u,id=%u] %s (custom type: %s)\n",
                               key_idx, ctx->keys[key_idx].key_id, ctx->keys[key_idx].key_name, ctx->keys[key_idx].datatype_string ? ctx->keys[key_idx].datatype_string : "");
                    } else {
                        av_log(avctx, AV_LOG_DEBUG, "mebx: key[%u,id=%u] %s (unknown namespace %u)\n",
                               key_idx, ctx->keys[key_idx].key_id, ctx->keys[key_idx].key_name, ctx->keys[key_idx].datatype_namespace);
                    }

                    // Store in metadata dictionary as well
                    char index_str[16];
                    snprintf(index_str, sizeof(index_str), "%u", key_idx + 1);
                    av_dict_set(&ctx->metadata, ctx->keys[key_idx].key_name, index_str, 0);

                    key_idx++;
                }
            }
        } else {
            av_log(avctx, AV_LOG_DEBUG, "mebx_parse_keys_box: Skipping unknown box type=0x%08x (box_size=%u)\n",
                   box_type, box_size);
        }

        // Move to next top-level box
        ptr += box_size;
    }

    return 0;
}

/**
 * Main mebx decoder function.
 * Parses the frame packet data which contains item entries.
 *
 * Packet format:
 *   [4 bytes] item size
 *   [4 bytes] item ID
 *   [variable] item data (binary, or a well-known type depending on header dtyp)
 */
static int mebx_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                             int *got_frame, AVPacket *avpkt)
{
    MebxContext *ctx = avctx->priv_data;
    const uint8_t *data = avpkt->data;
    int size = avpkt->size;
    const uint8_t *ptr = data;
    const uint8_t *end = data + size;

    if (size == 0) {
        // We shouldn't have empty packets as they should either be duplicated per-frame or missing
        av_log(avctx, AV_LOG_WARNING, "mebx_decode_frame: received empty packet (size=0)\n");
        *got_frame = 0;
        return 0;
    }

    // Parse item entries in the packet data
    // Each item has: [4 bytes size][4 bytes item_id][variable data]
    while (ptr + 8 <= end) {
        uint32_t item_size;
        uint32_t item_id;
        int data_size;
        char value_str[256] = { 0 }; // this might be too small??? Anything can really go in here but it tends to be small per-frame

        item_size = AV_RB32(ptr);
        item_id = AV_RB32(ptr + 4);

        if (item_size < 8 || ptr + item_size > end) {
            av_log(avctx, AV_LOG_WARNING, "mebx_decode_frame: invalid item size %u\n", item_size);
            break;
        }

        data_size = item_size - 8;  // Skip size and item_id fields

        av_log(avctx, AV_LOG_DEBUG, "mebx_decode_frame: item_id=%u, size=%u, data_size=%d\n",
               item_id, item_size, data_size);

        // Try to look up the key name from the keys array
        const char *key_name_ptr = NULL;

        for (int i = 0; i < ctx->nb_keys; i++) {
            if (ctx->keys[i].key_id == item_id) {
                key_name_ptr = ctx->keys[i].key_name;
                break;
            }
        }

        // Create the metadata entry
        if (key_name_ptr) {
            // Store binary data
            if (data_size > 0) {
                int str_pos = 0;
                for (int j = 0; j < data_size && str_pos < (int)sizeof(value_str) - 3; j++) {
                    str_pos += snprintf(value_str + str_pos, sizeof(value_str) - str_pos, "%02x", ptr[8 + j]);
                }
            }

            av_dict_set(&frame->metadata, key_name_ptr, value_str, 0);
            av_log(avctx, AV_LOG_DEBUG, "mebx_decode_frame: %s = %s\n", key_name_ptr, value_str);
        } else {
            // Unknown item ID - log it for now
            av_log(avctx, AV_LOG_DEBUG, "mebx_decode_frame: unknown item_id %u, skipping\n", item_id);
        }

        ptr += item_size;
    }

    // Set basic frame properties
    frame->pts = avpkt->pts;
    frame->pkt_dts = avpkt->dts;
    frame->time_base = avctx->pkt_timebase;
    if (avpkt->duration > 0)
        frame->duration = avpkt->duration;

    frame->format = 0;  // No specific format for data frames, set for validation

    // Store the original packet data as side-data for encoder to preserve it
    // Validation allows DATA frames with metadata/side-data but no buf[0]
    AVBufferRef *pkt_buf = av_buffer_create(av_memdup(avpkt->data, avpkt->size), avpkt->size,
                                            av_buffer_default_free, NULL, 0);
    if (!pkt_buf) {
        av_log(avctx, AV_LOG_ERROR, "mebx_decode_frame: Failed to allocate packet buffer\n");
        return AVERROR(ENOMEM);
    }

    AVFrameSideData *sd = av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_MEBX_PACKET, pkt_buf);
    if (!sd) {
        av_log(avctx, AV_LOG_ERROR, "mebx_decode_frame: Failed to attach packet data as side-data\n");
        av_buffer_unref(&pkt_buf);
        return AVERROR(ENOMEM);
    }

    *got_frame = 1;
    return avpkt->size;
}

static int mebx_decode_init(AVCodecContext *avctx)
{
    MebxContext *ctx = avctx->priv_data;
    if (avctx->extradata_size > 0) {
        mebx_parse_keys_box(avctx, avctx->extradata, avctx->extradata_size, ctx);
    }

    return 0;
}

static int mebx_decode_close(AVCodecContext *avctx)
{
    MebxContext *ctx = avctx->priv_data;

    if (ctx->keys) {
        for (int i = 0; i < ctx->nb_keys; i++) {
            if (ctx->keys[i].key_name) {
                av_free(ctx->keys[i].key_name);
            }
            if (ctx->keys[i].datatype_string) {
                av_free(ctx->keys[i].datatype_string);
            }
        }
        av_free(ctx->keys);
        ctx->keys = NULL;
        ctx->nb_keys = 0;
    }

    if (ctx->metadata) {
        av_dict_free(&ctx->metadata);
        ctx->metadata = NULL;
    }

    return 0;
}

const FFCodec ff_mebx_decoder = {
    .p.name         = "mebx",
    CODEC_LONG_NAME("Metadata Boxed"),
    .p.type         = AVMEDIA_TYPE_DATA,
    .p.id           = AV_CODEC_ID_MEBX,
    .priv_data_size = sizeof(MebxContext),
    .init           = mebx_decode_init,
    .close          = mebx_decode_close,
    FF_CODEC_DECODE_CB(mebx_decode_frame),
};
