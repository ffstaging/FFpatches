/*
 * Copyright (c) 2026, Faeez Kadiri < f1k2faeez at gmail dot com>
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

#include "vf_tonemap_cuda.h"

#define ST2084_MAX_LUMINANCE 10000.0f
#define REFERENCE_WHITE      100.0f
#define ST2084_M1            0.1593017578125f
#define ST2084_M2            78.84375f
#define ST2084_C1            0.8359375f
#define ST2084_C2            18.8515625f
#define ST2084_C3            18.6875f
#define HLG_A                0.17883277f
#define HLG_B                0.28466892f
#define HLG_C                0.55991073f
#define SDR_AVG              0.25f

/* --- Transfer functions ------------------------------------------ */

__device__ static inline float eotf_st2084(float x)
{
    float p = __powf(x, 1.0f / ST2084_M2);
    float a = fmaxf(p - ST2084_C1, 0.0f);
    float b = fmaxf(ST2084_C2 - ST2084_C3 * p, 1e-6f);
    float c = __powf(a / b, 1.0f / ST2084_M1);
    return x > 0.0f ? c * (ST2084_MAX_LUMINANCE / REFERENCE_WHITE) : 0.0f;
}

__device__ static inline float inverse_oetf_hlg(float x)
{
    float a = 4.0f * x * x;
    float b = __expf((x - HLG_C) / HLG_A) + HLG_B;
    return x < 0.5f ? a : b;
}

__device__ static inline float inverse_eotf_bt1886(float c)
{
    return c < 0.0f ? 0.0f : __powf(c, 1.0f / 2.4f);
}

__device__ static inline float oetf_bt709(float c)
{
    c = fmaxf(c, 0.0f);
    float r1 = 4.5f * c;
    float r2 = 1.099f * __powf(c, 0.45f) - 0.099f;
    return c < 0.018f ? r1 : r2;
}

__device__ static inline float linearize(float x, int trc)
{
    if (trc == TRC_HLG_CUDA)
        return inverse_oetf_hlg(x);
    return eotf_st2084(x);
}

__device__ static inline float delinearize(float x, int trc)
{
    if (trc == DELIN_BT709_CUDA)
        return oetf_bt709(x);
    return inverse_eotf_bt1886(x);
}

/* --- OOTF (HLG) ------------------------------------------------- */

__device__ static inline void ootf_hlg(float *r, float *g, float *b,
                                        const float *luma_src, float peak)
{
    float luma = luma_src[0] * (*r) + luma_src[1] * (*g) + luma_src[2] * (*b);
    float gamma = 1.2f + 0.42f * __log10f(peak * REFERENCE_WHITE / 1000.0f);
    gamma = fmaxf(1.0f, gamma);
    float factor = peak * __powf(luma, gamma - 1.0f) / __powf(12.0f, gamma);
    *r *= factor;
    *g *= factor;
    *b *= factor;
}

/* --- Tonemap algorithms ------------------------------------------ */

__device__ static inline float hable_f(float in)
{
    float a = 0.15f, b = 0.50f, c = 0.10f;
    float d = 0.20f, e = 0.02f, f = 0.30f;
    float num = in * (in * a + b * c) + d * e;
    float den = in * (in * a + b) + d * f;
    return num / den - e / f;
}

__device__ static inline float apply_tonemap(float sig, float peak,
                                              int algo, float param)
{
    switch (algo) {
    case TONEMAP_LINEAR_CUDA:
        return sig * param / peak;
    case TONEMAP_GAMMA_CUDA: {
        float p = sig > 0.05f ? sig / peak : 0.05f / peak;
        float v = __powf(p, 1.0f / param);
        return sig > 0.05f ? v : (sig * v / 0.05f);
    }
    case TONEMAP_CLIP_CUDA:
        return fminf(fmaxf(sig * param, 0.0f), 1.0f);
    case TONEMAP_REINHARD_CUDA:
        return sig / (sig + param) * (peak + param) / peak;
    case TONEMAP_HABLE_CUDA:
        return hable_f(sig) / hable_f(peak);
    case TONEMAP_MOBIUS_CUDA: {
        float j = param;
        if (sig <= j)
            return sig;
        float a = -j * j * (peak - 1.0f) / (j * j - 2.0f * j + peak);
        float b = (j * j - 2.0f * j * peak + peak) / fmaxf(peak - 1.0f, 1e-6f);
        return (b * b + 2.0f * b * j + j * j) / (b - a) * (sig + a) / (sig + b);
    }
    default: /* TONEMAP_NONE_CUDA */
        return sig;
    }
}

/* --- Color helpers ----------------------------------------------- */

__device__ static inline void mat3x3_mul(const float *m,
                                          float r, float g, float b,
                                          float *or_, float *og, float *ob)
{
    *or_ = __fmaf_rn(m[0], r, __fmaf_rn(m[1], g, m[2] * b));
    *og  = __fmaf_rn(m[3], r, __fmaf_rn(m[4], g, m[5] * b));
    *ob  = __fmaf_rn(m[6], r, __fmaf_rn(m[7], g, m[8] * b));
}

__device__ static inline void yuv2rgb(float y, float u, float v,
                                       const CUDATonemapParams &p,
                                       float *r, float *g, float *b)
{
    if (p.src_range_full) {
        u -= 0.5f;
        v -= 0.5f;
    } else {
        y = (y * 255.0f -  16.0f) / 219.0f;
        u = (u * 255.0f - 128.0f) / 224.0f;
        v = (v * 255.0f - 128.0f) / 224.0f;
    }
    mat3x3_mul(p.rgb_matrix, y, u, v, r, g, b);
}

__device__ static inline void rgb2yuv(float r, float g, float b,
                                       const CUDATonemapParams &p,
                                       float *y, float *u, float *v)
{
    mat3x3_mul(p.yuv_matrix, r, g, b, y, u, v);
    if (p.dst_range_full) {
        *u += 0.5f;
        *v += 0.5f;
    } else {
        *y = (219.0f * (*y) + 16.0f) / 255.0f;
        *u = (224.0f * (*u) + 128.0f) / 255.0f;
        *v = (224.0f * (*v) + 128.0f) / 255.0f;
    }
}

__device__ static inline float rgb2y(float r, float g, float b,
                                      const CUDATonemapParams &p)
{
    float y = p.yuv_matrix[0] * r + p.yuv_matrix[1] * g + p.yuv_matrix[2] * b;
    if (p.dst_range_full)
        return y;
    return (219.0f * y + 16.0f) / 255.0f;
}

__device__ static inline void chroma_sample(float r0, float g0, float b0,
                                             float r1, float g1, float b1,
                                             float r2, float g2, float b2,
                                             float r3, float g3, float b3,
                                             int loc,
                                             float *cr, float *cg, float *cb)
{
    switch (loc) {
    case 1: /* AVCHROMA_LOC_LEFT */
        *cr = (r0 + r2) * 0.5f;
        *cg = (g0 + g2) * 0.5f;
        *cb = (b0 + b2) * 0.5f;
        break;
    case 3: /* AVCHROMA_LOC_TOPLEFT */
        *cr = r0; *cg = g0; *cb = b0;
        break;
    case 4: /* AVCHROMA_LOC_TOP */
        *cr = (r0 + r1) * 0.5f;
        *cg = (g0 + g1) * 0.5f;
        *cb = (b0 + b1) * 0.5f;
        break;
    case 5: /* AVCHROMA_LOC_BOTTOMLEFT */
        *cr = r2; *cg = g2; *cb = b2;
        break;
    case 6: /* AVCHROMA_LOC_BOTTOM */
        *cr = (r2 + r3) * 0.5f;
        *cg = (g2 + g3) * 0.5f;
        *cb = (b2 + b3) * 0.5f;
        break;
    default: /* CENTER / UNSPECIFIED */
        *cr = (r0 + r1 + r2 + r3) * 0.25f;
        *cg = (g0 + g1 + g2 + g3) * 0.25f;
        *cb = (b0 + b1 + b2 + b3) * 0.25f;
        break;
    }
}

/* --- Per-pixel tonemap pipeline ---------------------------------- */

__device__ static inline void map_one_pixel_rgb(float *r, float *g, float *b,
                                                 const CUDATonemapParams &p,
                                                 float peak)
{
    float sig = fmaxf(fmaxf(*r, fmaxf(*g, *b)), 1e-6f);

    if (p.target_peak > 1.0f) {
        sig  *= 1.0f / p.target_peak;
        peak *= 1.0f / p.target_peak;
    }

    float sig_old = sig;

    if (p.desat_param > 0.0f) {
        float luma = p.luma_dst[0] * (*r) +
                     p.luma_dst[1] * (*g) +
                     p.luma_dst[2] * (*b);
        float coeff = fmaxf(sig - 0.18f, 1e-6f) / fmaxf(sig, 1e-6f);
        coeff = __powf(coeff, 10.0f / p.desat_param);
        *r = *r * (1.0f - coeff) + luma * coeff;
        *g = *g * (1.0f - coeff) + luma * coeff;
        *b = *b * (1.0f - coeff) + luma * coeff;
        sig = sig * (1.0f - coeff) + luma * coeff;
    }

    sig = apply_tonemap(sig, peak, p.tonemap_func, p.param);
    sig = fminf(sig, 1.0f);

    float ratio = sig / sig_old;
    *r *= ratio;
    *g *= ratio;
    *b *= ratio;
}

/* --- Quantization helpers ---------------------------------------- */

__device__ static inline float read_p010(const unsigned short *p,
                                         int idx)
{
    return (float)(__ldg(&p[idx]) >> 6) / 1023.0f;
}

__device__ static inline float saturate(float x)
{
    return fminf(fmaxf(x, 0.0f), 1.0f);
}

__device__ static inline unsigned short quant_p010(float v)
{
    return (unsigned short)(saturate(v) * 1023.0f + 0.5f) << 6;
}

__device__ static inline unsigned char quant_nv12(float v)
{
    return (unsigned char)(saturate(v) * 255.0f + 0.5f);
}

/* --- Main kernel ------------------------------------------------- */

extern "C"
__global__ void tonemap(CUDATonemapParams p)
{
    int xi = blockIdx.x * blockDim.x + threadIdx.x;
    int yi = blockIdx.y * blockDim.y + threadIdx.y;

    int x = 2 * xi;
    int y = 2 * yi;

    if (x + 1 >= p.width || y + 1 >= p.height)
        return;

    int src_pitch_y  = p.src_pitch / sizeof(unsigned short);
    int src_pitch_uv = p.src_pitch / sizeof(unsigned short);
    int dst_pitch_y, dst_pitch_uv;

    const unsigned short *src_y  = (const unsigned short *)p.src_y;
    const unsigned short *src_uv = (const unsigned short *)p.src_uv;

    /* Read 4 Y samples and 1 UV pair from P010 */
    float y0 = read_p010(src_y, y       * src_pitch_y + x);
    float y1 = read_p010(src_y, y       * src_pitch_y + x + 1);
    float y2 = read_p010(src_y, (y + 1) * src_pitch_y + x);
    float y3 = read_p010(src_y, (y + 1) * src_pitch_y + x + 1);

    float u_val = read_p010(src_uv, yi * src_pitch_uv + 2 * xi);
    float v_val = read_p010(src_uv, yi * src_pitch_uv + 2 * xi + 1);

    /* YUV to linear RGB for each of 4 pixels */
    float r0, g0, b0, r1, g1, b1, r2, g2, b2, r3, g3, b3;

    yuv2rgb(y0, u_val, v_val, p, &r0, &g0, &b0);
    yuv2rgb(y1, u_val, v_val, p, &r1, &g1, &b1);
    yuv2rgb(y2, u_val, v_val, p, &r2, &g2, &b2);
    yuv2rgb(y3, u_val, v_val, p, &r3, &g3, &b3);

    /* Linearize (EOTF) */
    r0 = linearize(r0, p.src_trc);
    g0 = linearize(g0, p.src_trc);
    b0 = linearize(b0, p.src_trc);
    r1 = linearize(r1, p.src_trc);
    g1 = linearize(g1, p.src_trc);
    b1 = linearize(b1, p.src_trc);
    r2 = linearize(r2, p.src_trc);
    g2 = linearize(g2, p.src_trc);
    b2 = linearize(b2, p.src_trc);
    r3 = linearize(r3, p.src_trc);
    g3 = linearize(g3, p.src_trc);
    b3 = linearize(b3, p.src_trc);

    /* OOTF (HLG only) */
    if (p.src_trc == TRC_HLG_CUDA) {
        ootf_hlg(&r0, &g0, &b0, p.luma_src, p.signal_peak);
        ootf_hlg(&r1, &g1, &b1, p.luma_src, p.signal_peak);
        ootf_hlg(&r2, &g2, &b2, p.luma_src, p.signal_peak);
        ootf_hlg(&r3, &g3, &b3, p.luma_src, p.signal_peak);
    }

    /* Primaries conversion (e.g. BT.2020 to BT.709) */
    if (!p.rgb2rgb_passthrough) {
        float tr, tg, tb;
        mat3x3_mul(p.rgb2rgb_matrix, r0, g0, b0,
                   &tr, &tg, &tb);
        r0 = tr; g0 = tg; b0 = tb;
        mat3x3_mul(p.rgb2rgb_matrix, r1, g1, b1,
                   &tr, &tg, &tb);
        r1 = tr; g1 = tg; b1 = tb;
        mat3x3_mul(p.rgb2rgb_matrix, r2, g2, b2,
                   &tr, &tg, &tb);
        r2 = tr; g2 = tg; b2 = tb;
        mat3x3_mul(p.rgb2rgb_matrix, r3, g3, b3,
                   &tr, &tg, &tb);
        r3 = tr; g3 = tg; b3 = tb;
    }

    /* Tonemap each pixel */
    float peak = p.signal_peak;
    map_one_pixel_rgb(&r0, &g0, &b0, p, peak);
    map_one_pixel_rgb(&r1, &g1, &b1, p, peak);
    map_one_pixel_rgb(&r2, &g2, &b2, p, peak);
    map_one_pixel_rgb(&r3, &g3, &b3, p, peak);

    /* Delinearize */
    r0 = delinearize(r0, p.dst_trc);
    g0 = delinearize(g0, p.dst_trc);
    b0 = delinearize(b0, p.dst_trc);
    r1 = delinearize(r1, p.dst_trc);
    g1 = delinearize(g1, p.dst_trc);
    b1 = delinearize(b1, p.dst_trc);
    r2 = delinearize(r2, p.dst_trc);
    g2 = delinearize(g2, p.dst_trc);
    b2 = delinearize(b2, p.dst_trc);
    r3 = delinearize(r3, p.dst_trc);
    g3 = delinearize(g3, p.dst_trc);
    b3 = delinearize(b3, p.dst_trc);

    /* Compute output luma (Y) for each pixel */
    float out_y0 = rgb2y(r0, g0, b0, p);
    float out_y1 = rgb2y(r1, g1, b1, p);
    float out_y2 = rgb2y(r2, g2, b2, p);
    float out_y3 = rgb2y(r3, g3, b3, p);

    /* Compute chroma from the 4 delinearized RGB values */
    float cr, cg, cb;
    chroma_sample(r0, g0, b0, r1, g1, b1, r2, g2, b2, r3, g3, b3,
                  p.chroma_loc, &cr, &cg, &cb);
    float out_u, out_v, dummy_y;
    rgb2yuv(cr, cg, cb, p, &dummy_y, &out_u, &out_v);

    /* Write output */
    if (p.out_depth == 10) {
        unsigned short *dy = (unsigned short *)p.dst_y;
        unsigned short *duv = (unsigned short *)p.dst_uv;
        dst_pitch_y  = p.dst_pitch / sizeof(unsigned short);
        dst_pitch_uv = dst_pitch_y;

        dy[y       * dst_pitch_y + x]     = quant_p010(out_y0);
        dy[y       * dst_pitch_y + x + 1] = quant_p010(out_y1);
        dy[(y + 1) * dst_pitch_y + x]     = quant_p010(out_y2);
        dy[(y + 1) * dst_pitch_y + x + 1] = quant_p010(out_y3);

        duv[yi * dst_pitch_uv + 2 * xi]     = quant_p010(out_u);
        duv[yi * dst_pitch_uv + 2 * xi + 1] = quant_p010(out_v);
    } else {
        unsigned char *dy = (unsigned char *)p.dst_y;
        unsigned char *duv = (unsigned char *)p.dst_uv;
        dst_pitch_y  = p.dst_pitch;
        dst_pitch_uv = dst_pitch_y;

        dy[y       * dst_pitch_y + x]     = quant_nv12(out_y0);
        dy[y       * dst_pitch_y + x + 1] = quant_nv12(out_y1);
        dy[(y + 1) * dst_pitch_y + x]     = quant_nv12(out_y2);
        dy[(y + 1) * dst_pitch_y + x + 1] = quant_nv12(out_y3);

        duv[yi * dst_pitch_uv + 2 * xi]     = quant_nv12(out_u);
        duv[yi * dst_pitch_uv + 2 * xi + 1] = quant_nv12(out_v);
    }
}
