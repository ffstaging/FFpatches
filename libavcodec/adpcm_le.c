/*
 * Copyright (c) 2025 Peter Ross
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

#include "adpcm.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"

static int adpcm_sanyo_expand3(ADPCMChannelStatus *c, int bits)
{
    int sign, delta, add;

    sign = bits & 4;
    if (sign)
        delta = 4 - (bits & 3);
    else
        delta = bits;

    switch (delta) {
    case 0:
        add = 0;
        c->step = (3 * c->step) >> 2;
        break;
    case 1:
        add = c->step;
        c->step = (4 * c->step - (c->step >> 1)) >> 2;
        break;
    case 2:
        add = 2 * c->step;
        c->step = ((c->step >> 1) + add) >> 1;
        break;
    case 3:
        add = 4 * c->step - (c->step >> 1);
        c->step = 2 * c->step;
        break;
    case 4:
        add = (11 * c->step) >> 1;
        c->step = 3 * c->step;
        break;
    }

    if (sign)
        add = -add;

    c->predictor = av_clip_int16(c->predictor + add);
    c->step = av_clip(c->step, 1, 7281);
    return c->predictor;
}

static int adpcm_sanyo_expand4(ADPCMChannelStatus *c, int bits)
{
    int sign, delta, add;

    sign = bits & 8;
    if (sign)
        delta = 8 - (bits & 7);
    else
        delta = bits;

    switch (delta) {
    case 0:
        add = 0;
        c->step = (3 * c->step) >> 2;
        break;
    case 1:
        add = c->step;
        c->step = (3 * c->step) >> 2;
        break;
    case 2:
        add = 2 * c->step;
        break;
    case 3:
        add = 3 * c->step;
        break;
    case 4:
        add = 4 * c->step;
        break;
    case 5:
        add = (11 * c->step) >> 1;
        c->step += c->step >> 2;
        break;
    case 6:
        add = (15 * c->step) >> 1;
        c->step = 2 * c->step;
        break;
    case 7:
        if (sign)
            add = (19 * c->step) >> 1;
        else
            add = (21 * c->step) >> 1;
        c->step = (c->step >> 1) + 2 * c->step;
        break;
    case 8:
        add = (25 * c->step) >> 1;
        c->step = 5 * c->step;
        break;
    }

    if (sign)
        add = -add;

    c->predictor = av_clip_int16(c->predictor + add);
    c->step = av_clip(c->step, 1, 2621);
    return c->predictor;
}

static int adpcm_sanyo_expand5(ADPCMChannelStatus *c, int bits)
{
    int sign, delta, add;

    sign = bits & 0x10;
    if (sign)
        delta = 16 - (bits & 0xF);
    else
        delta = bits;

    add = delta * c->step;
    switch (delta) {
    case 0:
        c->step += (c->step >> 2) - (c->step >> 1);
        break;
    case 1:
    case 2:
    case 3:
        c->step += (c->step >> 3) - (c->step >> 2);
        break;
    case 4:
    case 5:
        c->step += (c->step >> 4) - (c->step >> 3);
        break;
    case 6:
        break;
    case 7:
        c->step += c->step >> 3;
        break;
    case 8:
        c->step += c->step >> 2;
        break;
    case 9:
        c->step += c->step >> 1;
        break;
    case 10:
        c->step = 2 * c->step - (c->step >> 3);
        break;
    case 11:
        c->step = 2 * c->step + (c->step >> 3);
        break;
    case 12:
        c->step = 2 * c->step + (c->step >> 1) - (c->step >> 3);
        break;
    case 13:
        c->step = 3 * c->step - (c->step >> 2);
        break;
    case 14:
        c->step *= 3;
        break;
    case 15:
    case 16:
        c->step = (7 * c->step) >> 1;
        break;
    }

    if (sign)
        add = -add;

    c->predictor = av_clip_int16(c->predictor + add);
    c->step = av_clip(c->step, 1, 1024);
    return c->predictor;
}

int ff_adpcm_sanyo_decode(ADPCMChannelStatus *cs, const uint8_t *data, int data_size, int bits_per_coded_sample, int nb_samples, int channels, int16_t **samples_p)
{
    int (*expand)(ADPCMChannelStatus *c, int bits);
    GetBitContext gb;

    switch(bits_per_coded_sample) {
    case 3: expand = adpcm_sanyo_expand3; break;
    case 4: expand = adpcm_sanyo_expand4; break;
    case 5: expand = adpcm_sanyo_expand5; break;
    }

    init_get_bits8(&gb, data, data_size);
    for (int i = 0; i < nb_samples; i++)
        for (int ch = 0; ch < channels; ch++)
            samples_p[ch][i] = expand(&cs[ch], get_bits(&gb, bits_per_coded_sample));

    align_get_bits(&gb);
    return get_bits_count(&gb) / 8;
}
