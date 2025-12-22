/*
 * Fuzzer for libavutil
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

#include "config.h"
#include "libavutil/avutil.h"
#include "libavutil/eval.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

// Dummy class for option fuzzing
typedef struct DummyContext {
    const AVClass *class;
    int int_val;
    char *str_val;
    double dbl_val;
    int64_t i64_val;
} DummyContext;

static const AVOption dummy_options[] = {
    { "int", "integer option", offsetof(DummyContext, int_val), AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, 0 },
    { "str", "string option", offsetof(DummyContext, str_val), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, 0 },
    { "dbl", "double option", offsetof(DummyContext, dbl_val), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, -100.0, 100.0, 0 },
    { "i64", "int64 option", offsetof(DummyContext, i64_val), AV_OPT_TYPE_INT64, {.i64 = 0}, INT64_MIN, INT64_MAX, 0 },
    { NULL }
};

static const AVClass dummy_class = {
    .class_name = "Dummy",
    .item_name  = av_default_item_name,
    .option     = dummy_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char *str;
    
    // Limit size to avoid excessive processing time
    if (size > 8192) size = 8192;

    str = av_malloc(size + 1);
    if (!str) return 0;
    
    memcpy(str, data, size);
    str[size] = 0;

#ifdef FFMPEG_AVUTIL_OPT
    DummyContext obj = { .class = &dummy_class };
    av_opt_set_defaults(&obj);
    
    // Fuzz option parsing (key=value:key2=value2...)
    av_set_options_string(&obj, str, "=", ":");
    
    av_opt_free(&obj);
#else
    // Default: av_expr
    double res;
    // Fuzz av_expr_parse_and_eval
    av_expr_parse_and_eval(&res, str, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, NULL);
#endif
    
    av_free(str);
    return 0;
}