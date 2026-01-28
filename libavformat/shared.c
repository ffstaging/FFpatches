/*
 * Shared file cache protocol.
 * Copyright (c) 2026 Niklas Haas
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
 *
 * Based on cache.c by Michael Niedermayer
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/file_open.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "url.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define HEADER_MAGIC   MKTAG('F', 'S', 'H', '$')
#define HEADER_VERSION 1

typedef struct Spacemap {
    atomic_uint header_magic;
    atomic_ushort version;
    atomic_ushort block_shift;
    atomic_intptr_t filesize; /* byte offset of true EOF, or 0 if unknown */

    atomic_uchar blocks[];
} Spacemap;

/* Set to value iff the current value is unset (zero) */
#define DEF_SET_ONCE(ctype, atype)                                              \
    static int set_once_##atype(atomic_##atype *const ptr, const ctype value)   \
    {                                                                           \
        ctype prev = 0;                                                         \
        av_assert1(value != 0);                                                 \
        if (atomic_compare_exchange_strong_explicit(                            \
                ptr, &prev, value, memory_order_acquire, memory_order_relaxed)) \
            return 1;                                                           \
        else if (prev == value)                                                 \
            return 0;                                                           \
        else                                                                    \
            return AVERROR(EINVAL);                                             \
    }

DEF_SET_ONCE(unsigned int,   uint)
DEF_SET_ONCE(unsigned short, ushort)
DEF_SET_ONCE(intptr_t,       intptr_t)

typedef struct SharedContext {
    AVClass *class;
    URLContext *inner;
    int64_t inner_pos;

    /* options */
    char *filename;
    int block_shift;

    /* misc state */
    int64_t pos; ///< corrent logical position
    uint8_t *tmp_buf;
    int block_size;

    /* cache file */
    char *cache_path;
    int fd;

    /* space map */
    Spacemap *spacemap;
    char *map_path;
    off_t map_size;
    int mapfd;

    /* statistics */
    int64_t nb_hit;
    int64_t nb_miss;
} SharedContext;

static int shared_close(URLContext *h)
{
    SharedContext *s = h->priv_data;

    ffurl_close(s->inner);
    if (s->spacemap)
        munmap(s->spacemap, s->map_size);
    if (s->fd != -1)
        close(s->fd);
    if (s->mapfd != -1)
        close(s->mapfd);
    av_free(s->cache_path);
    av_free(s->map_path);
    av_free(s->tmp_buf);

    av_log(h, AV_LOG_DEBUG, "Cache statistics: %"PRId64" hits, %"PRId64" misses\n",
           s->nb_hit, s->nb_miss);
    return 0;
}

static int spacemap_init(URLContext *h);

static int shared_open(URLContext *h, const char *arg, int flags, AVDictionary **options)
{
    SharedContext *s = h->priv_data;
    int ret;

    if (!s->filename || !s->filename[0]) {
        av_log(h, AV_LOG_ERROR, "Missing path for shared cache!\n");
        return AVERROR(EINVAL);
    }

    s->fd = s->mapfd = -1; /* Set these early for shared_close() failure path */
    s->block_size = 1 << s->block_shift;
    s->tmp_buf = av_malloc(s->block_size);
    if (!s->tmp_buf) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    s->cache_path = av_asprintf("%s.cache",    s->filename);
    s->map_path   = av_asprintf("%s.spacemap", s->filename);
    if (!s->cache_path || !s->map_path) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    s->fd    = avpriv_open(s->cache_path, O_RDWR | O_CREAT, 0660);
    s->mapfd = avpriv_open(s->map_path,   O_RDWR | O_CREAT, 0660);
    if (!s->fd || !s->mapfd) {
        ret = AVERROR(errno);
        av_log(h, AV_LOG_ERROR, "Failed to open shared cache file: %s\n", av_err2str(ret));
        goto fail;
    }

    ret = spacemap_init(h);
    if (ret < 0)
        goto fail;

    /* Open underlying protocol */
    av_strstart(arg, "shared:", &arg);
    ret = ffurl_open_whitelist(&s->inner, arg, flags, &h->interrupt_callback,
                               options, h->protocol_whitelist, h->protocol_blacklist, h);

    if (ret < 0)
        goto fail;

    h->max_packet_size = s->block_size;
    h->min_packet_size = s->block_size;

fail:
    if (ret < 0)
        shared_close(h);
    return ret;
}

static int spacemap_grow(URLContext *h, int64_t block)
{
    SharedContext *s = h->priv_data;
    int64_t num_blocks = block + 1;
    size_t map_bytes = sizeof(Spacemap) + ((num_blocks + 7) >> 3);
    map_bytes = FFALIGN(map_bytes, (int64_t) s->block_size);
    if (map_bytes <= s->map_size)
        return 0;

    /* Lock the spacemap to ensure no other process is currently resizing it */
    int ret = flock(s->mapfd, LOCK_EX);
    if (ret < 0)
        goto fail;

    /* Get current size in case another process already grew the spacemap */
    struct stat st;
    ret = fstat(s->mapfd, &st);
    if (ret < 0)
        goto fail;

    if (map_bytes > st.st_size) {
        ret = ftruncate(s->mapfd, map_bytes);
        if (ret < 0)
            goto fail;
        st.st_size = map_bytes;
    }

    if (s->spacemap)
        munmap(s->spacemap, s->map_size);
    s->map_size = st.st_size;
    s->spacemap = mmap(NULL, s->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, s->mapfd, 0);
    if (!s->spacemap) {
        s->map_size = 0;
        goto fail;
    }

    flock(s->mapfd, LOCK_UN);

    /* Report new size after successful (re)map */
    num_blocks = ((s->map_size - sizeof(Spacemap)) << 3);
    av_log(h, AV_LOG_DEBUG, "Resized space map to %zu bytes, new capacity: "
           "%"PRId64" blocks = %zu MB\n", (size_t) s->map_size, num_blocks,
           (size_t) (num_blocks * s->block_size) >> 20);
    return 0;

fail:
    flock(s->mapfd, LOCK_UN);
    ret = AVERROR(errno);
    av_log(h, AV_LOG_ERROR, "Failed to resize space map: %s\n", av_err2str(ret));
    return ret;
}

static int spacemap_init(URLContext *h)
{
    SharedContext *s = h->priv_data;
    int ret;

    /* Grow the spacemap to at least hold one block; this ensures the header
     * space is allocated as well */
    ret = spacemap_grow(h, 0);
    if (ret < 0)
        return ret;

    if ((ret = set_once_uint(&s->spacemap->header_magic, HEADER_MAGIC)) < 0 ||
        (ret = set_once_ushort(&s->spacemap->version, HEADER_VERSION)) < 0 ||
        (ret = set_once_ushort(&s->spacemap->block_shift, s->block_shift)) < 0)
    {
        av_log(h, AV_LOG_ERROR, "Shared cache spacemap header mismatch!\n");
        av_log(h, AV_LOG_ERROR, "  Expected magic: 0x%X, version: %d, block_shift: %d\n",
               HEADER_MAGIC, HEADER_VERSION, s->block_shift);
        av_log(h, AV_LOG_ERROR, "  Got      magic: 0x%X, version: %d, block_shift: %d\n",
               atomic_load(&s->spacemap->header_magic),
               atomic_load(&s->spacemap->version),
               atomic_load(&s->spacemap->block_shift));
        return ret;
    }

    if (ret)
        av_log(h, AV_LOG_DEBUG, "Initialized new cache spacemap.\n");
    return 0;
}

static int64_t get_filesize(URLContext *h)
{
    SharedContext *s = h->priv_data;
    return atomic_load_explicit(&s->spacemap->filesize, memory_order_acquire);
}

static int set_filesize(URLContext *h, int64_t new_size)
{
    SharedContext *s = h->priv_data;
    int ret;

    ret = set_once_intptr_t(&s->spacemap->filesize, new_size);
    if (ret < 0) {
        av_log(h, AV_LOG_ERROR, "Cached file size mismatch, expected: "
                "%"PRId64", got: %"PRId64"!\n", new_size,
                atomic_load(&s->spacemap->filesize));
        return ret;
    }

    return 0;
}


static int64_t block_id(SharedContext *s, int64_t pos)
{
    return pos >> s->block_shift;
}

static int block_offset(SharedContext *s, int64_t pos)
{
    return pos & (s->block_size - 1);
}

static int block_is_cached(SharedContext *s, int64_t block)
{
    atomic_uchar *const ptr = &s->spacemap->blocks[block >> 3];
    return atomic_load_explicit(ptr, memory_order_acquire) & (1 << (block & 7));
}

static void block_mark_cached(SharedContext *s, int64_t block)
{
    atomic_uchar *const ptr = &s->spacemap->blocks[block >> 3];
    atomic_fetch_or_explicit(ptr, 1 << (block & 7), memory_order_release);
}

static int shared_read(URLContext *h, unsigned char *buf, int size)
{
    SharedContext *s = h->priv_data;
    int ret;

    if (size <= 0)
        return 0;

    int64_t filesize = get_filesize(h);
    if (filesize) { /* limit read request to true filesize if known */
        if (s->pos + size >= filesize)
            size = filesize - s->pos;
        if (size <= 0)
            return AVERROR_EOF;
    }

    const int64_t block = block_id(s, s->pos);
    const int64_t offset = block_offset(s, s->pos);
    ret = spacemap_grow(h, block);
    if (ret < 0)
        return ret;

    if (block_is_cached(s, block)) {
        const int64_t cached = s->block_size - offset;
        ret = pread(s->fd, buf, FFMIN(cached, size), s->pos);
        if (ret < 0) {
            ret = AVERROR(errno);
            av_log(h, AV_LOG_ERROR, "Failed to read from cache file: %s\n", av_err2str(ret));
            return ret;
        }

        s->nb_hit++;
        s->pos += ret;
        return ret;
    }

    /* Cache miss, fetch this block from underlying protocol */
    s->nb_miss++;

    int64_t block_pos = block * s->block_size;
    if (s->inner_pos != block_pos) {
        block_pos = ffurl_seek(s->inner, block_pos, SEEK_SET);
        if (block_pos < 0) {
            av_log(h, AV_LOG_ERROR, "Failed to seek underlying protocol: %s\n", av_err2str(ret));
            return ret;
        }

        s->inner_pos = block_pos;
    }

    /* Try and fetch the entire block; reuse the output buffer if possible */
    const int block_size = filesize ? FFMIN(filesize - block_pos, s->block_size) : s->block_size;
    uint8_t *const tmp = (size >= block_size && !offset) ? buf : s->tmp_buf;
    int bytes_read = 0;
    while (bytes_read < block_size) {
        ret = ffurl_read(s->inner, tmp, block_size - bytes_read);
        if (!ret || ret == AVERROR_EOF)
            break;
        else if (ret < 0)
            return ret;

        bytes_read += ret;
        s->inner_pos += ret;
    }

    if (bytes_read < block_size) {
        /* Learned location of true EOF, update filesize */
        ret = set_filesize(h, block_pos + bytes_read);
        if (ret < 0)
            return ret;
    }

    if (bytes_read > 0) {
        ret = pwrite(s->fd, tmp, bytes_read, block_pos);
        if (ret < 0) {
            ret = AVERROR(errno);
            av_log(h, AV_LOG_ERROR, "Failed to write to cache file: %s\n", av_err2str(ret));
            return ret;
        }
        block_mark_cached(s, block);
    } else {
        return AVERROR_EOF;
    }

    const int wanted = FFMIN(bytes_read - offset, size);
    av_assert0(wanted >= 0);
    if (tmp != buf)
        memcpy(buf, &s->tmp_buf[offset], wanted);
    s->pos += wanted;
    return wanted;
}

static int64_t shared_seek(URLContext *h, int64_t pos, int whence)
{
    SharedContext *s = h->priv_data;
    const int64_t filesize = get_filesize(h);

    if (whence == SEEK_SET) {
        return s->pos = pos;
    } else if (whence == SEEK_CUR) {
        return s->pos += pos;
    } else if (whence == SEEK_END) {
        if (filesize)
            return s->pos = filesize + pos;
        /* Defer to underlying protocol if filesize is unknown */
        int64_t res = ffurl_seek(s->inner, pos, whence);
        if (res < 0)
            return res;
        set_filesize(h, res - pos); /* Opportunistically update known filesize */
        return s->pos = s->inner_pos = res;
    } else if (whence == AVSEEK_SIZE) {
        if (filesize)
            return filesize;
        int64_t res = ffurl_seek(s->inner, pos, whence);
        if (res < 0)
            return res;
        set_filesize(h, res);
        return res;
    } else {
        return -1;
    }
}

static int shared_get_file_handle(URLContext *h)
{
    SharedContext *s = h->priv_data;
    return ffurl_get_file_handle(s->inner);
}

static int shared_get_short_seek(URLContext *h)
{
    SharedContext *s = h->priv_data;
    int ret = ffurl_get_short_seek(s->inner);
    if (ret < 0)
        return ret;
    return FFMAX(ret, s->block_size);
}

#define OFFSET(x) offsetof(SharedContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "shared_path",  "Set (base) path for shared file cache", OFFSET(filename), AV_OPT_TYPE_STRING, {.str = NULL}, .flags = D },
    { "block_shift",  "Set the base 2 logarithm of the block size", OFFSET(block_shift), AV_OPT_TYPE_INT, {.i64 = 15}, 9, 30, .flags = D },
    {0},
};

static const AVClass shared_context_class = {
    .class_name = "shared",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_shared_protocol = {
    .name                = "shared",
    .url_open2           = shared_open,
    .url_read            = shared_read,
    .url_seek            = shared_seek,
    .url_close           = shared_close,
    .url_get_file_handle = shared_get_file_handle,
    .url_get_short_seek  = shared_get_short_seek,
    .priv_data_size      = sizeof(SharedContext),
    .priv_data_class     = &shared_context_class,
};
