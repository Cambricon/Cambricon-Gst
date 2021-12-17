/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#include "easyinfer/easy_infer.h"

#include <memory>
#include <utility>

#include "cxxutil/log.h"
#include "internal/mlu_task_queue.h"
#include "model_loader_internal.h"

namespace edk {

class EasyInferPrivate {
 public:
  std::shared_ptr<ModelLoader> model_ = nullptr;
  cnrtFunction_t function_ = nullptr;
  MluTaskQueue_t queue_ = nullptr;
  void** param_ = nullptr;
  int batch_size_ = 1;
  cnrtRuntimeContext_t runtime_context_ = nullptr;
  std::unique_ptr<TimeMark> mark_start_{nullptr}, mark_end_{nullptr};
};

EasyInfer::EasyInfer() {
  d_ptr_ = new EasyInferPrivate;
}

EasyInfer::~EasyInfer() {
  if (d_ptr_->runtime_context_ != nullptr) {
    cnrtDestroyRuntimeContext(d_ptr_->runtime_context_);
  }
  if (nullptr != d_ptr_->function_) {
    cnrtDestroyFunction(d_ptr_->function_);
  }
  if (nullptr != d_ptr_->param_) {
    delete[] d_ptr_->param_;
  }
  delete d_ptr_;
}

void EasyInfer::Init(std::shared_ptr<ModelLoader> model, int dev_id) {
  d_ptr_->model_ = std::move(model);
  ModelLoaderInternalInterface interface(d_ptr_->model_.get());

  // clang-format off
  LOGD(INFER) << "Init inference context:"
              << "\n\t device id: " << dev_id
              << "\n\t model: " << reinterpret_cast<void*>(d_ptr_->model_.get());
  // clang-format on

  // init function
  CALL_CNRT_FUNC(cnrtCreateFunction(&d_ptr_->function_), "Create function failed.");

  CALL_CNRT_FUNC(cnrtCopyFunction(&d_ptr_->function_, interface.Function()), "Copy function failed.");

  d_ptr_->batch_size_ = 1;
  cnrtChannelType_t channel = CNRT_CHANNEL_TYPE_NONE;
  CALL_CNRT_FUNC(cnrtCreateRuntimeContext(&d_ptr_->runtime_context_, d_ptr_->function_, NULL),
                 "Create runtime context failed!");

  CALL_CNRT_FUNC(cnrtSetRuntimeContextChannel(d_ptr_->runtime_context_, channel),
                 "Set Runtime Context Channel failed!");
  CALL_CNRT_FUNC(cnrtSetRuntimeContextDeviceId(d_ptr_->runtime_context_, dev_id),
                 "Set Runtime Context Device Id failed!");
  CALL_CNRT_FUNC(cnrtInitRuntimeContext(d_ptr_->runtime_context_, NULL), "Init runtime context failed!");

  LOGI(INFER) << "Create MLU task queue from runtime context";
  cnrtQueue_t cnrt_queue;
  CALL_CNRT_FUNC(cnrtRuntimeContextCreateQueue(d_ptr_->runtime_context_, &cnrt_queue),
                 "Runtime Context Create Queue failed");
  d_ptr_->queue_ = MluTaskQueueProxy::Wrap(cnrt_queue);
  // init param
  d_ptr_->param_ = new void*[d_ptr_->model_->InputNum() + d_ptr_->model_->OutputNum()];

  d_ptr_->mark_start_.reset(new TimeMark);
  d_ptr_->mark_end_.reset(new TimeMark);
}

void EasyInfer::Run(void** input, void** output, float* hw_time) const {
  int i_num = d_ptr_->model_->InputNum();
  int o_num = d_ptr_->model_->OutputNum();

  LOGT(INFER) << "Process inference on one frame, input num: " << i_num << " output num: " << o_num;
  LOGT(INFER) << "Inference, input: " << input << " output: " << output;
  // prepare params for invokefunction
  for (int i = 0; i < i_num; ++i) {
    d_ptr_->param_[i] = input[i];
  }
  for (int i = 0; i < o_num; ++i) {
    d_ptr_->param_[i_num + i] = output[i];
  }
  cnrtQueue_t q = MluTaskQueueProxy::GetCnrtQueue(d_ptr_->queue_);
  if (hw_time) {
    // place start event
    d_ptr_->mark_start_->Mark(q);
  }

  CALL_CNRT_FUNC(cnrtInvokeRuntimeContext(d_ptr_->runtime_context_, d_ptr_->param_, q, NULL),
                 "Invoke Runtime Context failed");

  if (hw_time) {
    // place end event
    d_ptr_->mark_end_->Mark(q);
  }
  d_ptr_->queue_->Sync();
  if (hw_time) {
    *hw_time = TimeMark::Count(*d_ptr_->mark_start_, *d_ptr_->mark_end_);
    LOGI(INFER) << "Inference hardware time " << *hw_time << " ms";
  }
}

void EasyInfer::RunAsync(void** input, void** output, MluTaskQueue_t task_queue) const {
  int i_num = d_ptr_->model_->InputNum();
  int o_num = d_ptr_->model_->OutputNum();

  LOGT(INFER) << "Process inference on one frame, input num: " << i_num << " output num: " << o_num;
  LOGT(INFER) << "Inference, input: " << input << " output: " << output;
  // prepare params for invokefunction
  for (int i = 0; i < i_num; ++i) {
    d_ptr_->param_[i] = input[i];
  }
  for (int i = 0; i < o_num; ++i) {
    d_ptr_->param_[i_num + i] = output[i];
  }

  void* extra = nullptr;
  cnrtInvokeParam_t cnrt_invoke_param;
  unsigned int ui_affinity = static_cast<unsigned int>(-1);
  extra = reinterpret_cast<void*>(&cnrt_invoke_param);
  cnrt_invoke_param.invoke_param_type = CNRT_INVOKE_PARAM_TYPE_0;
  cnrt_invoke_param.cluster_affinity.affinity = &ui_affinity;

  cnrtQueue_t q = MluTaskQueueProxy::GetCnrtQueue(task_queue);
  CALL_CNRT_FUNC(cnrtInvokeRuntimeContext(d_ptr_->runtime_context_, d_ptr_->param_, q, extra),
                 "Invoke Runtime Context failed");
}

std::shared_ptr<ModelLoader> EasyInfer::Model() const { return d_ptr_->model_; }

MluTaskQueue_t EasyInfer::GetMluQueue() const { return d_ptr_->queue_; }

}  // namespace edk
