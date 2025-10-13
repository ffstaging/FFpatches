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

#include "bungee/Bungee.h"

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"

typedef struct BungeeContext
{
    const AVClass *class;

    const struct Functions *functions;

    void *stretcher;
    int debug;

    struct SampleRates sampleRates;
    int channelCount;

    float *inputBuffer;
    int inputBufferSampleCount;

    int64_t inputBufferBeginPosition;
    int64_t inputBufferEndPosition;
    int64_t inputStartPosition;
    int64_t inputFinishPosition;

    int64_t ptsOriginPosition;

    double semitones;

    struct Request request;
    struct InputChunk inputChunk;

    int64_t start_pts;
    int64_t last_pts;

    int64_t outputSampleCount;
    int eof; // end of input
} BungeeContext;

static int samplesNeeded(const BungeeContext *s)
{
    return s->inputChunk.end - s->inputBufferEndPosition;
}

static void appendToInputBuffer(BungeeContext *s, const AVFrame *in)
{
    int moveOffset = s->inputBufferBeginPosition - s->inputChunk.begin;
    int moveCount = s->inputBufferEndPosition - s->inputChunk.begin;

    av_assert1(!in || in->nb_samples <= samplesNeeded(s));

    if (moveCount < 0)
        moveCount = 0;

    for (int c = 0; c < s->channelCount; ++c)
        memmove(
            &s->inputBuffer[c * s->inputBufferSampleCount],
            &s->inputBuffer[c * s->inputBufferSampleCount - moveOffset],
            moveCount * sizeof(float));

    s->inputBufferBeginPosition = s->inputChunk.begin;

    if (in) {
        const float *const *source = (const float *const *)in->extended_data;

        for (int c = 0; c < s->channelCount; ++c)
            memcpy(
                &s->inputBuffer[moveCount + c * s->inputBufferSampleCount],
                source[c],
                in->nb_samples * sizeof(float));

        s->inputBufferEndPosition += in->nb_samples;
    }
}

#define BUNGEE_OPTIONS BUNGEE##_options


enum BungeeModeCounts {

#define X_BEGIN(Type, type) count##Type = 0
#define X_ITEM(Type, type, mode, description) + 1
#define X_END(Type, type) ,
BUNGEE_MODES
#undef X_BEGIN
#undef X_ITEM
#undef X_END

};


static const int flags = AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM;

static const AVOption BUNGEE_OPTIONS[] = {
    {"speed", "set speed as a tempo multiplier", offsetof(BungeeContext, request.speed), AV_OPT_TYPE_DOUBLE, {.dbl = 1}, 0.01, 100, flags},
    {"pitch", "set pitch as a semitone offset", offsetof(BungeeContext, semitones), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, -24, 24, flags},
    {"debug", "verbose debug checks", offsetof(BungeeContext, debug), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 0, AV_OPT_FLAG_AUDIO_PARAM},

#define X_BEGIN(Type, type) { #type, "set " #type " mode", offsetof(BungeeContext, request.type##Mode), AV_OPT_TYPE_INT, {.i64=0}, 0, count##Type - 1, flags, .unit = #type },
#define X_ITEM(Type, type, mode, description) {#mode, description, 0, AV_OPT_TYPE_CONST,  {.i64=type##Mode_##mode}, 0, 0, flags, .unit = #type },
#define X_END(Type, type)
BUNGEE_MODES
#undef X_BEGIN
#undef X_ITEM
#undef X_END

    {NULL},
};

AVFILTER_DEFINE_CLASS(BUNGEE);

static av_cold int init(AVFilterContext *ctx)
{
    BungeeContext *s = ctx->priv;
    s->functions = BUNGEE_GET_FUNCTIONS();
    if (!s->functions)
    {
        av_log(ctx, AV_LOG_ERROR, "No functions found for " BUNGEE_Name "\n");
        return AVERROR_INVALIDDATA;
    }
    s->stretcher = NULL;
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BungeeContext *s = ctx->priv;
    if (s->stretcher)
        s->functions->destroy(s->stretcher);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    BungeeContext *s = ctx->priv;
    s->sampleRates.input = inlink->sample_rate;
    s->sampleRates.output = inlink->sample_rate;
    s->channelCount = inlink->ch_layout.nb_channels;

    if (s->stretcher)
        s->functions->destroy(s->stretcher);
    s->stretcher = NULL;

    s->stretcher = s->functions->create(s->sampleRates, s->channelCount, 0);
    s->functions->enableInstrumentation(s->stretcher, s->debug);

    s->request.position = 0;
    s->request.pitch = pow(2., s->semitones * (1. / 12));
    s->functions->next(s->stretcher, &s->request);
    s->request.reset = 1;
    s->inputChunk = s->functions->specifyGrain(s->stretcher, &s->request, 0.);

    s->inputBufferSampleCount = s->inputChunk.end - s->inputChunk.begin;
    s->inputStartPosition = s->inputBufferSampleCount / 2;
    s->inputBufferBeginPosition = s->inputStartPosition - s->inputBufferSampleCount;
    s->inputBufferEndPosition = s->inputStartPosition;

    s->inputBuffer = av_calloc(s->inputBufferSampleCount * s->channelCount, sizeof(float));
    if (!s->inputBuffer)
        return AVERROR(ENOMEM);

    s->outputSampleCount = 0;

    s->ptsOriginPosition = AV_NOPTS_VALUE;
    s->eof = 0;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    BungeeContext *s = ctx->priv;
    struct OutputChunk outputChunk;
    int64_t pts;
    int status, ret = 0, endOfOutput = 0;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (!s->eof)
    {
        AVFrame *in;
        ret = ff_inlink_consume_samples(inlink, 1, samplesNeeded(s), &in);

        if (ff_inlink_acknowledge_status(inlink, &status, &pts))
            s->eof = status == AVERROR_EOF;

        if (in)
        {
            if (s->start_pts == AV_NOPTS_VALUE)
                s->start_pts = av_rescale_q(in->pts, inlink->time_base, outlink->time_base);

            appendToInputBuffer(s, in);
            av_frame_free(&in);
        }

        if (s->eof)
            s->inputFinishPosition = s->inputBufferEndPosition;
    }
    else
        appendToInputBuffer(s, NULL);

    av_assert1(s->inputBufferEndPosition == s->inputChunk.end);

    if (samplesNeeded(s) == 0 || s->eof)
    {
        s->request.pitch = pow(2., s->semitones * (1. / 12));

        s->functions->next(s->stretcher, &s->request);
        s->functions->analyseGrain(s->stretcher, s->inputBuffer, s->inputBufferSampleCount, 0, samplesNeeded(s));
        s->functions->synthesiseGrain(s->stretcher, &outputChunk);
        s->inputChunk = s->functions->specifyGrain(s->stretcher, &s->request, 0);

        endOfOutput = s->eof && outputChunk.request[1]->position >= s->inputFinishPosition;

        if (outputChunk.request[0]->position >= s->inputStartPosition)
        {
            AVRational outputTimebase = {1, s->sampleRates.output};
            AVFrame *out = ff_get_audio_buffer(outlink, outputChunk.frameCount);
            float *const *p = (float *const *)out->extended_data;

            if (!out)
                return AVERROR(ENOMEM);

            if (endOfOutput)
            {
                const double fraction = (s->inputFinishPosition - outputChunk.request[0]->position) / (outputChunk.request[1]->position - outputChunk.request[0]->position);
                int frameCount = round(outputChunk.frameCount * fraction);
                if (frameCount < 0)
                    frameCount = 0;
                if (frameCount > outputChunk.frameCount)
                    frameCount = outputChunk.frameCount;
                outputChunk.frameCount = frameCount;
            }

            for (int c = 0; c < s->channelCount; ++c)
                memcpy(p[c], &outputChunk.data[c * outputChunk.channelStride], sizeof(float) * outputChunk.frameCount);

            out->pts = s->start_pts + av_rescale_q(s->outputSampleCount, outputTimebase, outlink->time_base);
            s->last_pts = out->pts;

            out->nb_samples = outputChunk.frameCount;
            s->outputSampleCount += outputChunk.frameCount;

            ret = ff_filter_frame(outlink, out);
            if (ret < 0)
                return ret;
        }
    }

    if (ff_inlink_queued_samples(inlink) >= samplesNeeded(s))
        ff_filter_set_ready(ctx, 100);

    if (endOfOutput)
    {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->last_pts);
        return 0;
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static const AVFilterPad bungee_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

const FFFilter BUNGEE_AF = {
    .p.name = BUNGEE_name,
    .p.description = NULL_IF_CONFIG_SMALL("Adjust audio speed and pitch using " BUNGEE_Name "."),
    .p.priv_class = &BUNGEE_CLASS,
    .priv_size = sizeof(BungeeContext),
    .init = init,
    .uninit = uninit,
    .activate = activate,
    FILTER_INPUTS(bungee_inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_FLTP),
};
