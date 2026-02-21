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

#include "libavutil/file.c"

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "file.c";
    uint8_t *buf;
    size_t size;

    if (av_file_map(path, &buf, &size, 0, NULL) < 0)
        return 1;
    buf[0] = 's';
    printf("%s", buf);
    av_file_unmap(buf, size);

    if (av_file_map("no_such_file_xyz", &buf, &size, 0, NULL) >= 0) {
        av_file_unmap(buf, size);
        return 2;
    }

    if (av_file_map("no_such_file_xyz", &buf, &size, 1, NULL) >= 0) {
        av_file_unmap(buf, size);
        return 3;
    }

    return 0;
}
