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
 * Unit tests for libavutil/timecode.c:
 *   av_timecode_init, av_timecode_init_from_components,
 *   av_timecode_init_from_string, av_timecode_make_string,
 *   av_timecode_get_smpte_from_framenum, av_timecode_get_smpte,
 *   av_timecode_make_smpte_tc_string, av_timecode_make_smpte_tc_string2,
 *   av_timecode_make_mpeg_tc_string, av_timecode_adjust_ntsc_framenum2,
 *   av_timecode_check_frame_rate
 */

#include <stdio.h>
#include <string.h>

#include "libavutil/error.h"
#include "libavutil/macros.h"
#include "libavutil/rational.h"
#include "libavutil/timecode.h"

static int failed;

#define ASSERT_EQ(got, exp, label) do {                                     \
    if ((got) != (exp)) {                                                   \
        fprintf(stderr, "FAIL %s: got %d, expected %d\n",                  \
                label, (int)(got), (int)(exp));                             \
        failed++;                                                           \
    }                                                                       \
} while (0)

#define ASSERT_STR(got, exp, label) do {                                    \
    if (strcmp(got, exp)) {                                                  \
        fprintf(stderr, "FAIL %s: got '%s', expected '%s'\n",              \
                label, got, exp);                                            \
        failed++;                                                            \
    }                                                                        \
} while (0)

/* ----------------------------------------------------------------------- */
/* av_timecode_check_frame_rate                                             */
/* ----------------------------------------------------------------------- */
static void test_check_frame_rate(void)
{
    /* Standard rates must return 0 */
    static const struct { AVRational rate; int expect; } cases[] = {
        { {24,    1}, 0 },
        { {25,    1}, 0 },
        { {30,    1}, 0 },
        { {48,    1}, 0 },
        { {50,    1}, 0 },
        { {60,    1}, 0 },
        { {100,   1}, 0 },
        { {120,   1}, 0 },
        /* Non-standard → should return non-zero warning */
        { {23976, 1000}, -1 },
        { {29970, 1000}, -1 },
        /* Zero / invalid denominator */
        { {0, 0}, -1 },
        { {30, 0}, -1 },
    };
    for (int i = 0; i < FF_ARRAY_ELEMS(cases); i++) {
        int r = av_timecode_check_frame_rate(cases[i].rate);
        if (cases[i].expect == 0) {
            ASSERT_EQ(r, 0, "check_frame_rate standard");
        } else {
            if (r == 0) {
                fprintf(stderr,
                        "FAIL check_frame_rate: non-standard %d/%d returned 0\n",
                        cases[i].rate.num, cases[i].rate.den);
                failed++;
            }
        }
    }
}

/* ----------------------------------------------------------------------- */
/* av_timecode_init                                                         */
/* ----------------------------------------------------------------------- */
static void test_init(void)
{
    AVTimecode tc;
    int ret;

    /* Normal 25 fps */
    ret = av_timecode_init(&tc, (AVRational){25, 1}, 0, 0, NULL);
    ASSERT_EQ(ret, 0, "init 25fps");
    ASSERT_EQ(tc.fps, 25, "init fps");
    ASSERT_EQ(tc.start, 0, "init start");
    ASSERT_EQ(tc.rate.num, 25, "init rate.num");
    ASSERT_EQ(tc.rate.den,  1, "init rate.den");

    /* 30 fps with DROPFRAME is invalid for non-multiple-of-30000/1001 rates */
    ret = av_timecode_init(&tc, (AVRational){30, 1},
                           AV_TIMECODE_FLAG_DROPFRAME, 0, NULL);
    /* 30 fps IS a multiple of 30 → dropframe is allowed */
    ASSERT_EQ(ret, 0, "init 30fps dropframe");

    /* frame_start propagated */
    ret = av_timecode_init(&tc, (AVRational){24, 1}, 0, 100, NULL);
    ASSERT_EQ(ret, 0, "init with start");
    ASSERT_EQ(tc.start, 100, "init start propagated");

    /* Invalid: zero rate */
    ret = av_timecode_init(&tc, (AVRational){0, 1}, 0, 0, NULL);
    if (ret >= 0) {
        fprintf(stderr, "FAIL init: zero fps should fail, got ret=%d\n", ret);
        failed++;
    }

    /* Invalid: drop frame with non-multiple-of-30 fps */
    ret = av_timecode_init(&tc, (AVRational){25, 1},
                           AV_TIMECODE_FLAG_DROPFRAME, 0, NULL);
    if (ret >= 0) {
        fprintf(stderr,
                "FAIL init: drop+25fps should fail, got ret=%d\n", ret);
        failed++;
    }
}

/* ----------------------------------------------------------------------- */
/* av_timecode_init_from_components                                         */
/* ----------------------------------------------------------------------- */
static void test_init_from_components(void)
{
    AVTimecode tc;
    int ret;

    /* 00:00:00:00 at 25fps → start = 0 */
    ret = av_timecode_init_from_components(&tc, (AVRational){25, 1},
                                           0, 0, 0, 0, 0, NULL);
    ASSERT_EQ(ret, 0, "init_from_components 00:00:00:00");
    ASSERT_EQ(tc.start, 0, "components start 0");

    /* 01:00:00:00 at 25fps → start = 3600 * 25 = 90000 */
    ret = av_timecode_init_from_components(&tc, (AVRational){25, 1},
                                           0, 1, 0, 0, 0, NULL);
    ASSERT_EQ(ret, 0, "init_from_components 01:00:00:00");
    ASSERT_EQ(tc.start, 90000, "components start 01:00:00:00");

    /* 01:02:03:04 at 30fps → 1*3600*30 + 2*60*30 + 3*30 + 4 = 111694 */
    ret = av_timecode_init_from_components(&tc, (AVRational){30, 1},
                                           0, 1, 2, 3, 4, NULL);
    ASSERT_EQ(ret, 0, "init_from_components 01:02:03:04");
    ASSERT_EQ(tc.start, 111694, "components start 01:02:03:04");

    /* Drop frame 29.97: start should be adjusted for dropped frames */
    ret = av_timecode_init_from_components(&tc, (AVRational){30000, 1001},
                                           AV_TIMECODE_FLAG_DROPFRAME,
                                           1, 0, 0, 0, NULL);
    ASSERT_EQ(ret, 0, "init_from_components drop 01:00:00;00");
    /* 1h at 29.97 drop = 107892 frames (standard drop-frame count) */
    ASSERT_EQ(tc.start, 107892, "components drop start 01h");
}

/* ----------------------------------------------------------------------- */
/* av_timecode_init_from_string                                             */
/* ----------------------------------------------------------------------- */
static void test_init_from_string(void)
{
    AVTimecode tc;
    int ret;

    /* Non-drop ":" separator */
    ret = av_timecode_init_from_string(&tc, (AVRational){30, 1},
                                       "00:01:02:03", NULL);
    ASSERT_EQ(ret, 0, "init_from_string non-drop");
    ASSERT_EQ(!!(tc.flags & AV_TIMECODE_FLAG_DROPFRAME), 0, "string non-drop flag");
    /* 0*3600*30 + 1*60*30 + 2*30 + 3 = 1863 */
    ASSERT_EQ(tc.start, 1863, "string non-drop start");

    /* Drop ";" separator */
    ret = av_timecode_init_from_string(&tc, (AVRational){30000, 1001},
                                       "00:01:00;02", NULL);
    ASSERT_EQ(ret, 0, "init_from_string drop");
    ASSERT_EQ(!!(tc.flags & AV_TIMECODE_FLAG_DROPFRAME), 1, "string drop flag");

    /* Dot '.' separator also means drop */
    ret = av_timecode_init_from_string(&tc, (AVRational){30000, 1001},
                                       "01:00:00.00", NULL);
    ASSERT_EQ(ret, 0, "init_from_string dot-drop");
    ASSERT_EQ(!!(tc.flags & AV_TIMECODE_FLAG_DROPFRAME), 1, "string dot-drop flag");

    /* Invalid string */
    ret = av_timecode_init_from_string(&tc, (AVRational){25, 1},
                                       "notvalid", NULL);
    if (ret >= 0) {
        fprintf(stderr, "FAIL init_from_string: invalid str should fail\n");
        failed++;
    }
}

/* ----------------------------------------------------------------------- */
/* av_timecode_make_string                                                  */
/* ----------------------------------------------------------------------- */
static void test_make_string(void)
{
    AVTimecode tc;
    char buf[AV_TIMECODE_STR_SIZE];

    /* 25fps, no flags: "00:00:01:10" for frame 35 (= 1s*25 + 10) */
    av_timecode_init(&tc, (AVRational){25, 1}, 0, 0, NULL);
    av_timecode_make_string(&tc, buf, 35);
    ASSERT_STR(buf, "00:00:01:10", "make_string 25fps frame 35");

    /* frame 0 → "00:00:00:00" */
    av_timecode_make_string(&tc, buf, 0);
    ASSERT_STR(buf, "00:00:00:00", "make_string frame 0");

    /* 30fps non-drop, frame 30 → "00:00:01:00" */
    av_timecode_init(&tc, (AVRational){30, 1}, 0, 0, NULL);
    av_timecode_make_string(&tc, buf, 30);
    ASSERT_STR(buf, "00:00:01:00", "make_string 30fps frame 30");

    /* AV_TIMECODE_FLAG_24HOURSMAX: hour wraps at 24 */
    av_timecode_init(&tc, (AVRational){25, 1},
                     AV_TIMECODE_FLAG_24HOURSMAX, 0, NULL);
    /* frame at 25h = 25*3600*25 = 2250000 frames, should wrap to 01:00:00:00 */
    av_timecode_make_string(&tc, buf, 25 * 3600 * 25);
    ASSERT_STR(buf, "01:00:00:00", "make_string 24h wrap");

    /* Drop-frame 29.97: separator should be ';' */
    av_timecode_init(&tc, (AVRational){30000, 1001},
                     AV_TIMECODE_FLAG_DROPFRAME, 0, NULL);
    av_timecode_make_string(&tc, buf, 0);
    if (strchr(buf, ';') == NULL) {
        fprintf(stderr, "FAIL make_string: drop frame should use ';', got '%s'\n", buf);
        failed++;
    }
}

/* ----------------------------------------------------------------------- */
/* av_timecode_make_smpte_tc_string                                         */
/* ----------------------------------------------------------------------- */
static void test_make_smpte_tc_string(void)
{
    char buf[AV_TIMECODE_STR_SIZE];
    AVTimecode tc;
    uint32_t smpte;

    /* Build SMPTE code for 01:02:03:04 at 30fps non-drop */
    smpte = av_timecode_get_smpte((AVRational){30, 1}, 0, 1, 2, 3, 4);
    av_timecode_make_smpte_tc_string(buf, smpte, 1);
    ASSERT_STR(buf, "01:02:03:04", "smpte_tc_string 01:02:03:04");

    /* Using av_timecode_get_smpte_from_framenum */
    av_timecode_init(&tc, (AVRational){25, 1}, 0, 0, NULL);
    /* frame 25*3600 + 25*60 + 25*1 + 5 = 91530 → 01:01:01:05 at 25fps */
    smpte = av_timecode_get_smpte_from_framenum(&tc, 25 * 3600 + 25 * 60 + 25 + 5);
    av_timecode_make_smpte_tc_string(buf, smpte, 1);
    ASSERT_STR(buf, "01:01:01:05", "smpte_from_framenum 01:01:01:05");
}

/* ----------------------------------------------------------------------- */
/* av_timecode_make_smpte_tc_string2                                        */
/* ----------------------------------------------------------------------- */
static void test_make_smpte_tc_string2(void)
{
    char buf[AV_TIMECODE_STR_SIZE];
    uint32_t smpte;

    /* 50fps: frame 0 should parse to "00:00:00:00" */
    smpte = av_timecode_get_smpte((AVRational){50, 1}, 0, 0, 0, 0, 0);
    av_timecode_make_smpte_tc_string2(buf, (AVRational){50, 1}, smpte, 1, 0);
    ASSERT_STR(buf, "00:00:00:00", "smpte_tc_string2 50fps 00:00:00:00");

    /* 60fps at 01:00:00:00 */
    smpte = av_timecode_get_smpte((AVRational){60, 1}, 0, 1, 0, 0, 0);
    av_timecode_make_smpte_tc_string2(buf, (AVRational){60, 1}, smpte, 1, 0);
    ASSERT_STR(buf, "01:00:00:00", "smpte_tc_string2 60fps 01:00:00:00");
}

/* ----------------------------------------------------------------------- */
/* av_timecode_make_mpeg_tc_string                                          */
/* ----------------------------------------------------------------------- */
static void test_make_mpeg_tc_string(void)
{
    char buf[AV_TIMECODE_STR_SIZE];

    /* Build 25-bit MPEG timecode manually for 01:02:03:04 non-drop:
     * hh=1, mm=2, ss=3, ff=4
     * tc25bit = (hh<<19)|(mm<<13)|(ss<<6)|ff */
    uint32_t tc25 = (1u << 19) | (2u << 13) | (3u << 6) | 4u;
    av_timecode_make_mpeg_tc_string(buf, tc25);
    ASSERT_STR(buf, "01:02:03:04", "mpeg_tc_string 01:02:03:04");

    /* Drop flag bit 24=1 → separator ';' */
    uint32_t tc25_drop = tc25 | (1u << 24);
    av_timecode_make_mpeg_tc_string(buf, tc25_drop);
    if (strchr(buf, ';') == NULL) {
        fprintf(stderr,
                "FAIL mpeg_tc_string: drop flag should produce ';', got '%s'\n", buf);
        failed++;
    }
}

/* ----------------------------------------------------------------------- */
/* av_timecode_adjust_ntsc_framenum2                                        */
/* ----------------------------------------------------------------------- */
static void test_adjust_ntsc(void)
{
    /* 29.97 drop: frame 0 → 0 */
    ASSERT_EQ(av_timecode_adjust_ntsc_framenum2(0, 30), 0, "ntsc_adjust frame 0");

    /* Frame 1800 (1 min at 30fps) → subtract 2 (first minute skip) */
    ASSERT_EQ(av_timecode_adjust_ntsc_framenum2(1800, 30), 1800 + 2,
              "ntsc_adjust 1800 (1min at 30)");

    /* Non-multiple of 30 → unchanged */
    ASSERT_EQ(av_timecode_adjust_ntsc_framenum2(1000, 25), 1000,
              "ntsc_adjust non-30 fps");
    ASSERT_EQ(av_timecode_adjust_ntsc_framenum2(1000, 0), 1000,
              "ntsc_adjust fps=0");

    /* 60fps (2x30): drop_frames=4, frames_per_10mins=35964 */
    int adj = av_timecode_adjust_ntsc_framenum2(3600, 60);
    /* 3600 frames into 1 min at 60fps → +4 skipped */
    ASSERT_EQ(adj, 3600 + 4, "ntsc_adjust 60fps 1min");
}

/* ----------------------------------------------------------------------- */
/* av_timecode_get_smpte roundtrip                                          */
/* ----------------------------------------------------------------------- */
static void test_get_smpte_roundtrip(void)
{
    /* Encode and decode 12:34:56:07 at 30fps, check the string matches */
    char buf[AV_TIMECODE_STR_SIZE];
    uint32_t smpte = av_timecode_get_smpte((AVRational){30, 1}, 0, 12, 34, 56, 7);
    av_timecode_make_smpte_tc_string(buf, smpte, 1);
    ASSERT_STR(buf, "12:34:56:07", "smpte roundtrip 12:34:56:07");

    /* 00:00:00:00 */
    smpte = av_timecode_get_smpte((AVRational){30, 1}, 0, 0, 0, 0, 0);
    av_timecode_make_smpte_tc_string(buf, smpte, 1);
    ASSERT_STR(buf, "00:00:00:00", "smpte roundtrip 00:00:00:00");

    /* Drop-frame bit */
    smpte = av_timecode_get_smpte((AVRational){30000, 1001}, 1, 0, 1, 0, 2);
    if (!(smpte & (1u << 30))) {
        fprintf(stderr, "FAIL get_smpte: drop bit not set\n");
        failed++;
    }
    av_timecode_make_smpte_tc_string(buf, smpte, 0 /* allow drop */);
    /* separator should be ';' for drop */
    if (strchr(buf, ';') == NULL) {
        fprintf(stderr,
                "FAIL get_smpte: drop roundtrip missing ';', got '%s'\n", buf);
        failed++;
    }
}

int main(void)
{
    test_check_frame_rate();
    test_init();
    test_init_from_components();
    test_init_from_string();
    test_make_string();
    test_make_smpte_tc_string();
    test_make_smpte_tc_string2();
    test_make_mpeg_tc_string();
    test_adjust_ntsc();
    test_get_smpte_roundtrip();

    if (failed) {
        fprintf(stderr, "%d test(s) FAILED\n", failed);
        return 1;
    }
    return 0;
}
