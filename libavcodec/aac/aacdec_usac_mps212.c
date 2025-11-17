/*
 * Copyright (c) 2025 Lynne <dev@lynne.ee>
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

#include "aacdec_tab.h"
#include "libavcodec/get_bits.h"
#include "libavutil/macros.h"

#include "aacdec_usac_mps212.h"

static int huff_dec_1D(GetBitContext *gb, const int16_t (*tab)[2])
{
    int16_t idx = 0;
    do {
        /* Overreads are not possible here, the array forms a closed set */
        idx = tab[idx][get_bits1(gb)];
    } while (idx > 0);
    return idx;
}

static int huff_dec_2D(GetBitContext *gb, const int16_t (*tab)[2], int16_t ret[2])
{
    int idx = huff_dec_1D(gb, tab);
    if (!idx) { /* Escape */
        ret[0] = 0;
        ret[1] = 1;
        return 1;
    }

    idx = -(idx + 1);
    ret[0] = idx >> 4;
    ret[1] = idx & 0xf;
    return 0;
}

static int huff_data_1d(GetBitContext *gb, int16_t *data, int data_bands,
                        enum MPSDataType data_type, int diff_freq, int p0_flag)
{
    const int16_t (*hcod_first_band)[2];
    const int16_t (*hcod1D)[2];
    switch (data_type) {
    case MPS_CLD:
        hcod_first_band = ff_aac_hcod_firstband_CLD;
        hcod1D = ff_aac_hcod1D_CLD[diff_freq];
        break;
    case MPS_ICC:
        hcod_first_band = ff_aac_hcod_firstband_ICC;
        hcod1D = ff_aac_hcod1D_ICC;
        break;
    case MPS_IPD:
        hcod_first_band = ff_aac_hcod_firstband_IPD;
        hcod1D = ff_aac_hcod1D_IPD[diff_freq];
        break;
    }

    if (p0_flag)
        data[0] = -(huff_dec_1D(gb, hcod_first_band) + 1);

    for (int off = diff_freq; off < data_bands; off++) {
        int16_t val = -(huff_dec_1D(gb, hcod1D) + 1);
        if (val && data_type != MPS_IPD)
            val = get_bits1(gb) ? -val : val;
        data[off] = val;
    }

    return 0;
}

static void symmetry_data(GetBitContext *gb, int16_t data[2],
                          uint8_t lav, enum MPSDataType data_type)
{
    int16_t sum = data[0] + data[1];
    int16_t diff = data[0] - data[1];

    if (sum > lav) {
        data[0] = -sum + (2*lav + 1);
        data[1] = -diff;
    } else {
        data[0] = sum;
        data[1] = diff;
    }

    if ((data_type != MPS_IPD) && (data[0] + data[1])) {
        int sym = get_bits1(gb) ? -1 : 1;
        data[0] *= sym;
        data[1] *= sym;
    }

    if (data[0] - data[1]) {
        if (get_bits1(gb))
            FFSWAP(int16_t, data[0], data[1]);
    }
}

static void pcm_decode(GetBitContext *gb, int16_t *data0, int16_t *data1,
                       int16_t offset, int nb_pcm_data_bands,
                       int nb_quant_steps, int nb_levels)
{
    int max_group_len;
    switch (nb_levels) {
    case  3: max_group_len = 5; break;
    case  7: max_group_len = 6; break;
    case 11: max_group_len = 2; break;
    case 13: max_group_len = 4; break;
    case 19: max_group_len = 4; break;
    case 25: max_group_len = 3; break;
    case 51: max_group_len = 4; break;
    case  4: case  8: case 15: case 16: case 26: case 31:
        max_group_len = 1;
        break;
    default:
        return;
    };

    for (int i = 0; i < nb_pcm_data_bands; i+= max_group_len) {
        int group_len = FFMIN(max_group_len, nb_pcm_data_bands - i);
        int nb_bits = ceilf(group_len*log2f(nb_quant_steps));
        int pcm = get_bits(gb, nb_bits);
        for (int j = 0; j < group_len; j++) {
            int idx = i + (group_len - 1) - j;
            int val = pcm % nb_quant_steps;
            if (data0 && data1) {
                if (idx % 2)
                    data1[idx / 2] = val - offset;
                else
                    data0[idx / 2] = val - offset;
            } else if (!data1) {
                data0[idx] = val - offset;
            } else if (!data0) {
                data1[idx] = val - offset;
            }
            pcm = (pcm - val) / nb_quant_steps;
        }
    }
}

static void huff_data_2d(GetBitContext *gb, int16_t *part0_data[2], int16_t (*data)[2],
                         int data_bands, int stride, enum MPSDataType data_type,
                         int diff_freq, int freq_pair)
{
    int16_t lav_idx = huff_dec_1D(gb, ff_aac_hcod_lav_idx);
    uint8_t lav = ff_aac_lav_tab_XXX[data_type][-(lav_idx + 1)];

    const int16_t (*hcod1D)[2];
    const int16_t (*hcod2D)[2];
    switch (data_type) {
    case MPS_CLD:
        hcod1D = ff_aac_hcod1D_CLD[diff_freq];
        switch (lav) {
        case 3: hcod2D = ff_aac_hcod2D_CLD_03[freq_pair][diff_freq]; break;
        case 5: hcod2D = ff_aac_hcod2D_CLD_05[freq_pair][diff_freq]; break;
        case 7: hcod2D = ff_aac_hcod2D_CLD_07[freq_pair][diff_freq]; break;
        case 9: hcod2D = ff_aac_hcod2D_CLD_09[freq_pair][diff_freq]; break;
        }
        break;
    case MPS_ICC:
        hcod1D = ff_aac_hcod1D_ICC;
        switch (lav) {
        case 1: hcod2D = ff_aac_hcod2D_ICC_01[freq_pair][diff_freq]; break;
        case 3: hcod2D = ff_aac_hcod2D_ICC_03[freq_pair][diff_freq]; break;
        case 5: hcod2D = ff_aac_hcod2D_ICC_05[freq_pair][diff_freq]; break;
        case 7: hcod2D = ff_aac_hcod2D_ICC_07[freq_pair][diff_freq]; break;
        }
        break;
    case MPS_IPD:
        hcod1D = ff_aac_hcod1D_IPD[diff_freq];
        switch (lav) {
        case 1: hcod2D = ff_aac_hcod2D_IPD_01[freq_pair][diff_freq]; break;
        case 3: hcod2D = ff_aac_hcod2D_IPD_03[freq_pair][diff_freq]; break;
        case 5: hcod2D = ff_aac_hcod2D_IPD_05[freq_pair][diff_freq]; break;
        case 7: hcod2D = ff_aac_hcod2D_IPD_07[freq_pair][diff_freq]; break;
        }
        break;
    }

    if (part0_data[0])
        part0_data[0][0] = -(huff_dec_1D(gb, hcod1D) + 1);
    if (part0_data[1])
        part0_data[1][0] = -(huff_dec_1D(gb, hcod1D) + 1);

    int i = 0;
    int esc_cnt = 0;
    int16_t esc_data[2][28];
    int esc_idx[28];
    for (; i < data_bands; i += stride) {
        if (huff_dec_2D(gb, hcod2D, data[i]))
            esc_idx[esc_cnt++] = i; /* Escape */
        else
            symmetry_data(gb, data[i], lav, data_type);
    }

    if (esc_cnt) {
        pcm_decode(gb, esc_data[0], esc_data[1],
                   0, 2*esc_cnt, 0, (2*lav + 1));
        for (i = 0; i < esc_cnt; i++) {
            data[esc_idx[i]][0] = esc_data[0][i] - lav;
            data[esc_idx[i]][0] = esc_data[0][i] - lav;
        }
    }
}

static int huff_decode(GetBitContext *gb, int16_t *data[2],
                       enum MPSDataType data_type, int diff_freq[2],
                       int num_val, int *time_pair, int ldMode)
{
    int16_t pair_vec[28][2];
    int num_val_ch[2] = { num_val, num_val };
    int16_t *p0_data[2][2] = { 0 };
    int df_rest_flag[2] = { 0, 0 };

    /* Coding scheme */
    if (get_bits1(gb)) { /* 2D */
        *time_pair = 0;
        if (data[0] && data[1] && !ldMode)
            *time_pair = get_bits1(gb);

        if (time_pair) {
            if (diff_freq[0] || diff_freq[1]) {
                p0_data[0][0] = data[0];
                p0_data[0][1] = data[1];

                data[0] += 1;
                data[1] += 1;

                num_val_ch[0] -= 1;
            }

            int diff_mode = 1;
            if (!diff_freq[0] || !diff_freq[1])
                diff_mode = 0; // time

            huff_data_2d(gb, p0_data[0], pair_vec, num_val_ch[0], 1, data_type,
                         diff_mode, 0);

            for (int i = 0; i < num_val_ch[0]; i++) {
                data[0][i] = pair_vec[i][0];
                data[1][i] = pair_vec[i][1];
            }
        } else {
            if (data[0]) {
                if (diff_freq[0]) {
                    p0_data[0][0] = data[0];
                    p0_data[0][1] = NULL;

                    num_val_ch[0] -= 1;
                    data[0]++;
                }
                df_rest_flag[0] = num_val_ch[0] % 2;
                if (df_rest_flag[0])
                    num_val_ch[0] -= 1;
                if (num_val_ch[0] < 0)
                    return AVERROR(EINVAL);
            }

            if (data[1]) {
                if (diff_freq[1]) {
                    p0_data[1][0] = NULL;
                    p0_data[1][1] = data[1];

                    num_val_ch[1] -= 1;
                    data[1]++;
                }
                df_rest_flag[1] = num_val_ch[1] % 2;
                if (df_rest_flag[1])
                    num_val_ch[1] -= 1;
                if (num_val_ch[1] < 0)
                    return AVERROR(EINVAL);
            }

            if (data[0]) {
                huff_data_2d(gb, p0_data[0], pair_vec, num_val_ch[0], 2, data_type,
                             diff_freq[0], 1);
                if (df_rest_flag[0])
                    huff_data_1d(gb, data[0] + num_val_ch[0], 1,
                                 data_type, diff_freq[0], 0);
            }

            if (data[1]) {
                huff_data_2d(gb, p0_data[1], pair_vec + 1, num_val_ch[1], 2, data_type,
                             diff_freq[1], 1);
                if (df_rest_flag[1])
                    huff_data_1d(gb, data[1] + num_val_ch[1], 1,
                                 data_type, diff_freq[1], 0);
            }
        }
    } else { /* 1D */
        if (data[0])
            huff_data_1d(gb, data[0], num_val, data_type, diff_freq[0], diff_freq[0]);
        if (data[1])
            huff_data_1d(gb, data[1], num_val, data_type, diff_freq[1], diff_freq[0]);
    }

    return 0;
}

static void diff_freq_decode(const int16_t *diff, int16_t *out, int nb_val)
{
    int i = 0;
    out[0] = diff[0];
    for (i = 1; i < nb_val; i++)
        out[i] = out[i - 1] + diff[i];
}

static void diff_time_decode_backwards(const int16_t *prev, const int16_t *diff,
                                       int16_t *out, const int mixed_diff_type,
                                       const int nb_val)
{
    if (mixed_diff_type)
        out[0] = diff[0];
    for (int i = mixed_diff_type; i < nb_val; i++)
        out[i] = prev[i] + diff[i];
}

static void diff_time_decode_forwards(const int16_t *prev, const int16_t *diff,
                                      int16_t *out, const int mixed_diff_type,
                                      const int nb_val)
{
    if (mixed_diff_type)
        out[0] = diff[0];
    for (int i = mixed_diff_type; i < nb_val; i++)
        out[i] = prev[i] - diff[i];
}

static void attach_lsb(GetBitContext *gb, int16_t *data_msb,
                       int offset, int nb_lsb, int nb_val,
                       int16_t *data)
{
    for (int i = 0; i < nb_val; i++) {
        int msb = data_msb[i];
        if (nb_lsb > 0) {
            uint32_t lsb = get_bits(gb, nb_lsb);
            data[i] = ((msb << nb_lsb) | lsb) - offset;
        } else {
            data[i] = msb - offset;
        }
    }
}

int ff_aac_ec_pair_dec(GetBitContext *gb,
                       int *data[2], int16_t *last,
                       enum MPSDataType data_type, int start_band, int nb_bands,
                       int pair, int coarse,
                       int allowDiffTimeBack_flag)
{
    int attach_lsb_flag = 0;
    int quant_levels = 0;
    int quant_offset = 0;

    switch (data_type) {
    case MPS_CLD:
        if (coarse) {
            attach_lsb_flag = 0;
            quant_levels = 15;
            quant_offset = 7;
      } else {
            attach_lsb_flag = 0;
            quant_levels = 31;
            quant_offset = 15;
        }
        break;
    case MPS_ICC:
        if (coarse) {
            attach_lsb_flag = 0;
            quant_levels = 4;
            quant_offset = 0;
        } else {
            attach_lsb_flag = 0;
            quant_levels = 8;
            quant_offset = 0;
        }
        break;
    case MPS_IPD:
        if (!coarse) {
            attach_lsb_flag = 1;
            quant_levels = 16;
            quant_offset = 0;
        } else {
            attach_lsb_flag = 0;
            quant_levels = 8;
            quant_offset = 0;
        }
        break;
    }

    int16_t last_msb[28] = { 0 };
    int16_t data_pair[2][28] = { 0 };
    int16_t data_diff[2][28] = { 0 };
    int16_t *p_data[2];
    if (get_bits1(gb)) {
        int nb_pcm_vals;
        if (pair) {
            p_data[0] = data_pair[0];
            p_data[1] = data_pair[1];
            nb_pcm_vals = 2 * nb_bands;
        } else {
            p_data[0] = data_pair[0];
            p_data[1] = NULL;
            nb_pcm_vals = nb_bands;
        }

        int nb_quant_steps;
        switch (data_type) {
        case MPS_CLD: nb_quant_steps = coarse ? 15 : 31; break;
        case MPS_ICC: nb_quant_steps = coarse ?  4 :  8; break;
        case MPS_IPD: nb_quant_steps = coarse ?  8 : 16; break;
        }
        pcm_decode(gb, p_data[0], p_data[1], quant_offset, nb_pcm_vals,
                   nb_quant_steps, quant_levels);

        memcpy(data + start_band, data_pair[0], 2*nb_bands);
        if (pair)
            memcpy(data + start_band, data_pair[1], 2*nb_bands);

        return 0;
    }

    if (pair) {
        p_data[0] = data_pair[0];
        p_data[1] = data_pair[1];
    } else {
        p_data[0] = data_pair[0];
        p_data[1] = NULL;
    }

    int diff_freq[2] = { 1, 1 };
    int backwards = 1;

    if (pair || allowDiffTimeBack_flag)
        diff_freq[0] = !get_bits1(gb);

    if (pair && (diff_freq[0] || allowDiffTimeBack_flag))
        diff_freq[1] = !get_bits1(gb);

    int time_pair;
    huff_decode(gb, p_data, data_type, diff_freq,
                nb_bands, &time_pair, 0 /* 1 if SAOC */);

    /* Differential decoding */
    if (!diff_freq[0] || !diff_freq[1]) {
        if (0 /* 1 if SAOC */) {
            backwards = 1;
        } else {
            if (pair) {
            if (!diff_freq[0] && !allowDiffTimeBack_flag) {
                backwards = 0;
            } else if (!diff_freq[1]) {
                backwards = 1;
            } else {
                backwards = !get_bits1(gb);
            }
            } else {
                backwards = 1;
            }
        }
    }

    int mixed_time_pair = (diff_freq[0] != diff_freq[1]) && time_pair;

    if (backwards) {
        if (diff_freq[0]) {
            diff_freq_decode(data_diff[0], data_pair[0], nb_bands);
        } else {
            for (int i = 0; i < nb_bands; i++) {
                last_msb[i] = last[i + start_band] + quant_offset;
                if (attach_lsb_flag) {
                    last_msb[i] >>= 1;
                }
            }
            diff_time_decode_backwards(last_msb, data_diff[0], data_pair[0],
                                       mixed_time_pair, nb_bands);
        }

        if (diff_freq[1])
            diff_freq_decode(data_diff[1], data_pair[1], nb_bands);
        else
            diff_time_decode_backwards(data_pair[0], data_diff[1],
                                       data_pair[1], mixed_time_pair, nb_bands);
    } else {
        diff_freq_decode(data_diff[1], data_pair[1], nb_bands);

        if (diff_freq[0])
          diff_freq_decode(data_diff[0], data_pair[0], nb_bands);
        else
          diff_time_decode_forwards(data_pair[1], data_diff[0], data_pair[0],
                                    mixed_time_pair, nb_bands);
    }

    /* Decode LSBs */
    attach_lsb(gb, p_data[0], quant_offset, attach_lsb_flag,
               nb_bands, p_data[0]);
    if (pair)
        attach_lsb(gb, p_data[1], quant_offset, attach_lsb_flag,
                   nb_bands, p_data[1]);

    memcpy(data + start_band, data_pair[0], 2*nb_bands);
    if (pair)
        memcpy(data + start_band, data_pair[1], 2*nb_bands);

    return 0;
}

int ff_aac_huff_dec_reshape(GetBitContext *gb, int16_t *out_data,
                            int nb_val)
{
    int val, len;
    int val_received = 0;
    int16_t rl_data[2] = { 0 };

    while (val_received < nb_val) {
        huff_dec_2D(gb, ff_aac_hcod2D_reshape, rl_data);
        val = rl_data[0];
        len = rl_data[1] + 1;
        if (val_received + len > nb_val)
            return AVERROR(EINVAL);
        for (int i = val_received; i < val_received + len; i++)
            out_data[i] = val;
    }
    val_received += len;

    return 0;
}
