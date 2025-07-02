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

#include "libavutil/buffer.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"

#include "nifilter.h"
#include "filters.h"
#include "formats.h"
#include "libavutil/mem.h"
#include "video.h"

typedef struct NetIntUploadContext {
    const AVClass *class;
    int device_idx;
    const char *device_name;
    AVBufferRef *hwdevice;
    AVBufferRef *hwframe;
    int keep_alive_timeout; /* keep alive timeout setting */
} NetIntUploadContext;

static int query_formats(AVFilterContext *ctx)
{
    NetIntUploadContext *nictx = ctx->priv;
    AVHWFramesConstraints *constraints = NULL;
    const enum AVPixelFormat *input_pix_fmts, *output_pix_fmts;
    AVFilterFormats *input_formats = NULL;
    int err, i;

    if (!nictx->hwdevice)
        return AVERROR(ENOMEM);

    constraints = av_hwdevice_get_hwframe_constraints(nictx->hwdevice, NULL);
    if (!constraints) {
        err = AVERROR(EINVAL);
        goto fail;
    }

    input_pix_fmts  = constraints->valid_sw_formats;
    output_pix_fmts = constraints->valid_hw_formats;

    input_formats = ff_make_format_list(output_pix_fmts);
    if (!input_formats) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    if (input_pix_fmts) {
        for (i = 0; input_pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
            err = ff_add_format(&input_formats, input_pix_fmts[i]);
            if (err < 0)
                goto fail;
        }
    }

    if ((err = ff_formats_ref(input_formats, &ctx->inputs[0]->outcfg.formats)) < 0 ||
        (err = ff_formats_ref(ff_make_format_list(output_pix_fmts),
                              &ctx->outputs[0]->incfg.formats)) < 0)
        goto fail;

    av_hwframe_constraints_free(&constraints);
    return 0;

fail:
    av_buffer_unref(&nictx->hwdevice);
    av_hwframe_constraints_free(&constraints);
    return err;
}

static av_cold int init(AVFilterContext *ctx)
{
    NetIntUploadContext *s = ctx->priv;
    char buf[64] = { 0 };

    snprintf(buf, sizeof(buf), "%d", s->device_idx);

    if (s->device_name) {
        int tmp_guid_id;
        tmp_guid_id = ni_rsrc_get_device_by_block_name(s->device_name, NI_DEVICE_TYPE_UPLOAD);
        if (tmp_guid_id != NI_RETCODE_FAILURE) {
            av_log(ctx, AV_LOG_VERBOSE,"User set uploader device_name=%s. This will replace uploader_device_id\n",s->device_name);
            memset(buf, 0, sizeof(buf));
            snprintf(buf, sizeof(buf), "%d", tmp_guid_id);
        }
        else {
            av_log(ctx, AV_LOG_VERBOSE, "Uploader device_name=%s not found. Use default value of uploader device_num=%d instead.\n",s->device_name,s->device_idx);
        }
    }

    return av_hwdevice_ctx_create(&s->hwdevice, AV_HWDEVICE_TYPE_NI_QUADRA, buf, NULL, 0);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    NetIntUploadContext *s = ctx->priv;

    av_buffer_unref(&s->hwframe);
    av_buffer_unref(&s->hwdevice);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    NetIntUploadContext *s = ctx->priv;
    AVNIFramesContext *pub_ctx;
    AVHWFramesContext *hwframe_ctx;
    int ret;

    av_buffer_unref(&s->hwframe);

    if (inlink->format == outlink->format) {
        // The input is already a hardware format, so we just want to
        // pass through the input frames in their own hardware context.
        FilterLink *li = ff_filter_link(inlink);
        if (!li->hw_frames_ctx) {
            av_log(ctx, AV_LOG_ERROR, "No input hwframe context.\n");
            return AVERROR(EINVAL);
        }
        FilterLink *lo = ff_filter_link(outlink);
        lo->hw_frames_ctx = av_buffer_ref(li->hw_frames_ctx);
        if (!lo->hw_frames_ctx)
            return AVERROR(ENOMEM);
        return 0;
    }

    s->hwframe = av_hwframe_ctx_alloc(s->hwdevice);
    if (!s->hwframe)
        return AVERROR(ENOMEM);

    hwframe_ctx            = (AVHWFramesContext*)s->hwframe->data;
    hwframe_ctx->format    = AV_PIX_FMT_NI_QUAD;
    hwframe_ctx->sw_format = inlink->format;
    hwframe_ctx->width     = inlink->w;
    hwframe_ctx->height    = inlink->h;
    pub_ctx = (AVNIFramesContext*)hwframe_ctx->hwctx;
    pub_ctx->keep_alive_timeout = s->keep_alive_timeout;
    FilterLink *li = ff_filter_link(inlink);
    pub_ctx->framerate     = li->frame_rate;

    ret = av_hwframe_ctx_init(s->hwframe);
    if (ret < 0)
        return ret;

    FilterLink *lo = ff_filter_link(outlink);
    lo->hw_frames_ctx = av_buffer_ref(s->hwframe);
    if (!lo->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext   *ctx = link->dst;
    AVFilterLink  *outlink = ctx->outputs[0];
    AVFrame *out = NULL;
    int ret;

    if (in->format == outlink->format)
        return ff_filter_frame(outlink, in);

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    out->width  = in->width;
    out->height = in->height;

    ret = av_hwframe_transfer_data(out, in, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error transferring data to the Quadra\n");
        goto fail;
    }

    ret = av_frame_copy_props(out, in);
    if (ret < 0)
        goto fail;

    av_frame_free(&in);

    return ff_filter_frame(ctx->outputs[0], out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink  *inlink = ctx->inputs[0];
    AVFilterLink  *outlink = ctx->outputs[0];
    AVFrame *frame = NULL;
    int ret = 0;
    FilterLink *lo = ff_filter_link(outlink);
    AVHWFramesContext *hwfc = (AVHWFramesContext *) lo->hw_frames_ctx->data;
    AVNIFramesContext *f_hwctx = (AVNIFramesContext*) hwfc->hwctx;

    // Forward the status on output link to input link, if the status is set, discard all queued frames
    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    av_log(ctx, AV_LOG_TRACE, "%s: ready %u inlink framequeue %u available_frame %d outlink framequeue %u frame_wanted %d\n",
        __func__, ctx->ready, ff_inlink_queued_frames(inlink), ff_inlink_check_available_frame(inlink), ff_inlink_queued_frames(outlink), ff_outlink_frame_wanted(outlink));

    if (ff_inlink_check_available_frame(inlink)) {
        if (inlink->format != outlink->format) {
            ret = ni_device_session_query_buffer_avail(&f_hwctx->api_ctx, NI_DEVICE_TYPE_UPLOAD);

            if (ret == NI_RETCODE_ERROR_UNSUPPORTED_FW_VERSION) {
                av_log(ctx, AV_LOG_WARNING, "No backpressure support in FW\n");
            } else if (ret < 0) {
                av_log(ctx, AV_LOG_WARNING, "%s: query ret %d, ready %u inlink framequeue %u available_frame %d outlink framequeue %u frame_wanted %d - return NOT READY\n",
                       __func__, ret, ctx->ready, ff_inlink_queued_frames(inlink), ff_inlink_check_available_frame(inlink), ff_inlink_queued_frames(outlink), ff_outlink_frame_wanted(outlink));
                return FFERROR_NOT_READY;
            }
        }

        ret = ff_inlink_consume_frame(inlink, &frame);
        if (ret < 0)
            return ret;

        ret = filter_frame(inlink, frame);
        if (ret >= 0) {
            ff_filter_set_ready(ctx, 300);
        }
        return ret;
    }

    // We did not get a frame from input link, check its status
    FF_FILTER_FORWARD_STATUS(inlink, outlink);

    // We have no frames yet from input link and no EOF, so request some.
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

#define OFFSET(x) offsetof(NetIntUploadContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
// default device_idx -1 for uploader to auto balance
static const AVOption ni_upload_options[] = {
    { "device",  "Number of the device to use", OFFSET(device_idx),  AV_OPT_TYPE_INT,    {.i64 = -1},  -1,        INT_MAX,  FLAGS},
    { "devname", "Name of the device to use",   OFFSET(device_name), AV_OPT_TYPE_STRING, {.str = NULL}, CHAR_MIN, CHAR_MAX, FLAGS},
    NI_FILT_OPTION_KEEPALIVE,
    { NULL }
};

AVFILTER_DEFINE_CLASS(ni_upload);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

FFFilter ff_vf_hwupload_ni_quadra = {
    .p.name         = "ni_quadra_hwupload",
    .p.description  = NULL_IF_CONFIG_SMALL(
        "NETINT Quadra upload a system memory frame to a device v" NI_XCODER_REVISION),
    .p.priv_class   = &ni_upload_class,
    .priv_size      = sizeof(NetIntUploadContext),
    .init           = init,
    .uninit         = uninit,
    .activate       = activate,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
};
