/*
 * Copyright (c) 2024
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
 * DNN Torch backend implementation.
 */

#include <torch/torch.h>
#include <torch/script.h>

extern "C" {
#include "libavutil/opt.h"
#include "libavutil/mem.h"
#include "libavutil/avassert.h"
#include "../dnn_interface.h"
#include "dnn_backend_common.h"
#include "dnn_io_proc.h"
#include "queue.h"
#include "safe_queue.h"
}

static int get_input_th(DNNModel *model, DNNData *input, const char *input_name);
static int get_output_th(DNNModel *model, const char *input_name, int input_width, int input_height, const char *output_name, int *output_width, int *output_height);
static void dnn_free_model_th(DNNModel **model);
static int th_start_inference(void *args);
static void infer_completion_callback(void *args);

typedef struct THModel {
    DNNModel model;
    DnnContext *ctx;
    torch::jit::Module *jit_model;
    SafeQueue *request_queue;
    Queue *task_queue;
    Queue *lltask_queue;
} THModel;

typedef struct THInferRequest {
    torch::Tensor *output;
    torch::Tensor *input_tensor;
    uint8_t *input_data;
    size_t input_data_size;
} THInferRequest;

typedef struct THRequestItem {
    THInferRequest *infer_request;
    LastLevelTaskItem *lltask;
    DNNAsyncExecModule exec_module;
} THRequestItem;


#define OFFSET(x) offsetof(THOptions, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM
static const AVOption dnn_th_options[] = {
    { "optimize", "turn on graph executor optimization", OFFSET(optimize), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS},
    { NULL }
};

static int extract_lltask_from_task(TaskItem *task, Queue *lltask_queue)
{
    THModel *th_model = (THModel *)task->model;
    DnnContext *ctx = th_model->ctx;
    LastLevelTaskItem *lltask = (LastLevelTaskItem *)av_malloc(sizeof(*lltask));
    if (!lltask) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate memory for LastLevelTaskItem\n");
        return AVERROR(ENOMEM);
    }
    task->inference_todo = 1;
    task->inference_done = 0;
    lltask->task = task;
    if (ff_queue_push_back(lltask_queue, lltask) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to push back lltask_queue.\n");
        av_freep(&lltask);
        return AVERROR(ENOMEM);
    }
    return 0;
}

static void th_free_request(THInferRequest *request)
{
    if (!request)
        return;
    if (request->output) {
        delete(request->output);
        request->output = NULL;
    }
    if (request->input_tensor) {
        delete(request->input_tensor);
        request->input_tensor = NULL;
    }
    if (request->input_data) {
        av_freep(&request->input_data);
    }
    request->input_data_size = 0;
}

static inline void destroy_request_item(THRequestItem **arg)
{
    THRequestItem *item;
    if (!arg || !*arg) {
        return;
    }
    item = *arg;
    th_free_request(item->infer_request);
    av_freep(&item->infer_request);
    av_freep(&item->lltask);
    ff_dnn_async_module_cleanup(&item->exec_module);
    av_freep(arg);
}

static void dnn_free_model_th(DNNModel **model)
{
    THModel *th_model;
    if (!model || !*model)
        return;

    th_model = (THModel *)(*model);

    if (th_model->request_queue) {
        while (ff_safe_queue_size(th_model->request_queue) != 0) {
            THRequestItem *item = (THRequestItem *)ff_safe_queue_pop_front(th_model->request_queue);
            destroy_request_item(&item);
        }
        ff_safe_queue_destroy(th_model->request_queue);
    }

    if (th_model->lltask_queue) {
        while (ff_queue_size(th_model->lltask_queue) != 0) {
            LastLevelTaskItem *item = (LastLevelTaskItem *)ff_queue_pop_front(th_model->lltask_queue);
            av_freep(&item);
        }
        ff_queue_destroy(th_model->lltask_queue);
    }

    if (th_model->task_queue) {
        while (ff_queue_size(th_model->task_queue) != 0) {
            TaskItem *item = (TaskItem *)ff_queue_pop_front(th_model->task_queue);
            av_frame_free(&item->in_frame);
            av_frame_free(&item->out_frame);
            av_freep(&item);
        }
        ff_queue_destroy(th_model->task_queue);
    }

    if (th_model->jit_model)
        delete th_model->jit_model;

    av_freep(&th_model);
    *model = NULL;
}

static int get_input_th(DNNModel *model, DNNData *input, const char *input_name)
{
    input->dt = DNN_FLOAT;
    input->order = DCO_RGB;
    input->layout = DL_NCHW;
    input->dims[0] = 1;
    input->dims[1] = 3;
    input->dims[2] = -1;
    input->dims[3] = -1;
    return 0;
}

static int fill_model_input_th(THModel *th_model, THRequestItem *request)
{
    THInferRequest *infer_request = NULL;
    TaskItem *task = NULL;
    LastLevelTaskItem *lltask = NULL;
    DNNData input = { 0 };
    DnnContext *ctx = th_model->ctx;
    int ret, width_idx, height_idx, channel_idx;
    size_t cur_size;

    lltask = (LastLevelTaskItem *)ff_queue_pop_front(th_model->lltask_queue);
    if (!lltask)
        return AVERROR(EINVAL);

    request->lltask = lltask;
    task = lltask->task;
    infer_request = request->infer_request;

    ret = get_input_th(&th_model->model, &input, NULL);
    if (ret != 0)
        goto err;

    width_idx = dnn_get_width_idx_by_layout(input.layout);
    height_idx = dnn_get_height_idx_by_layout(input.layout);
    channel_idx = dnn_get_channel_idx_by_layout(input.layout);

    input.dims[height_idx] = task->in_frame->height;
    input.dims[width_idx] = task->in_frame->width;

    // Calculate required size for the current frame
    cur_size = (size_t)input.dims[height_idx] * input.dims[width_idx] *
               input.dims[channel_idx] * sizeof(float);

    /**
     * Reuse the persistent buffer. 
     * Only reallocate if the existing buffer is too small or doesn't exist.
     */
    if (!infer_request->input_data || infer_request->input_data_size < cur_size) {
        av_freep(&infer_request->input_data);
        infer_request->input_data = (uint8_t *)av_malloc(cur_size);
        if (!infer_request->input_data) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
        infer_request->input_data_size = cur_size;
    }

    input.data = infer_request->input_data;

    // Initialize tensors if they don't exist
    if (!infer_request->input_tensor)
        infer_request->input_tensor = new torch::Tensor();
    if (!infer_request->output)
        infer_request->output = new torch::Tensor();

    switch (th_model->model.func_type) {
    case DFT_PROCESS_FRAME:
        input.scale = 255;
        if (task->do_ioproc) {
            if (th_model->model.frame_pre_proc != NULL) {
                th_model->model.frame_pre_proc(task->in_frame, &input, th_model->model.filter_ctx);
            } else {
                ff_proc_from_frame_to_dnn(task->in_frame, &input, ctx);
            }
        }
        break;
    default:
        avpriv_report_missing_feature(ctx, "model function type %d", th_model->model.func_type);
        ret = AVERROR(ENOSYS);
        goto err;
    }

    /**
     * Map the buffer to a Torch tensor. 
     * Note: We do NOT pass 'deleter' here because 'input_data' is owned 
     * by THInferRequest and will be freed in dnn_free_model_th.
     */
    *infer_request->input_tensor = torch::from_blob(input.data,
        {1, input.dims[channel_idx], input.dims[height_idx], input.dims[width_idx]},
        torch::kFloat32);

    return 0;

err:
    th_free_request(infer_request);
    return ret;
}

static int th_start_inference(void *args)
{
    THRequestItem *request = (THRequestItem *)args;
    THInferRequest *infer_request = NULL;
    LastLevelTaskItem *lltask = NULL;
    TaskItem *task = NULL;
    THModel *th_model = NULL;
    DnnContext *ctx = NULL;
    std::vector<torch::jit::IValue> inputs;
    torch::NoGradGuard no_grad;

    if (!request) {
        av_log(NULL, AV_LOG_ERROR, "THRequestItem is NULL\n");
        return AVERROR(EINVAL);
    }
    infer_request = request->infer_request;
    lltask = request->lltask;
    task = lltask->task;
    th_model = (THModel *)task->model;
    ctx = th_model->ctx;

    if (ctx->torch_option.optimize)
        torch::jit::setGraphExecutorOptimize(true);
    else
        torch::jit::setGraphExecutorOptimize(false);

    if (!infer_request->input_tensor || !infer_request->output) {
        av_log(ctx, AV_LOG_ERROR, "input or output tensor is NULL\n");
        return DNN_GENERIC_ERROR;
    }
    // Transfer tensor to the same device as model
    c10::Device device = (*th_model->jit_model->parameters().begin()).device();
    if (infer_request->input_tensor->device() != device)
        *infer_request->input_tensor = infer_request->input_tensor->to(device);
    inputs.push_back(*infer_request->input_tensor);

    *infer_request->output = th_model->jit_model->forward(inputs).toTensor();

    return 0;
}

static void infer_completion_callback(void *args)
{
    THRequestItem *request = (THRequestItem *)args;
    LastLevelTaskItem *lltask = request->lltask;
    TaskItem *task = lltask->task;
    THModel *th_model = (THModel *)task->model;
    THInferRequest *infer_request = request->infer_request;
    torch::Tensor *output = infer_request->output;
    DNNData outputs = { 0 };
    c10::IntArrayRef sizes;

    if (!output || output->ndimension() != 4) {
        avpriv_report_missing_feature(th_model->ctx, "torch model output dimensions != 4");
        goto err;
    }

    sizes = output->sizes();
    outputs.order  = DCO_RGB;
    outputs.layout = DL_NCHW;
    outputs.dt     = DNN_FLOAT;
    outputs.dims[0] = sizes.at(0);
    outputs.dims[1] = sizes.at(1);
    outputs.dims[2] = sizes.at(2);
    outputs.dims[3] = sizes.at(3);

    if (th_model->model.func_type == DFT_PROCESS_FRAME) {
        if (task->do_ioproc) {
            if (output->device() != torch::kCPU)
                *output = output->to(torch::kCPU);
            outputs.scale = 255;
            outputs.data = output->data_ptr();
            if (th_model->model.frame_post_proc)
                th_model->model.frame_post_proc(task->out_frame, &outputs, th_model->model.filter_ctx);
            else
                ff_proc_from_dnn_to_frame(task->out_frame, &outputs, th_model->ctx);
        } else {
            task->out_frame->width  = outputs.dims[dnn_get_width_idx_by_layout(outputs.layout)];
            task->out_frame->height = outputs.dims[dnn_get_height_idx_by_layout(outputs.layout)];
        }
    } else {
        avpriv_report_missing_feature(th_model->ctx, "model function type %d", th_model->model.func_type);
        goto err;
    }

    task->inference_done++;
    av_freep(&request->lltask);

err:
    th_free_request(infer_request);
    if (ff_safe_queue_push_back(th_model->request_queue, request) < 0) {
        destroy_request_item(&request);
    }
}

static int execute_model_th(THRequestItem *request, Queue *lltask_queue)
{
    THModel *th_model = NULL;
    LastLevelTaskItem *lltask;
    TaskItem *task = NULL;
    int ret = 0;

    if (ff_queue_size(lltask_queue) == 0) {
        destroy_request_item(&request);
        return 0;
    }

    lltask = (LastLevelTaskItem *)ff_queue_peek_front(lltask_queue);
    if (!lltask) {
        destroy_request_item(&request);
        return AVERROR(EINVAL);
    }
    task = lltask->task;
    th_model = (THModel *)task->model;

    ret = fill_model_input_th(th_model, request);
    if (ret != 0) goto err;

    if (task->async) {
        return ff_dnn_start_inference_async(th_model->ctx, &request->exec_module);
    } else {
        ret = th_start_inference(request);
        if (ret != 0) goto err;
        infer_completion_callback(request);
        return (task->inference_done == task->inference_todo) ? 0 : DNN_GENERIC_ERROR;
    }

err:
    th_free_request(request->infer_request);
    if (ff_safe_queue_push_back(th_model->request_queue, request) < 0) {
        destroy_request_item(&request);
    }
    return ret;
}

static THInferRequest *th_create_inference_request(void)
{
    THInferRequest *request = (THInferRequest *)av_mallocz(sizeof(THInferRequest));
    if (!request) {
        return NULL;
    }
    return request;
}

static int get_output_th(DNNModel *model, const char *input_name, int input_width, int input_height,
                                   const char *output_name, int *output_width, int *output_height)
{
    int ret = 0;
    THModel *th_model = (THModel *) model;
    DnnContext *ctx = th_model->ctx;
    TaskItem task = { 0 };
    THRequestItem *request = NULL;
    DNNExecBaseParams exec_params = {
        .input_name     = input_name,
        .output_names   = &output_name,
        .nb_output      = 1,
        .in_frame       = NULL,
        .out_frame      = NULL,
    };

    ret = ff_dnn_fill_gettingoutput_task(&task, &exec_params, th_model, input_height, input_width, ctx);
    if (ret != 0)
        goto err;

    ret = extract_lltask_from_task(&task, th_model->lltask_queue);
    if (ret != 0)
        goto err;

    request = (THRequestItem*) ff_safe_queue_pop_front(th_model->request_queue);
    if (!request) {
        ret = AVERROR(EINVAL);
        goto err;
    }

    ret = execute_model_th(request, th_model->lltask_queue);
    *output_width = task.out_frame->width;
    *output_height = task.out_frame->height;

err:
    av_frame_free(&task.out_frame);
    av_frame_free(&task.in_frame);
    return ret;
}

static DNNModel *dnn_load_model_th(DnnContext *ctx, DNNFunctionType func_type, AVFilterContext *filter_ctx)
{
    THModel *th_model = (THModel *)av_mallocz(sizeof(THModel));
    THRequestItem *item = NULL;
    const char *device_name = ctx->device ? ctx->device : "cpu";

    if (!th_model)
        return NULL;

    th_model->ctx = ctx;

    // Device and XPU Initialization
    c10::Device device = c10::Device(device_name);
    if (device.is_xpu()) {
        at::detail::getXPUHooks().initXPU();
    }

    try {
        th_model->jit_model = new torch::jit::Module;
        (*th_model->jit_model) = torch::jit::load(ctx->model_filename);
        th_model->jit_model->to(device);
    } catch (const c10::Error& e) {
        av_log(ctx, AV_LOG_ERROR, "Failed to load torch model\n");
        goto fail;
    }

    th_model->request_queue = ff_safe_queue_create();
    th_model->task_queue = ff_queue_create();
    th_model->lltask_queue = ff_queue_create();

    if (!th_model->request_queue || !th_model->task_queue || !th_model->lltask_queue)
        goto fail;

    item = (THRequestItem *)av_mallocz(sizeof(THRequestItem));
    if (!item)
        goto fail;

    item->infer_request = th_create_inference_request();
    if (!item->infer_request)
        goto fail;

    // Setup the async module callbacks for the common infrastructure
    item->exec_module.start_inference = &th_start_inference;
    item->exec_module.callback = &infer_completion_callback;
    item->exec_module.args = item;

    if (ff_safe_queue_push_back(th_model->request_queue, item) < 0)
        goto fail;

    th_model->model.get_input = &get_input_th;
    th_model->model.get_output = &get_output_th;
    th_model->model.filter_ctx = filter_ctx;
    th_model->model.func_type = func_type;

    return &th_model->model;

fail:
    dnn_free_model_th((DNNModel**)&th_model);
    return NULL;
}

static int dnn_execute_model_th(const DNNModel *model, DNNExecBaseParams *exec_params)
{
    THModel *th_model = (THModel *)model;
    DnnContext *ctx = th_model->ctx;
    TaskItem *task;
    THRequestItem *request;
    int ret = 0;

    ret = ff_check_exec_params(ctx, DNN_TH, model->func_type, exec_params);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "exec parameter checking fail.\n");
        return ret;
    }

    task = (TaskItem *)av_malloc(sizeof(TaskItem));
    if (!task) {
        av_log(ctx, AV_LOG_ERROR, "unable to alloc memory for task item.\n");
        return AVERROR(ENOMEM);
    }

    ret = ff_dnn_fill_task(task, exec_params, th_model, 0, 1);
    if (ret != 0) {
        av_freep(&task);
        av_log(ctx, AV_LOG_ERROR, "unable to fill task.\n");
        return ret;
    }

    ret = ff_queue_push_back(th_model->task_queue, task);
    if (ret < 0) {
        av_freep(&task);
        av_log(ctx, AV_LOG_ERROR, "unable to push back task_queue.\n");
        return ret;
    }

    ret = extract_lltask_from_task(task, th_model->lltask_queue);
    if (ret != 0) {
        av_log(ctx, AV_LOG_ERROR, "unable to extract last level task from task.\n");
        return ret;
    }

    request = (THRequestItem *)ff_safe_queue_pop_front(th_model->request_queue);
    if (!request) {
        av_log(ctx, AV_LOG_ERROR, "unable to get infer request.\n");
        return AVERROR(EINVAL);
    }

    return execute_model_th(request, th_model->lltask_queue);
}

static DNNAsyncStatusType dnn_get_result_th(const DNNModel *model, AVFrame **in, AVFrame **out)
{
    THModel *th_model = (THModel *)model;
    return ff_dnn_get_result_common(th_model->task_queue, in, out);
}

static int dnn_flush_th(const DNNModel *model)
{
    THModel *th_model = (THModel *)model;
    THRequestItem *request;

    if (ff_queue_size(th_model->lltask_queue) == 0)
        // no pending task need to flush
        return 0;

    request = (THRequestItem *)ff_safe_queue_pop_front(th_model->request_queue);
    if (!request) {
        av_log(th_model->ctx, AV_LOG_ERROR, "unable to get infer request.\n");
        return AVERROR(EINVAL);
    }

    return execute_model_th(request, th_model->lltask_queue);
}

extern const DNNModule ff_dnn_backend_torch = {
    .clazz          = DNN_DEFINE_CLASS(dnn_th),
    .type           = DNN_TH,
    .load_model     = dnn_load_model_th,
    .execute_model  = dnn_execute_model_th,
    .get_result     = dnn_get_result_th,
    .flush          = dnn_flush_th,
    .free_model     = dnn_free_model_th,
};
