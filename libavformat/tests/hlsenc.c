/*
 * Copyright (c) 2025 FFmpeg developers
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

#include <stdio.h>
#include <string.h>

static int test_vtt_filename_fmtstr(void)
{
    int ret = 0;
    struct {
        const char *vtt_basename;
        int vtt_index;
        const char *expected;
    } tests[] = {
        { "normal%d.vtt",        5, "normal5.vtt" },
        { "test_%s_file%d.vtt", 10, "test_%s_file10.vtt" },
        { "leak%x%x%d.vtt",      0, "leak%x%x0.vtt" },
        { "%p%n%d.vtt",          1, "%p%n1.vtt" },
        { "safe_name%d.vtt",   123, "safe_name123.vtt" },
    };
    int i;

    printf("Testing VTT filename format string safety:\n");

    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        const char *vtt_basename = tests[i].vtt_basename;
        int vtt_index = tests[i].vtt_index;
        const char *expected = tests[i].expected;
        const char *fmt;
        int base_len, len;
        char vtt_file[256];

        fmt = strstr(vtt_basename, "%d");
        base_len = fmt ? (int)(fmt - vtt_basename) : strlen(vtt_basename);
        len = base_len + 25;

        if (len > sizeof(vtt_file))
            len = sizeof(vtt_file);

        snprintf(vtt_file, len, "%.*s%d.vtt", base_len, vtt_basename, vtt_index);

        if (strcmp(vtt_file, expected) != 0) {
            printf("  FAIL: input='%s' idx=%d => '%s' (expected '%s')\n",
                   vtt_basename, vtt_index, vtt_file, expected);
            ret = 1;
        } else {
            printf("  PASS: input='%s' idx=%d => '%s'\n",
                   vtt_basename, vtt_index, vtt_file);
        }
    }

    return ret;
}

int main(void)
{
    int ret = 0;

    ret |= test_vtt_filename_fmtstr();

    return ret;
}
