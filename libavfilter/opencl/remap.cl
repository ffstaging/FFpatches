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

const sampler_t linear_sampler = (CLK_NORMALIZED_COORDS_FALSE |
                                  CLK_FILTER_LINEAR);

const sampler_t nearest_sampler = (CLK_NORMALIZED_COORDS_FALSE |
                                   CLK_FILTER_NEAREST);

__kernel void remap_near(
    __write_only image2d_t dst,
    __read_only  image2d_t src,
    __read_only  image2d_t xmapi,
    __read_only  image2d_t ymapi,
    float4 fill_color,
    float4 scale,
    int4 swizzle)
{
    int2 p = (int2)(get_global_id(0), get_global_id(1));

    /* image dimensions */
    int2 src_dim = get_image_dim(src);
    int2 dst_dim = get_image_dim(dst);
    int2 map_dim = get_image_dim(xmapi);

    float2 src_dimf = (float2)(src_dim.x, src_dim.y);
    float2 dst_dimf = (float2)(dst_dim.x, dst_dim.y);
    float2 map_dimf = (float2)(map_dim.x, map_dim.y);

    /* compute map scaling to full-res */
    float2 map_scale = map_dimf / dst_dimf;

    /* scaled position to fetch from the maps */
    float2 map_p = (float2)(p.x, p.y) * map_scale;

    /* read mapping coordinates from full-res maps */
    float4 xmap = read_imagef(xmapi, nearest_sampler, map_p);
    float4 ymap = read_imagef(ymapi, nearest_sampler, map_p);
    float2 pos  = (float2)(xmap.x, ymap.x) * 65535.f;

    pos /= map_scale;

    /* check bounds */
    int2 mi = ((pos >= (float2)(0.f,0.f)) * (pos < src_dimf));
    float m = mi.x && mi.y;

    /* read source and apply swizzle + scale */
    float4 src_val = read_imagef(src, nearest_sampler, pos);

    float tmp[4];
    vstore4(src_val, 0, tmp);
    src_val = (float4)(tmp[swizzle.x] * scale.x,
                       tmp[swizzle.y] * scale.y,
                       tmp[swizzle.z] * scale.z,
                       tmp[swizzle.w] * scale.w);

    /* mix with fill color if out-of-bounds */
    float4 val = mix(fill_color, src_val, m);

    write_imagef(dst, p, val);
}

__kernel void remap_linear(
    __write_only image2d_t dst,
    __read_only  image2d_t src,
    __read_only  image2d_t xmapi,
    __read_only  image2d_t ymapi,
    float4 fill_color,
    float4 scale,
    int4 swizzle)
{
    int2 p = (int2)(get_global_id(0), get_global_id(1));

    /* image dimensions */
    int2 src_dim = get_image_dim(src);
    int2 dst_dim = get_image_dim(dst);
    int2 map_dim = get_image_dim(xmapi);

    float2 src_dimf = (float2)(src_dim.x, src_dim.y);
    float2 dst_dimf = (float2)(dst_dim.x, dst_dim.y);
    float2 map_dimf = (float2)(map_dim.x, map_dim.y);

    /* compute map scaling to full-res */
    float2 map_scale = map_dimf / dst_dimf;

    /* scaled position to fetch from the maps */
    float2 map_p = (float2)(p.x, p.y) * map_scale;

    /* read mapping coordinates from full-res maps */
    float4 xmap = read_imagef(xmapi, nearest_sampler, map_p);
    float4 ymap = read_imagef(ymapi, nearest_sampler, map_p);
    float2 pos  = (float2)(xmap.x, ymap.x) * 65535.f;

    pos /= map_scale;

    /* check bounds */
    int2 mi = ((pos >= (float2)(0.f,0.f)) * (pos < src_dimf));
    float m = mi.x && mi.y;

    /* read source and apply swizzle + scale */
    float4 src_val = read_imagef(src, linear_sampler, pos);

    float tmp[4];
    vstore4(src_val, 0, tmp);
    src_val = (float4)(tmp[swizzle.x] * scale.x,
                       tmp[swizzle.y] * scale.y,
                       tmp[swizzle.z] * scale.z,
                       tmp[swizzle.w] * scale.w);

    /* mix with fill color if out-of-bounds */
    float4 val = mix(fill_color, src_val, m);

    write_imagef(dst, p, val);
}
