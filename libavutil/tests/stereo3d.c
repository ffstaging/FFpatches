/*
 * Copyright (c) 2026 Soham Kute
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

#include <stddef.h>
#include <stdio.h>

#include "libavutil/stereo3d.c"

static int test_alloc(void)
{
    AVStereo3D *s;
    size_t size;

    /* av_stereo3d_alloc: NULL size path */
    s = av_stereo3d_alloc();
    if (!s) {
        fprintf(stderr, "av_stereo3d_alloc failed\n");
        return 1;
    }
    printf("alloc: type=%d flags=%d view=%d eye=%d\n",
           s->type, s->flags, s->view, s->primary_eye);
    printf("alloc: disparity=%d/%d fov=%d/%d\n",
           s->horizontal_disparity_adjustment.num,
           s->horizontal_disparity_adjustment.den,
           s->horizontal_field_of_view.num,
           s->horizontal_field_of_view.den);
    av_free(s);

    /* av_stereo3d_alloc_size: non-NULL size path */
    size = 0;
    s = av_stereo3d_alloc_size(&size);
    if (!s) {
        fprintf(stderr, "av_stereo3d_alloc_size failed\n");
        return 1;
    }
    if (size != sizeof(AVStereo3D)) {
        fprintf(stderr, "alloc_size: wrong size %zu, expected %zu\n", size, sizeof(AVStereo3D));
        av_free(s);
        return 1;
    }
    printf("alloc_size: size=ok\n");
    av_free(s);
    return 0;
}

static void test_type_names(void)
{
    static const int types[] = {
        AV_STEREO3D_2D,
        AV_STEREO3D_SIDEBYSIDE,
        AV_STEREO3D_TOPBOTTOM,
        AV_STEREO3D_FRAMESEQUENCE,
        AV_STEREO3D_CHECKERBOARD,
        AV_STEREO3D_SIDEBYSIDE_QUINCUNX,
        AV_STEREO3D_LINES,
        AV_STEREO3D_COLUMNS,
        AV_STEREO3D_UNSPEC,
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(types); i++)
        printf("type_name(%d): %s\n", types[i], av_stereo3d_type_name(types[i]));

    /* out-of-bounds -> "unknown" */
    printf("type_name(9999): %s\n", av_stereo3d_type_name(9999));

    /* from_name: round-trip each valid name */
    for (int i = 0; i < FF_ARRAY_ELEMS(types); i++) {
        const char *name = av_stereo3d_type_name(types[i]);
        printf("from_name(%s): %d\n", name, av_stereo3d_from_name(name));
    }

    /* from_name: no match -> -1 */
    printf("from_name(invalid): %d\n", av_stereo3d_from_name("invalid"));
}

static void test_view_names(void)
{
    static const int views[] = {
        AV_STEREO3D_VIEW_PACKED,
        AV_STEREO3D_VIEW_LEFT,
        AV_STEREO3D_VIEW_RIGHT,
        AV_STEREO3D_VIEW_UNSPEC,
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(views); i++)
        printf("view_name(%d): %s\n", views[i], av_stereo3d_view_name(views[i]));

    /* out-of-bounds -> "unknown" */
    printf("view_name(9999): %s\n", av_stereo3d_view_name(9999));

    for (int i = 0; i < FF_ARRAY_ELEMS(views); i++) {
        const char *name = av_stereo3d_view_name(views[i]);
        printf("view_from_name(%s): %d\n", name, av_stereo3d_view_from_name(name));
    }

    printf("view_from_name(invalid): %d\n", av_stereo3d_view_from_name("invalid"));
}

static void test_eye_names(void)
{
    static const int eyes[] = {
        AV_PRIMARY_EYE_NONE,
        AV_PRIMARY_EYE_LEFT,
        AV_PRIMARY_EYE_RIGHT,
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(eyes); i++)
        printf("eye_name(%d): %s\n", eyes[i], av_stereo3d_primary_eye_name(eyes[i]));

    /* out-of-bounds -> "unknown" */
    printf("eye_name(9999): %s\n", av_stereo3d_primary_eye_name(9999));

    for (int i = 0; i < FF_ARRAY_ELEMS(eyes); i++) {
        const char *name = av_stereo3d_primary_eye_name(eyes[i]);
        printf("eye_from_name(%s): %d\n", name, av_stereo3d_primary_eye_from_name(name));
    }

    printf("eye_from_name(invalid): %d\n", av_stereo3d_primary_eye_from_name("invalid"));
}

static int test_create_side_data(void)
{
    AVFrame *frame = av_frame_alloc();
    AVStereo3D *s;

    if (!frame) {
        fprintf(stderr, "av_frame_alloc failed\n");
        return 1;
    }

    s = av_stereo3d_create_side_data(frame);
    if (!s) {
        fprintf(stderr, "av_stereo3d_create_side_data failed\n");
        av_frame_free(&frame);
        return 1;
    }

    printf("side_data: type=%d flags=%d view=%d eye=%d\n",
           s->type, s->flags, s->view, s->primary_eye);
    printf("side_data: disparity=%d/%d fov=%d/%d\n",
           s->horizontal_disparity_adjustment.num,
           s->horizontal_disparity_adjustment.den,
           s->horizontal_field_of_view.num,
           s->horizontal_field_of_view.den);

    av_frame_free(&frame);
    return 0;
}

int main(void)
{
    int ret = 0;

    ret |= test_alloc();
    test_type_names();
    test_view_names();
    test_eye_names();
    ret |= test_create_side_data();

    return ret;
}