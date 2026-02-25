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

/**
 * Unit tests for cbs_sei.c functions:
 *   ff_cbs_sei_find_type, ff_cbs_sei_alloc_message_payload,
 *   ff_cbs_sei_list_add, ff_cbs_sei_free_message_list,
 *   ff_cbs_sei_add_message, ff_cbs_sei_find_message,
 *   ff_cbs_sei_delete_message_type
 */

#include <stdio.h>
#include <string.h>

#include "libavutil/log.h"
#include "libavutil/macros.h"
#include "libavcodec/cbs.h"
#include "libavcodec/cbs_h264.h"
#include "libavcodec/cbs_sei.h"
#include "libavcodec/sei.h"

#define CHECK_RET(label, expr) do {             \
    int err__ = (expr);                         \
    if (err__ < 0) {                            \
        av_log(NULL, AV_LOG_ERROR,              \
               "%s failed: err=%d\n",          \
               label, err__);                   \
        ret = 1;                                \
        goto end;                               \
    }                                           \
} while (0)

/* ------------------------------------------------------------------ */
/* Test ff_cbs_sei_find_type                                           */
/* ------------------------------------------------------------------ */
static int test_find_type(CodedBitstreamContext *ctx)
{
    const SEIMessageTypeDescriptor *desc;

    /* Known common type - filler payload */
    desc = ff_cbs_sei_find_type(ctx, SEI_TYPE_FILLER_PAYLOAD);
    if (!desc) {
        av_log(NULL, AV_LOG_ERROR,
               "find_type: expected descriptor for SEI_TYPE_FILLER_PAYLOAD\n");
        return 1;
    }
    if (desc->type != SEI_TYPE_FILLER_PAYLOAD) {
        av_log(NULL, AV_LOG_ERROR,
               "find_type: wrong type returned: %d\n", desc->type);
        return 1;
    }

    /* User-data registered */
    desc = ff_cbs_sei_find_type(ctx, SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35);
    if (!desc) {
        av_log(NULL, AV_LOG_ERROR,
               "find_type: expected descriptor for user_data_registered\n");
        return 1;
    }

    /* User-data unregistered */
    desc = ff_cbs_sei_find_type(ctx, SEI_TYPE_USER_DATA_UNREGISTERED);
    if (!desc) {
        av_log(NULL, AV_LOG_ERROR,
               "find_type: expected descriptor for user_data_unregistered\n");
        return 1;
    }

    /* Mastering display colour volume */
    desc = ff_cbs_sei_find_type(ctx, SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME);
    if (!desc) {
        av_log(NULL, AV_LOG_ERROR,
               "find_type: expected descriptor for mastering_display_colour_volume\n");
        return 1;
    }

    /* Content light level */
    desc = ff_cbs_sei_find_type(ctx, SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO);
    if (!desc) {
        av_log(NULL, AV_LOG_ERROR,
               "find_type: expected descriptor for content_light_level\n");
        return 1;
    }

    /* Unknown type - should return NULL */
    desc = ff_cbs_sei_find_type(ctx, 0x7fff);
    if (desc) {
        av_log(NULL, AV_LOG_ERROR,
               "find_type: got unexpected descriptor for unknown type\n");
        return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Test ff_cbs_sei_list_add and ff_cbs_sei_free_message_list           */
/* ------------------------------------------------------------------ */
static int test_list_add_and_free(void)
{
    SEIRawMessageList list = { 0 };
    int ret = 0;

    /* Add several messages and verify counter grows */
    for (int i = 0; i < 5; i++) {
        ret = ff_cbs_sei_list_add(&list);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "list_add: iteration %d failed\n", i);
            goto done;
        }
        if (list.nb_messages != i + 1) {
            av_log(NULL, AV_LOG_ERROR,
                   "list_add: expected %d messages, got %d\n",
                   i + 1, list.nb_messages);
            ret = 1;
            goto done;
        }
    }

    /* Capacity must be >= nb_messages */
    if (list.nb_messages_allocated < list.nb_messages) {
        av_log(NULL, AV_LOG_ERROR,
               "list_add: nb_messages_allocated %d < nb_messages %d\n",
               list.nb_messages_allocated, list.nb_messages);
        ret = 1;
        goto done;
    }

done:
    ff_cbs_sei_free_message_list(&list);
    if (list.messages) {
        av_log(NULL, AV_LOG_ERROR,
               "free_message_list: messages pointer not NULLed\n");
        return 1;
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/* Test ff_cbs_sei_alloc_message_payload                               */
/* ------------------------------------------------------------------ */
static int test_alloc_message_payload(CodedBitstreamContext *ctx)
{
    const SEIMessageTypeDescriptor *desc;
    SEIRawMessage msg = { 0 };
    int ret;

    desc = ff_cbs_sei_find_type(ctx, SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO);
    if (!desc) {
        av_log(NULL, AV_LOG_ERROR,
               "alloc_payload: descriptor not found\n");
        return 1;
    }

    ret = ff_cbs_sei_alloc_message_payload(&msg, desc);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "alloc_payload: allocation failed\n");
        return 1;
    }
    if (!msg.payload || !msg.payload_ref) {
        av_log(NULL, AV_LOG_ERROR,
               "alloc_payload: payload or payload_ref is NULL\n");
        av_refstruct_unref(&msg.payload_ref);
        return 1;
    }
    if (msg.payload_type != SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO) {
        av_log(NULL, AV_LOG_ERROR,
               "alloc_payload: wrong payload_type %u\n", msg.payload_type);
        av_refstruct_unref(&msg.payload_ref);
        return 1;
    }

    av_refstruct_unref(&msg.payload_ref);

    /* Test with user_data_registered (has a special free function) */
    desc = ff_cbs_sei_find_type(ctx, SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35);
    if (!desc)
        return 1;
    memset(&msg, 0, sizeof(msg));
    ret = ff_cbs_sei_alloc_message_payload(&msg, desc);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "alloc_payload: user_data_registered allocation failed\n");
        return 1;
    }
    av_refstruct_unref(&msg.payload_ref);

    /* Test with user_data_unregistered (has another special free function) */
    desc = ff_cbs_sei_find_type(ctx, SEI_TYPE_USER_DATA_UNREGISTERED);
    if (!desc)
        return 1;
    memset(&msg, 0, sizeof(msg));
    ret = ff_cbs_sei_alloc_message_payload(&msg, desc);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "alloc_payload: user_data_unregistered allocation failed\n");
        return 1;
    }
    av_refstruct_unref(&msg.payload_ref);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Test ff_cbs_sei_add_message / find_message / delete_message_type   */
/* ------------------------------------------------------------------ */
static int test_add_find_delete(CodedBitstreamContext *ctx)
{
    CodedBitstreamFragment au = { 0 };
    SEIRawMessage *iter = NULL;
    SEIRawContentLightLevelInfo *cll;
    const SEIMessageTypeDescriptor *desc;
    int ret = 0;

    desc = ff_cbs_sei_find_type(ctx, SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO);
    if (!desc) {
        av_log(NULL, AV_LOG_ERROR, "add_find_delete: descriptor not found\n");
        return 1;
    }

    /* Add first message */
    {
        SEIRawMessage msg = { 0 };
        CHECK_RET("alloc_payload", ff_cbs_sei_alloc_message_payload(&msg, desc));

        cll = msg.payload;
        cll->max_content_light_level   = 1000;
        cll->max_pic_average_light_level = 400;

        ret = ff_cbs_sei_add_message(ctx, &au, 1,
                                     SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO,
                                     msg.payload, msg.payload_ref);
        av_refstruct_unref(&msg.payload_ref);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "add_find_delete: first add_message failed\n");
            ret = 1;
            goto end;
        }
    }

    /* Add second message of the same type */
    {
        SEIRawMessage msg = { 0 };
        CHECK_RET("alloc_payload 2", ff_cbs_sei_alloc_message_payload(&msg, desc));

        cll = msg.payload;
        cll->max_content_light_level   = 2000;
        cll->max_pic_average_light_level = 600;

        ret = ff_cbs_sei_add_message(ctx, &au, 1,
                                     SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO,
                                     msg.payload, msg.payload_ref);
        av_refstruct_unref(&msg.payload_ref);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "add_find_delete: second add_message failed\n");
            ret = 1;
            goto end;
        }
    }

    /* Find first message */
    iter = NULL;
    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO, &iter);
    if (ret < 0 || !iter) {
        av_log(NULL, AV_LOG_ERROR,
               "add_find_delete: first find_message failed\n");
        ret = 1;
        goto end;
    }
    cll = iter->payload;
    if (cll->max_content_light_level != 1000) {
        av_log(NULL, AV_LOG_ERROR,
               "add_find_delete: wrong CLL value %u (expected 1000)\n",
               cll->max_content_light_level);
        ret = 1;
        goto end;
    }

    /* Find second message using the iterator */
    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO, &iter);
    if (ret < 0 || !iter) {
        av_log(NULL, AV_LOG_ERROR,
               "add_find_delete: second find_message failed\n");
        ret = 1;
        goto end;
    }
    cll = iter->payload;
    if (cll->max_content_light_level != 2000) {
        av_log(NULL, AV_LOG_ERROR,
               "add_find_delete: wrong CLL value %u (expected 2000)\n",
               cll->max_content_light_level);
        ret = 1;
        goto end;
    }

    /* No more messages */
    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO, &iter);
    if (ret != AVERROR(ENOENT)) {
        av_log(NULL, AV_LOG_ERROR,
               "add_find_delete: expected ENOENT for exhausted iterator\n");
        ret = 1;
        goto end;
    }

    /* Delete all messages of this type */
    ff_cbs_sei_delete_message_type(ctx, &au, SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO);

    /* Verify they are all gone */
    iter = NULL;
    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO, &iter);
    if (ret != AVERROR(ENOENT)) {
        av_log(NULL, AV_LOG_ERROR,
               "add_find_delete: messages remain after delete_message_type\n");
        ret = 1;
        goto end;
    }

    ret = 0;

end:
    ff_cbs_fragment_free(&au);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Test add_message without a refcount (payload_ref == NULL)           */
/* ------------------------------------------------------------------ */
static int test_add_message_no_ref(CodedBitstreamContext *ctx)
{
    CodedBitstreamFragment au = { 0 };
    SEIRawContentLightLevelInfo cll = {
        .max_content_light_level     = 500,
        .max_pic_average_light_level = 200,
    };
    SEIRawMessage *iter = NULL;
    int ret = 0;

    ret = ff_cbs_sei_add_message(ctx, &au, 1,
                                 SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO,
                                 &cll, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "add_no_ref: add_message failed: %d\n", ret);
        ret = 1;
        goto end;
    }

    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO, &iter);
    if (ret != 0 || !iter) {
        av_log(NULL, AV_LOG_ERROR,
               "add_no_ref: find_message failed\n");
        ret = 1;
        goto end;
    }

    ret = 0;
end:
    ff_cbs_fragment_free(&au);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Test that find_message correctly iterates over multiple SEI units  */
/* ------------------------------------------------------------------ */
static int test_find_message_across_units(CodedBitstreamContext *ctx)
{
    CodedBitstreamFragment au = { 0 };
    SEIRawMasteringDisplayColourVolume mdcv = {
        .display_primaries_x         = { 13250, 7500, 34000 },
        .display_primaries_y         = { 34500, 3000, 16000 },
        .white_point_x               = 15635,
        .white_point_y               = 16450,
        .max_display_mastering_luminance = 10000000,
        .min_display_mastering_luminance = 50,
    };
    SEIRawContentLightLevelInfo cll = {
        .max_content_light_level     = 1000,
        .max_pic_average_light_level = 400,
    };
    SEIRawMessage *iter = NULL;
    int ret = 0;

    /* Add two different SEI types */
    ret = ff_cbs_sei_add_message(ctx, &au, 1,
                                 SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME,
                                 &mdcv, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "multi_unit: add mdcv failed\n");
        ret = 1;
        goto end;
    }

    ret = ff_cbs_sei_add_message(ctx, &au, 1,
                                 SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO,
                                 &cll, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "multi_unit: add cll failed\n");
        ret = 1;
        goto end;
    }

    /* Find mastering display */
    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME, &iter);
    if (ret != 0 || !iter) {
        av_log(NULL, AV_LOG_ERROR,
               "multi_unit: find mdcv failed\n");
        ret = 1;
        goto end;
    }

    /* Find content light level */
    iter = NULL;
    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO, &iter);
    if (ret != 0 || !iter) {
        av_log(NULL, AV_LOG_ERROR,
               "multi_unit: find cll failed\n");
        ret = 1;
        goto end;
    }

    /* Try a type not present */
    iter = NULL;
    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_FILLER_PAYLOAD, &iter);
    if (ret != AVERROR(ENOENT)) {
        av_log(NULL, AV_LOG_ERROR,
               "multi_unit: expected ENOENT for absent type\n");
        ret = 1;
        goto end;
    }

    /* Delete one type and ensure the other remains */
    ff_cbs_sei_delete_message_type(ctx, &au,
                                   SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME);
    iter = NULL;
    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO, &iter);
    if (ret != 0 || !iter) {
        av_log(NULL, AV_LOG_ERROR,
               "multi_unit: cll gone after deleting unrelated type\n");
        ret = 1;
        goto end;
    }

    ret = 0;
end:
    ff_cbs_fragment_free(&au);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Test find_type for H.264-specific types                             */
/* ------------------------------------------------------------------ */
static int test_find_type_h264(CodedBitstreamContext *ctx)
{
    const SEIMessageTypeDescriptor *desc;

    /* Buffering period is H.264-specific */
    desc = ff_cbs_sei_find_type(ctx, SEI_TYPE_BUFFERING_PERIOD);
    if (!desc) {
        av_log(NULL, AV_LOG_ERROR,
               "find_type_h264: expected descriptor for buffering_period\n");
        return 1;
    }

    /* Decoded picture hash is a common suffix type */
    desc = ff_cbs_sei_find_type(ctx, SEI_TYPE_DECODED_PICTURE_HASH);
    if (!desc) {
        av_log(NULL, AV_LOG_ERROR,
               "find_type_h264: expected descriptor for decoded_picture_hash\n");
        return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Test add/find/delete using a given CBS context (codec-agnostic).   */
/* Used for H.265 and H.266 to cover those branches in                */
/* cbs_sei_get_unit / cbs_sei_get_message_list.                       */
/* prefix=1 for H.264/H.265; H.266 uses prefix SEI as well.          */
/* ------------------------------------------------------------------ */
static int test_add_find_delete_codec(CodedBitstreamContext *ctx, int prefix,
                                      const char *codec_name)
{
    CodedBitstreamFragment au = { 0 };
    SEIRawContentLightLevelInfo cll = {
        .max_content_light_level     = 1000,
        .max_pic_average_light_level = 400,
    };
    SEIRawMessage *iter = NULL;
    int ret = 0;

    /* Add a message */
    ret = ff_cbs_sei_add_message(ctx, &au, prefix,
                                 SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO,
                                 &cll, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "add_find_delete_%s: add_message failed: %d\n", codec_name, ret);
        ret = 1;
        goto end;
    }

    /* Find it */
    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO, &iter);
    if (ret < 0 || !iter) {
        av_log(NULL, AV_LOG_ERROR,
               "add_find_delete_%s: find_message failed\n", codec_name);
        ret = 1;
        goto end;
    }

    /* Delete it */
    ff_cbs_sei_delete_message_type(ctx, &au, SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO);

    /* Verify gone */
    iter = NULL;
    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO, &iter);
    if (ret != AVERROR(ENOENT)) {
        av_log(NULL, AV_LOG_ERROR,
               "add_find_delete_%s: expected ENOENT after delete\n", codec_name);
        ret = 1;
        goto end;
    }

    ret = 0;
end:
    ff_cbs_fragment_free(&au);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Test H.265 suffix SEI unit (prefix=0 path in cbs_sei_get_unit)    */
/* ------------------------------------------------------------------ */
static int test_h265_suffix_sei(CodedBitstreamContext *ctx)
{
    CodedBitstreamFragment au = { 0 };
    SEIRawContentLightLevelInfo cll = {
        .max_content_light_level     = 500,
        .max_pic_average_light_level = 200,
    };
    SEIRawMessage *iter = NULL;
    int ret = 0;

    /* prefix=0 → suffix SEI (HEVC_NAL_SEI_SUFFIX path) */
    ret = ff_cbs_sei_add_message(ctx, &au, 0,
                                 SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO,
                                 &cll, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "h265_suffix: add_message failed: %d\n", ret);
        ret = 1;
        goto end;
    }

    ret = ff_cbs_sei_find_message(ctx, &au,
                                  SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO, &iter);
    if (ret < 0 || !iter) {
        av_log(NULL, AV_LOG_ERROR,
               "h265_suffix: find_message failed\n");
        ret = 1;
        goto end;
    }

    ret = 0;
end:
    ff_cbs_fragment_free(&au);
    return ret;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    CodedBitstreamContext *ctx264  = NULL;
    CodedBitstreamContext *ctx265  = NULL;
    CodedBitstreamContext *ctx266  = NULL;
    int ret = 0;
    int failed = 0;

#define RUN_TEST(name, ...) do {                                        \
    int r = name(__VA_ARGS__);                                          \
    if (r) {                                                            \
        av_log(NULL, AV_LOG_ERROR, "FAIL: %s\n", #name);               \
        failed++;                                                        \
    }                                                                   \
} while (0)

    /* H.264 context — required */
    ret = ff_cbs_init(&ctx264, AV_CODEC_ID_H264, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to init H264 CBS context: %d\n", ret);
        return 1;
    }

    /* H.265 context — optional (skip gracefully if not compiled in) */
    if (ff_cbs_init(&ctx265, AV_CODEC_ID_H265, NULL) < 0)
        ctx265 = NULL;

    /* H.266 context — optional */
    if (ff_cbs_init(&ctx266, AV_CODEC_ID_H266, NULL) < 0)
        ctx266 = NULL;

    /* -- H.264 tests -- */
    RUN_TEST(test_find_type,          ctx264);
    RUN_TEST(test_find_type_h264,     ctx264);
    RUN_TEST(test_list_add_and_free);
    RUN_TEST(test_alloc_message_payload, ctx264);
    RUN_TEST(test_add_find_delete,    ctx264);
    RUN_TEST(test_add_message_no_ref, ctx264);
    RUN_TEST(test_find_message_across_units, ctx264);

    /* -- H.265 tests: cover H265 branches in cbs_sei_get_unit /
          cbs_sei_get_message_list (skipped if H.265 CBS not compiled in) -- */
    if (ctx265) {
        RUN_TEST(test_add_find_delete_codec, ctx265, 1, "h265_prefix");
        RUN_TEST(test_h265_suffix_sei, ctx265);
    }

    /* -- H.266 tests: cover H266 branches (skipped if not compiled in) -- */
    if (ctx266)
        RUN_TEST(test_add_find_delete_codec, ctx266, 1, "h266_prefix");

    ff_cbs_close(&ctx264);
    ff_cbs_close(&ctx265);
    ff_cbs_close(&ctx266);

    if (failed) {
        av_log(NULL, AV_LOG_ERROR, "%d test(s) FAILED\n", failed);
        return 1;
    }

    return 0;
}
