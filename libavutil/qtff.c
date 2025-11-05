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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qtff.h"
#include "intfloat.h"
#include "intreadwrite.h"
#include "error.h"

int av_qtff_convert_well_known_to_str(int data_type, const uint8_t *data, int data_size,
                                      char *str, int str_size)
{
    if (!data || !str || str_size <= 0)
        return AVERROR(EINVAL);

    switch (data_type) {
    case 0:  // Reserved
    case 2:  // UTF-16
    case 3:  // S/JIS
    case 4:  // UTF-8 sort
    case 5:  // UTF-16 sort
    case 13: // JPEG
    case 14: // PNG
    case 27: // BMP
    case 28: // QuickTime Metadata atom
    case 70: // BE PointF32
    case 71: // BE DimensionsF32
    case 72: // BE RectF32
    case 79: // AffineTransformF64
        return AVERROR_PATCHWELCOME;
    case 1: {  // UTF-8
        int len = data_size < str_size - 1 ? data_size : str_size - 1;
        memcpy(str, data, len);
        str[len] = '\0';
        break;
    }
    case 21: {  // BE Signed Integer (variable size, not usable for timed metadata)
        int val = 0;
        switch (data_size) {
        case 1:
            val = (int8_t)AV_RB8(data);
            break;
        case 2:
            val = (int16_t)AV_RB16(data);
            break;
        case 3:
            val = ((int32_t)(AV_RB24(data) << 8)) >> 8;
            break;
        case 4:
            val = (int32_t)AV_RB32(data);
            break;
        default:
            return AVERROR(EINVAL);
        }

        if (snprintf(str, str_size, "%d", val) >= str_size)
            return AVERROR(ENOMEM);
        break;
    }
    case 22: {  // BE Unsigned Integer (variable size, not usable for timed metadata)
        unsigned int val = 0;
        switch (data_size) {
        case 1:
            val = AV_RB8(data);
            break;
        case 2:
            val = AV_RB16(data);
            break;
        case 3:
            val = AV_RB24(data);
            break;
        case 4:
            val = AV_RB32(data);
            break;
        default:
            return AVERROR(EINVAL);
        }

        if (snprintf(str, str_size, "%u", val) >= str_size)
            return AVERROR(ENOMEM);
        break;
    }
    case 23: {  // BE float32
        float val;
        if (data_size != 4)
            return AVERROR(EINVAL);

        val = av_int2float(AV_RB32(data));
        if (snprintf(str, str_size, "%f", val) >= str_size)
            return AVERROR(ENOMEM);
        break;
    }
    case 24: {  // BE float64
        double val;
        if (data_size != 8)
            return AVERROR(EINVAL);

        val = av_int2double(AV_RB64(data));
        if (snprintf(str, str_size, "%f", val) >= str_size)
            return AVERROR(ENOMEM);
        break;
    }
    case 65:  // 8-bit Signed Integer
        if (data_size != 1)
            return AVERROR(EINVAL);
        if (snprintf(str, str_size, "%d", (int)(int8_t)data[0]) >= str_size)
            return AVERROR(ENOMEM);
        break;
    case 66:  // BE 16-bit Signed Integer
        if (data_size != 2)
            return AVERROR(EINVAL);
        if (snprintf(str, str_size, "%d", (int)(int16_t)AV_RB16(data)) >= str_size)
            return AVERROR(ENOMEM);
        break;
    case 67:  // BE 32-bit Signed Integer
        if (data_size != 4)
            return AVERROR(EINVAL);
        if (snprintf(str, str_size, "%d", (int)(int32_t)AV_RB32(data)) >= str_size)
            return AVERROR(ENOMEM);
        break;
    case 74:  // BE 64-bit Signed Integer
        if (data_size != 8)
            return AVERROR(EINVAL);
        if (snprintf(str, str_size, "%lld", (long long)(int64_t)AV_RB64(data)) >= str_size)
            return AVERROR(ENOMEM);
        break;
    case 75:  // 8-bit Unsigned Integer
        if (data_size != 1)
            return AVERROR(EINVAL);
        if (snprintf(str, str_size, "%u", (unsigned int)data[0]) >= str_size)
            return AVERROR(ENOMEM);
        break;
    case 76:  // BE 16-bit Unsigned Integer
        if (data_size != 2)
            return AVERROR(EINVAL);
        if (snprintf(str, str_size, "%u", (unsigned int)AV_RB16(data)) >= str_size)
            return AVERROR(ENOMEM);
        break;
    case 77:  // BE 32-bit Unsigned Integer
        if (data_size != 4)
            return AVERROR(EINVAL);
        if (snprintf(str, str_size, "%u", (unsigned int)AV_RB32(data)) >= str_size)
            return AVERROR(ENOMEM);
        break;
    case 78:  // BE 64-bit Unsigned Integer
        if (data_size != 8)
            return AVERROR(EINVAL);
        if (snprintf(str, str_size, "%llu", (unsigned long long)AV_RB64(data)) >= str_size)
            return AVERROR(ENOMEM);
        break;
    default:
        return AVERROR(EINVAL);
    }

    return 0;
}

int av_qtff_convert_str_to_well_known(int data_type, const char *str, uint8_t *data, int data_size)
{
    if (!str || !data)
        return AVERROR(EINVAL);

    switch (data_type) {
    case 0:  // Reserved
    case 2:  // UTF-16
    case 3:  // S/JIS
    case 4:  // UTF-8 sort
    case 5:  // UTF-16 sort
    case 13: // JPEG
    case 14: // PNG
    case 27: // BMP
    case 28: // QuickTime Metadata atom
    case 70: // BE PointF32
    case 71: // BE DimensionsF32
    case 72: // BE RectF32
    case 79: // AffineTransformF64
        return AVERROR_PATCHWELCOME; // these are defined well-known types, but not implemented to be parsed
    case 1: {  // UTF-8
        int len = strlen(str);
        if (len > data_size)
            return AVERROR(ENOMEM);
        memcpy(data, str, len);
        return len;
    }
    case 21: {  // BE Signed Integer (variable size, not usable for timed metadata)
        long long val;
        char *endptr;

        val = strtoll(str, &endptr, 10);
        if (endptr == str || *endptr != '\0')
            return AVERROR(EINVAL);

        switch (data_size) {
        case 1:
            if (val < INT8_MIN || val > INT8_MAX)
                return AVERROR(ERANGE);
            AV_WB8(data, (uint8_t)val);
            break;
        case 2:
            if (val < INT16_MIN || val > INT16_MAX)
                return AVERROR(ERANGE);
            AV_WB16(data, (int16_t)val);
            break;
        case 3:
            if (val < -8388608 || val > 8388607)  // 24-bit signed range
                return AVERROR(ERANGE);
            AV_WB24(data, (int32_t)val);
            break;
        case 4:
            if (val < INT32_MIN || val > INT32_MAX)
                return AVERROR(ERANGE);
            AV_WB32(data, (int32_t)val);
            break;
        default:
            return AVERROR(EINVAL);
        }
        break;
    }
    case 22: {  // BE unsigned integer, variable size
        unsigned long long val;
        char *endptr;

        val = strtoull(str, &endptr, 10);
        if (endptr == str || *endptr != '\0')
            return AVERROR(EINVAL);

        switch (data_size) {
        case 1:
            if (val > UINT8_MAX)
                return AVERROR(ERANGE);
            AV_WB8(data, (uint8_t)val);
            break;
        case 2:
            if (val > UINT16_MAX)
                return AVERROR(ERANGE);
            AV_WB16(data, (uint16_t)val);
            break;
        case 3:
            if (val > 16777215)  // 24-bit unsigned range
                return AVERROR(ERANGE);
            AV_WB24(data, (uint32_t)val);
            break;
        case 4:
            if (val > UINT32_MAX)
                return AVERROR(ERANGE);
            AV_WB32(data, (uint32_t)val);
            break;
        default:
            return AVERROR(EINVAL);
        }
        break;
    }
    case 23: {  // BE float32
        float val;
        char *endptr;

        if (data_size != 4)
            return AVERROR(EINVAL);

        val = strtof(str, &endptr);
        if (endptr == str || *endptr != '\0')
            return AVERROR(EINVAL);

        AV_WB32(data, av_float2int(val));
        break;
    }
    case 24: {  // BE float64
        double val;
        char *endptr;

        if (data_size != 8)
            return AVERROR(EINVAL);

        val = strtod(str, &endptr);
        if (endptr == str || *endptr != '\0')
            return AVERROR(EINVAL);

        AV_WB64(data, av_double2int(val));
        break;
    }
    case 65:    // 8-bit Signed Integer
    case 66:    // BE 16-bit Signed Integer
    case 67:    // BE 32-bit Signed Integer
    case 74: {  // BE 64-bit Signed Integer
        long long val;
        char *endptr;
        int expected_size = (data_type == 65) ? 1 : (data_type == 66) ? 2 : (data_type == 67) ? 4 : 8;

        if (data_size != expected_size)
            return AVERROR(EINVAL);

        val = strtoll(str, &endptr, 10);
        if (endptr == str || *endptr != '\0')
            return AVERROR(EINVAL);

        switch (data_type) {
        case 65:
            if (val < INT8_MIN || val > INT8_MAX)
                return AVERROR(ERANGE);
            AV_WB8(data, (uint8_t)val);
            break;
        case 66:
            if (val < INT16_MIN || val > INT16_MAX)
                return AVERROR(ERANGE);
            AV_WB16(data, (int16_t)val);
            break;
        case 67:
            if (val < INT32_MIN || val > INT32_MAX)
                return AVERROR(ERANGE);
            AV_WB32(data, (int32_t)val);
            break;
        case 74:
            AV_WB64(data, (int64_t)val);
            break;
        }
        break;
    }
    case 75:    // 8-bit Unsigned Integer
    case 76:    // BE 16-bit Unsigned Integer
    case 77:    // BE 32-bit Unsigned Integer
    case 78: {  // BE 64-bit Unsigned Integer
        unsigned long long val;
        char *endptr;
        int expected_size = (data_type == 75) ? 1 : (data_type == 76) ? 2 : (data_type == 77) ? 4 : 8;

        if (data_size != expected_size)
            return AVERROR(EINVAL);

        val = strtoull(str, &endptr, 10);
        if (endptr == str || *endptr != '\0')
            return AVERROR(EINVAL);

        switch (data_type) {
        case 75:
            if (val > UINT8_MAX)
                return AVERROR(ERANGE);
            AV_WB8(data, (uint8_t)val);
            break;
        case 76:
            if (val > UINT16_MAX)
                return AVERROR(ERANGE);
            AV_WB16(data, (uint16_t)val);
            break;
        case 77:
            if (val > UINT32_MAX)
                return AVERROR(ERANGE);
            AV_WB32(data, (uint32_t)val);
            break;
        case 78:
            AV_WB64(data, (uint64_t)val);
            break;
        }
        break;
    }
    default:
        return AVERROR(EINVAL);
    }

    return data_size;
}
