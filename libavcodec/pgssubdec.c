/*
 * PGS subtitle decoder
 * Copyright (c) 2009 Stephen Backway
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

/**
 * @file
 * PGS subtitle decoder
 */

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "mathops.h"

#include "libavutil/colorspace.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#define RGBA(r,g,b,a) (((unsigned)(a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define MAX_EPOCH_PALETTES 8    // Max 8 allowed per PGS epoch
#define MAX_EPOCH_OBJECTS  64   // Max 64 allowed per PGS epoch
#define MAX_OBJECT_REFS    2    // Max objects per display set
#define MAX_OBJECT_WH      4096 // Max object width/height


enum SegmentType {
    PALETTE_SEGMENT      = 0x14,
    OBJECT_SEGMENT       = 0x15,
    PRESENTATION_SEGMENT = 0x16,
    WINDOW_SEGMENT       = 0x17,
    DISPLAY_SEGMENT      = 0x80,
};

typedef struct PGSSubObjectRef {
    uint16_t id;
    uint8_t  window_id;
    uint8_t  composition_flag;
    uint16_t x;
    uint16_t y;
    uint16_t crop_x;
    uint16_t crop_y;
    uint16_t crop_w;
    uint16_t crop_h;
} PGSSubObjectRef;

typedef struct PGSSubPresentation {
    uint8_t         palette_flag;
    uint8_t         palette_id;
    uint8_t         object_count;
    PGSSubObjectRef objects[MAX_OBJECT_REFS];
    int64_t         pts;
} PGSSubPresentation;

typedef struct PGSSubObject {
    uint16_t  id;
    uint16_t  w;
    uint16_t  h;
    uint8_t   *rle;
    uint8_t   *bitmap;
    uint32_t  rle_buffer_size;
    uint32_t  rle_data_len;
    uint32_t  rle_remaining_len;
    uint32_t  bitmap_buffer_size;
    uint32_t  bitmap_size;
} PGSSubObject;

typedef struct PGSSubObjects {
    uint8_t      count;
    PGSSubObject object[MAX_EPOCH_OBJECTS];
} PGSSubObjects;

typedef struct PGSSubPalette {
    uint8_t     id;
    uint32_t    clut[AVPALETTE_COUNT];
} PGSSubPalette;

typedef struct PGSSubPalettes {
    uint8_t       count;
    PGSSubPalette palette[MAX_EPOCH_PALETTES];
} PGSSubPalettes;

typedef struct PGSGraphicPlane {
   uint8_t        count;
   uint8_t        writable;
   AVSubtitleRect visible_rect[MAX_OBJECT_REFS];
} PGSGraphicPlane;

typedef struct PGSSubContext {
    AVClass *class;
    PGSSubPresentation presentation;
    PGSSubPalettes     palettes;
    PGSSubObjects      objects;
    PGSGraphicPlane    plane;
    int forced_subs_only;
} PGSSubContext;

static void clear_graphic_plane(PGSSubContext *ctx)
{
    int i;

    for (i = 0; i < ctx->plane.count; i++) {
       av_freep(&ctx->plane.visible_rect[i].data[0]);
       memset(&ctx->plane.visible_rect[i], 0, sizeof(ctx->plane.visible_rect[i]));
    }
    ctx->plane.writable = 0;
    ctx->plane.count = 0;
}

static void flush_cache(AVCodecContext *avctx)
{
    PGSSubContext *ctx = avctx->priv_data;
    int i;

    for (i = 0; i < ctx->objects.count; i++) {
        av_freep(&ctx->objects.object[i].rle);
        ctx->objects.object[i].rle_buffer_size    = 0;
        ctx->objects.object[i].rle_remaining_len  = 0;
        av_freep(&ctx->objects.object[i].bitmap);
        ctx->objects.object[i].bitmap_buffer_size = 0;
        ctx->objects.object[i].bitmap_size        = 0;
    }
    ctx->objects.count = 0;
    ctx->palettes.count = 0;
}

static PGSSubObject * find_object(int id, PGSSubObjects *objects)
{
    int i;

    for (i = 0; i < objects->count; i++) {
        if (objects->object[i].id == id)
            return &objects->object[i];
    }
    return NULL;
}

static PGSSubPalette * find_palette(int id, PGSSubPalettes *palettes)
{
    int i;

    for (i = 0; i < palettes->count; i++) {
        if (palettes->palette[i].id == id)
            return &palettes->palette[i];
    }
    return NULL;
}

static av_cold int init_decoder(AVCodecContext *avctx)
{
    avctx->pix_fmt     = AV_PIX_FMT_PAL8;

    return 0;
}

static av_cold int close_decoder(AVCodecContext *avctx)
{
    clear_graphic_plane((PGSSubContext *)avctx->priv_data);
    flush_cache(avctx);

    return 0;
}

/**
 * Decode the RLE data.
 *
 * The subtitle is stored as a Run Length Encoded image.
 *
 * @param avctx contains the current codec context
 * @param sub pointer to the processed subtitle data
 * @param buf pointer to the RLE data to process
 * @param buf_size size of the RLE data to process
 */
static int decode_object_rle(AVCodecContext *avctx, PGSSubObject *object)
{
    const uint8_t *rle_buf;
    const uint8_t *rle_end;
    int pixel_count, line_count;
    rle_buf = object->rle;
    rle_end = object->rle + object->rle_data_len;

    object->bitmap_size = object->w * object->h;
    av_fast_padded_malloc(&object->bitmap, &object->bitmap_buffer_size,
                          object->bitmap_size);

    if (!object->bitmap)
        return AVERROR(ENOMEM);

    pixel_count = 0;
    line_count  = 0;

    while (rle_buf < rle_end && line_count < object->h) {
        uint8_t flags, color;
        int run;

        color = bytestream_get_byte(&rle_buf);
        run   = 1;

        if (color == 0x00) {
            flags = bytestream_get_byte(&rle_buf);
            run   = flags & 0x3f;
            if (flags & 0x40)
                run = (run << 8) + bytestream_get_byte(&rle_buf);
            color = flags & 0x80 ? bytestream_get_byte(&rle_buf) : 0;
        }

        if (run > 0 && pixel_count + run <= object->w * object->h) {
            memset(object->bitmap + pixel_count, color, run);
            pixel_count += run;
        } else if (!run) {
            /*
             * New Line. Check if correct pixels decoded, if not display warning
             * and adjust bitmap pointer to correct new line position.
             */
            if (pixel_count % object->w > 0) {
                av_log(avctx, AV_LOG_ERROR,
                       "Decoded %d pixels, when object line should be %d pixels\n",
                       pixel_count % object->w, object->w);
                if (avctx->err_recognition & AV_EF_EXPLODE) {
                    return AVERROR_INVALIDDATA;
                }
            }
            line_count++;
        }
    }

    if (pixel_count < object->w * object->h) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient RLE data for object\n");
        return AVERROR_INVALIDDATA;
    }
    ff_dlog(avctx, "Pixel Count = %d, Area = %d\n", pixel_count, object->w * object->h);
    return 0;
}

/**
 * Parse the picture segment packet.
 *
 * The picture segment contains details on the sequence id,
 * width, height and Run Length Encoded (RLE) bitmap data.
 *
 * @param avctx contains the current codec context
 * @param buf pointer to the packet to process
 * @param buf_size size of packet to process
 */
static int parse_object_segment(AVCodecContext *avctx,
                                  const uint8_t *buf, int buf_size)
{
    PGSSubContext *ctx = avctx->priv_data;
    PGSSubObject *object;

    uint8_t sequence_desc;
    unsigned int rle_bitmap_len, width, height;
    int id, ret;

    if (buf_size <= 4)
        return AVERROR_INVALIDDATA;
    buf_size -= 4;

    id = bytestream_get_be16(&buf);
    object = find_object(id, &ctx->objects);
    if (!object) {
        if (ctx->objects.count >= MAX_EPOCH_OBJECTS) {
            av_log(avctx, AV_LOG_ERROR, "Too many objects in epoch\n");
            return AVERROR_INVALIDDATA;
        }
        object = &ctx->objects.object[ctx->objects.count++];
        object->id = id;
    }

    /* skip object version number */
    buf += 1;

    /* Read the Sequence Description to determine if start of RLE data or appended to previous RLE */
    sequence_desc = bytestream_get_byte(&buf);

    /* First in sequence object definition segment */
    if (sequence_desc & 0x80) {
        if (buf_size <= 7)
            return AVERROR_INVALIDDATA;
        buf_size -= 7;

        /* Decode rle bitmap length, stored size includes width/height data */
        rle_bitmap_len = bytestream_get_be24(&buf) - 2*2;

        if (buf_size > rle_bitmap_len) {
            av_log(avctx, AV_LOG_ERROR,
                   "Buffer dimension %d larger than the expected RLE data %d\n",
                   buf_size, rle_bitmap_len);
            return AVERROR_INVALIDDATA;
        }

        /* Get bitmap dimensions from data */
        width  = bytestream_get_be16(&buf);
        height = bytestream_get_be16(&buf);

        /* Make sure the bitmap is not too large */
        if (MAX_OBJECT_WH < width || MAX_OBJECT_WH < height || !width || !height) {
            av_log(avctx, AV_LOG_ERROR, "Bitmap dimensions (%dx%d) invalid.\n", width, height);
            return AVERROR_INVALIDDATA;
        }

        object->rle_data_len = 0;
        object->w = width;
        object->h = height;
        /* Dimensions against video are checked at decode after cropping. */
        av_fast_padded_malloc(&object->rle, &object->rle_buffer_size, rle_bitmap_len);

        if (!object->rle) {
            object->rle_remaining_len = 0;
            return AVERROR(ENOMEM);
        }

        memcpy(object->rle, buf, buf_size);
        object->rle_remaining_len = rle_bitmap_len;
    } else {
        /* Additional RLE data */
        if (buf_size > object->rle_remaining_len)
            return AVERROR_INVALIDDATA;

        memcpy(object->rle + object->rle_data_len, buf, buf_size);
    }
    object->rle_data_len += buf_size;
    object->rle_remaining_len -= buf_size;

    /* Last in sequence object definition (can be both first and last) */
    if (sequence_desc & 0x40) {
        /* Attempt decoding if data is valid */
        if (0 == object->rle_remaining_len) {
            ret = decode_object_rle(avctx, object);
            if (ret < 0 && (avctx->err_recognition & AV_EF_EXPLODE || ret == AVERROR(ENOMEM))) {
                return ret;
            }
        } else {
            av_log(avctx, AV_LOG_ERROR,
                "RLE data length %u is %u bytes shorter than expected\n",
                object->rle_data_len, object->rle_remaining_len);
            if (avctx->err_recognition & AV_EF_EXPLODE)
                return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}

/**
 * Parse the palette segment packet.
 *
 * The palette segment contains details of the palette,
 * a maximum of 256 colors (AVPALETTE_COUNT) can be defined.
 *
 * @param avctx contains the current codec context
 * @param buf pointer to the packet to process
 * @param buf_size size of packet to process
 */
static int parse_palette_segment(AVCodecContext *avctx,
                                  const uint8_t *buf, int buf_size)
{
    PGSSubContext *ctx = avctx->priv_data;
    PGSSubPalette *palette;

    const uint8_t *buf_end = buf + buf_size;
    const uint8_t *cm      = ff_crop_tab + MAX_NEG_CROP;
    int color_id;
    int y, cb, cr, alpha;
    int r, g, b, r_add, g_add, b_add;
    int id;

    id  = bytestream_get_byte(&buf);
    palette = find_palette(id, &ctx->palettes);
    if (!palette) {
        if (ctx->palettes.count >= MAX_EPOCH_PALETTES) {
            av_log(avctx, AV_LOG_ERROR, "Too many palettes in epoch\n");
            return AVERROR_INVALIDDATA;
        }
        palette = &ctx->palettes.palette[ctx->palettes.count++];
        palette->id  = id;
    }

    /* Skip palette version */
    buf += 1;

    while (buf < buf_end) {
        color_id  = bytestream_get_byte(&buf);
        y         = bytestream_get_byte(&buf);
        cr        = bytestream_get_byte(&buf);
        cb        = bytestream_get_byte(&buf);
        alpha     = bytestream_get_byte(&buf);

        /* Default to BT.709 colorspace. In case of <= 576 height use BT.601 */
        if (avctx->height <= 0 || avctx->height > 576) {
            YUV_TO_RGB1_CCIR_BT709(cb, cr);
        } else {
            YUV_TO_RGB1_CCIR(cb, cr);
        }

        YUV_TO_RGB2_CCIR(r, g, b, y);

        ff_dlog(avctx, "Color %d := (%d,%d,%d,%d)\n", color_id, r, g, b, alpha);

        /* Store color in palette */
        palette->clut[color_id] = RGBA(r,g,b,alpha);
    }
    return 0;
}

/**
 * Parse the presentation segment packet.
 *
 * The presentation segment contains details on the video
 * width, video height, x & y subtitle position.
 *
 * @param avctx contains the current codec context
 * @param buf pointer to the packet to process
 * @param buf_size size of packet to process
 * @todo TODO: Implement cropping
 */
static int parse_presentation_segment(AVCodecContext *avctx,
                                      const uint8_t *buf, int buf_size,
                                      int64_t pts)
{
    PGSSubContext *ctx = avctx->priv_data;
    int ret;
    uint8_t i, state;
    const uint8_t *buf_end = buf + buf_size;

    // Video descriptor
    int w = bytestream_get_be16(&buf);
    int h = bytestream_get_be16(&buf);

    // On a new display set, reset writability of the graphic plane
    ctx->plane.writable = 0;

    ctx->presentation.pts = pts;

    ff_dlog(avctx, "Video Dimensions %dx%d\n",
            w, h);
    ret = ff_set_dimensions(avctx, w, h);
    if (ret < 0)
        return ret;

    /* Skip 3 bytes: framerate (1), presentation id number (2) */
    buf+=3;

    /*
     * State is a 2 bit field that defines pgs epoch boundaries
     * 00 - Normal, previously defined objects and palettes are still valid
     * 01 - Acquisition point, previous objects and palettes can be released
     * 10 - Epoch start, previous objects and palettes can be released
     * 11 - Epoch continue, previous objects and palettes can be released
     *
     * Reserved 6 bits discarded
     */
    state = bytestream_get_byte(&buf) >> 6;
    if (state != 0) {
        /* Epoch start always wipes the graphic plane. Epoch continue does only if
         * playback is not seamless, which should not happen with a proper stream.
         */
        if (0b10 == state)
            clear_graphic_plane((PGSSubContext *)avctx->priv_data);
        flush_cache(avctx);
    }

    /* Reserved 7 bits discarded. */
    ctx->presentation.palette_flag = bytestream_get_byte(&buf) & 0x80;
    ctx->presentation.palette_id = bytestream_get_byte(&buf);

    /*
     * On palette update, don't parse the compositions references,
     * just evaluate the existing graphic plane with the new palette.
     */
    if (!ctx->presentation.palette_flag) {
        ctx->presentation.object_count = bytestream_get_byte(&buf);
        if (ctx->presentation.object_count > MAX_OBJECT_REFS) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid number of presentation objects %d\n",
                   ctx->presentation.object_count);
            ctx->presentation.object_count = 2;
            if (avctx->err_recognition & AV_EF_EXPLODE) {
                return AVERROR_INVALIDDATA;
            }
        }

        for (i = 0; i < ctx->presentation.object_count; i++) {
            PGSSubObjectRef *const object = &ctx->presentation.objects[i];

            if (buf_end - buf < 8) {
                av_log(avctx, AV_LOG_ERROR, "Insufficient space for object\n");
                ctx->presentation.object_count = i;
                return AVERROR_INVALIDDATA;
            }

            object->id               = bytestream_get_be16(&buf);
            object->window_id        = bytestream_get_byte(&buf);
            object->composition_flag = bytestream_get_byte(&buf);

            object->x = bytestream_get_be16(&buf);
            object->y = bytestream_get_be16(&buf);

            // If cropping
            if (object->composition_flag & 0x80) {
                object->crop_x = bytestream_get_be16(&buf);
                object->crop_y = bytestream_get_be16(&buf);
                object->crop_w = bytestream_get_be16(&buf);
                object->crop_h = bytestream_get_be16(&buf);
            }

            /* Placement is checked at decode after cropping. */
            ff_dlog(avctx, "Subtitle Placement x=%d, y=%d\n",
                    object->x, object->y);
        }
    }
    return 0;
}

/**
 * Parse the window segment packet.
 *
 * The window segment instructs the decoder to redraw the graphic plane
 * with the composition references provided in the presentation segment
 *
 * @param avctx contains the current codec context
 */
static int parse_window_segment(AVCodecContext *avctx, const uint8_t *buf,
                                int buf_size)
{
    PGSSubContext *ctx = (PGSSubContext *)avctx->priv_data;

    // 1 byte: number of windows defined
    if (bytestream_get_byte(&buf) > MAX_OBJECT_REFS) {
        av_log(avctx, AV_LOG_ERROR, "Too many windows defined.\n");
        return AVERROR_INVALIDDATA;
    }

    /* TODO: mask objects with windows when transfering to the graphic plane
     * Window Segment Structure
     *     {
     *       1 byte : window id,
     *       2 bytes: X position of window,
     *       2 bytes: Y position of window,
     *       2 bytes: Width of window,
     *       2 bytes: Height of window.
     *     }
     */
    // Flush the graphic plane, it will be redrawn.
    clear_graphic_plane(ctx);
    ctx->plane.writable = 1;
    ctx->plane.count = ctx->presentation.object_count;
    return 0;
}

/**
 * Parse the display segment packet.
 *
 * The display segment closes the display set. The inferred data is used
 * to decide if the display should be updated.
 *
 * @param avctx contains the current codec context
 * @param data pointer to the data pertaining the subtitle to display
 * @param buf pointer to the packet to process
 * @param buf_size size of packet to process
 */
static int display_end_segment(AVCodecContext *avctx, AVSubtitle *sub,
                               const uint8_t *buf, int buf_size)
{
    PGSSubContext *ctx = avctx->priv_data;
    int64_t pts;
    PGSSubPalette *palette;
    int i;

    pts = ctx->presentation.pts != AV_NOPTS_VALUE ? ctx->presentation.pts : sub->pts;
    memset(sub, 0, sizeof(*sub));
    sub->pts = pts;
    ctx->presentation.pts = AV_NOPTS_VALUE;
    // There is no explicit end time for PGS subtitles.  The end time
    // is defined by the start of the next sub which may contain no
    // objects (i.e. clears the previous sub)
    sub->end_display_time   = UINT32_MAX;

    // Object count is zero only on an epoch start with no WDS
    // or the last DS with a WDS had no presentation object.
    if (!ctx->plane.count) {
        return 1;
    }

    if (!ctx->presentation.palette_flag && !ctx->plane.writable) {
        // This display set does not perform a display update
        // E.g. it only defines new objects or palettes for future usage.
        return 0;
    }

    sub->rects = av_calloc(ctx->plane.count, sizeof(*sub->rects));
    if (!sub->rects)
        return AVERROR(ENOMEM);

    palette = find_palette(ctx->presentation.palette_id, &ctx->palettes);
    if (!palette) {
        // Missing palette.  Should only happen with damaged streams.
        av_log(avctx, AV_LOG_ERROR, "Invalid palette id %d\n",
               ctx->presentation.palette_id);
        avsubtitle_free(sub);
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < ctx->plane.count; i++) {
        const PGSSubObjectRef *sub_object = &ctx->presentation.objects[i];
        AVSubtitleRect *const gp_rect = &ctx->plane.visible_rect[i];
        AVSubtitleRect *rect;
        gp_rect->type = SUBTITLE_BITMAP;

        // Compose the graphic plane if a window segment has been provided
        if (ctx->plane.writable) {
            PGSSubObject *object;

            // Process bitmap
            object = find_object(sub_object->id, &ctx->objects);
            if (!object) {
                // Missing object.  Should only happen with damaged streams.
                av_log(avctx, AV_LOG_ERROR, "Invalid object id %d\n", sub_object->id);
                if (avctx->err_recognition & AV_EF_EXPLODE)
                    return AVERROR_INVALIDDATA;
                // Leaves rect empty with 0 width and height.
                continue;
            }
            if (sub_object->composition_flag & 0x40)
                gp_rect->flags |= AV_SUBTITLE_FLAG_FORCED;

            gp_rect->x    = sub_object->x;
            gp_rect->y    = sub_object->y;

            if (object->rle) {
                int out_of_picture = 0;
                gp_rect->w = object->w;
                gp_rect->h = object->h;

                gp_rect->linesize[0] = object->w;

                // Check for cropping.
                if (sub_object->composition_flag & 0x80) {
                    int out_of_object = 0;

                    if (object->w < sub_object->crop_x + sub_object->crop_w)
                        out_of_object = 1;
                    if (object->h < sub_object->crop_y + sub_object->crop_h)
                        out_of_object = 1;

                    if (out_of_object) {
                        av_log(avctx, AV_LOG_ERROR,
                               "Subtitle cropping values are out of object. "
                               "obj_w = %d, obj_h = %d, crop_x = %d, crop_y = %d, "
                               "crop_w = %d, crop_h = %d.\n",
                               object->w,
                               object->h,
                               sub_object->crop_x,
                               sub_object->crop_y,
                               sub_object->crop_w,
                               sub_object->crop_h);
                        if (avctx->err_recognition & AV_EF_EXPLODE)
                            return AVERROR_INVALIDDATA;
                    } else {
                        // Replace subtitle dimensions with cropping ones.
                        gp_rect->w = sub_object->crop_w;
                        gp_rect->h = sub_object->crop_h;
                        gp_rect->linesize[0] = sub_object->crop_w;
                    }
                }

                /* Make sure the subtitle is not out of picture. */
                if (avctx->width < gp_rect->x + gp_rect->w || !gp_rect->w)
                    out_of_picture = 1;
                if (avctx->height < gp_rect->y + gp_rect->h || !gp_rect->h)
                    out_of_picture = 1;
                if (out_of_picture) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Subtitle out of video bounds. "
                           "x = %d, y = %d, width = %d, height = %d.\n",
                           gp_rect->x, gp_rect->y, gp_rect->w, gp_rect->h);
                    if (avctx->err_recognition & AV_EF_EXPLODE)
                        return AVERROR_INVALIDDATA;
                    gp_rect->w = 0;
                    gp_rect->h = 0;
                    continue;
                }

                if (!object->bitmap_size || object->rle_remaining_len) {
                    gp_rect->w = 0;
                    gp_rect->h = 0;
                    continue;
                }

                gp_rect->data[0] = av_malloc_array(gp_rect->w, gp_rect->h);
                if (!gp_rect->data[0])
                    return AVERROR(ENOMEM);

                if (sub_object->composition_flag & 0x80) {
                    /* Copy cropped bitmap. */
                    int y;

                    for (y = 0; y < sub_object->crop_h; y++) {
                        memcpy(&gp_rect->data[0][y * sub_object->crop_w],
                               &object->bitmap[(sub_object->crop_y + y) *
                               object->w + sub_object->crop_x],
                               sub_object->crop_w);
                    }
                }
                else {
                    /* copy full object */
                    memcpy(gp_rect->data[0], object->bitmap, object->bitmap_size);
                }
            }
        }
        // Export graphic plane content with latest palette
        rect = av_memdup(gp_rect, sizeof(*gp_rect));
        if (!rect)
            return AVERROR(ENOMEM);

        sub->rects[sub->num_rects++] = rect;
        if (gp_rect->data[0]) {
            rect->data[0] = av_memdup(gp_rect->data[0], rect->w*rect->h);
            if (!rect->data[0])
                return AVERROR(ENOMEM);
        }

        // Allocate memory for colors
        rect->nb_colors = AVPALETTE_COUNT;
        rect->data[1]   = av_mallocz(AVPALETTE_SIZE);
        if (!rect->data[1])
            return AVERROR(ENOMEM);

        if (!ctx->forced_subs_only || ctx->presentation.objects[i].composition_flag & 0x40)
            memcpy(rect->data[1], palette->clut, rect->nb_colors * sizeof(uint32_t));
    }
    return 1;
}

static int decode(AVCodecContext *avctx, AVSubtitle *sub,
                  int *got_sub_ptr, const AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;

    const uint8_t *buf_end;
    uint8_t       segment_type;
    int           segment_length;
    int i, ret;

    ff_dlog(avctx, "PGS sub packet:\n");

    for (i = 0; i < buf_size; i++) {
        ff_dlog(avctx, "%02x ", buf[i]);
        if (i % 16 == 15)
            ff_dlog(avctx, "\n");
    }

    if (i & 15)
        ff_dlog(avctx, "\n");

    *got_sub_ptr = 0;

    /* Ensure that we have received at a least a segment code and segment length */
    if (buf_size < 3)
        return -1;

    buf_end = buf + buf_size;

    /* Step through buffer to identify segments */
    while (buf < buf_end) {
        segment_type   = bytestream_get_byte(&buf);
        segment_length = bytestream_get_be16(&buf);

        ff_dlog(avctx, "Segment Length %d, Segment Type %x\n", segment_length, segment_type);

        if (segment_type != DISPLAY_SEGMENT && segment_length > buf_end - buf)
            break;

        ret = 0;
        switch (segment_type) {
        case PALETTE_SEGMENT:
            ret = parse_palette_segment(avctx, buf, segment_length);
            break;
        case OBJECT_SEGMENT:
            ret = parse_object_segment(avctx, buf, segment_length);
            break;
        case PRESENTATION_SEGMENT:
            ret = parse_presentation_segment(avctx, buf, segment_length, sub->pts);
            break;
        case WINDOW_SEGMENT:
            ret = parse_window_segment(avctx, buf, segment_length);
            break;
        case DISPLAY_SEGMENT:
            if (*got_sub_ptr) {
                av_log(avctx, AV_LOG_ERROR, "Duplicate display segment\n");
                ret = AVERROR_INVALIDDATA;
                break;
            }
            ret = display_end_segment(avctx, sub, buf, segment_length);
            if (ret >= 0)
                *got_sub_ptr = ret;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unknown subtitle segment type 0x%x, length %d\n",
                   segment_type, segment_length);
            ret = AVERROR_INVALIDDATA;
            break;
        }
        if (ret < 0 && (ret == AVERROR(ENOMEM) ||
                        avctx->err_recognition & AV_EF_EXPLODE))
            return ret;

        buf += segment_length;
    }

    return buf_size;
}

#define OFFSET(x) offsetof(PGSSubContext, x)
#define SD AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    {"forced_subs_only", "Only show forced subtitles", OFFSET(forced_subs_only), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, SD},
    { NULL },
};

static const AVClass pgsdec_class = {
    .class_name = "PGS subtitle decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_pgssub_decoder = {
    .p.name         = "pgssub",
    CODEC_LONG_NAME("HDMV Presentation Graphic Stream subtitles"),
    .p.type         = AVMEDIA_TYPE_SUBTITLE,
    .p.id           = AV_CODEC_ID_HDMV_PGS_SUBTITLE,
    .priv_data_size = sizeof(PGSSubContext),
    .init           = init_decoder,
    .close          = close_decoder,
    FF_CODEC_DECODE_SUB_CB(decode),
    .p.priv_class   = &pgsdec_class,
};
