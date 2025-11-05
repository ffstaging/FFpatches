/*
 * copyright (c) 2025 Lukas Holliger
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

#ifndef AVUTIL_QTFF_H
#define AVUTIL_QTFF_H

#include <stdint.h>

/**
 * @file
 * QuickTime File Format (QTFF) utilities
 */

/**
 * Convert a QuickTime well-known type to a string
 *
 * @param data_type QuickTime metadata data type
 * @param data      Pointer to the binary data
 * @param data_size Size of the binary data in bytes
 * @param str       Buffer to write the string representation to
 * @param str_size  Size of the output buffer
 * @return          0 on success, negative AVERROR code on failure
 *
 * @see https://developer.apple.com/documentation/quicktime-file-format/well-known_types
 */
int av_qtff_convert_well_known_to_str(int data_type, const uint8_t *data, int data_size,
                          char *str, int str_size);

/**
 * Convert a string to QuickTime well-known value
 *
 * @param data_type QuickTime metadata data type
 * @param str       Input string to convert
 * @param data      Buffer to write the binary data to
 * @param data_size Size to use for the binary data
 * @return          Number of bytes written on success, negative AVERROR code on failure
 *
 * @see https://developer.apple.com/documentation/quicktime-file-format/well-known_types
 */
int av_qtff_convert_str_to_well_known(int data_type, const char *str, uint8_t *data, int data_size);

#endif /* AVUTIL_QTFF_H */
