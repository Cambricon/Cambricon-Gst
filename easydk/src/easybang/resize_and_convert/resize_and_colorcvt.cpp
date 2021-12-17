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

#include "easybang/resize_and_colorcvt.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "cxxutil/log.h"
#include "device/mlu_context.h"
#include "internal/mlu_task_queue.h"

using std::string;
extern bool PrepareKernelParam(int d_row, int d_col, int color_mode, int data_type, int batchsize,
                               bool keep_aspect_ratio, KernelParam** param, int dev_type, int padMethod, string* estr);

extern void FreeKernelParam(KernelParam* param);

extern float ResizeAndConvert(void* dst, void** y_plane_addrs, void** uv_plane_addrs, int** src_whs, int** src_rois_mlu,
                              int* src_rois_cpu, KernelParam* kparam, cnrtFunctionType_t func_type, cnrtDim3_t dim,
                              cnrtQueue_t queue, int dev_type, string* estr);

namespace edk {

std::ostream& operator<<(std::ostream& os, const MluResizeConvertOp::InputData& data) {
  os << "\n\t y plane attr: " << data.planes[0] << "\n"
     << "\t uv plane attr: " << data.planes[1] << "\n"
     << "\t src w: " << data.src_w << "\n"
     << "\t src h: " << data.src_h << "\n"
     << "\t src stride: " << data.src_stride << "\n"
     << "\t crop x: " << data.crop_x << "\n"
     << "\t crop y: " << data.crop_y << "\n"
     << "\t crop w: " << data.crop_w << "\n"
     << "\t crop h: " << data.crop_h;
  return os;
}

class MluResizeConvertPrivate {
 public:
  MluResizeConvertOp::Attr attr_;
  cnrtFunctionType_t ftype_ = CNRT_FUNC_TYPE_BLOCK;
  MluTaskQueue_t queue_ = nullptr;
  KernelParam* kparam_ = nullptr;
  std::deque<MluResizeConvertOp::InputData> input_datas_cache_;
  void **y_ptrs_cpu_ = nullptr, **uv_ptrs_cpu_ = nullptr;
  void **y_ptrs_mlu_ = nullptr, **uv_ptrs_mlu_ = nullptr;
  int** src_whs_mlu_ = nullptr;
  int* src_whs_mlu_tmp_ = nullptr;
  int* src_whs_cpu_ = nullptr;
  int** src_rois_mlu_ = nullptr;
  int* src_rois_mlu_tmp_ = nullptr;
  int* src_rois_cpu_ = nullptr;
  std::string estr_;
  bool shared_queue_ = false;

  bool PrepareTaskQueue();
};

MluResizeConvertOp::MluResizeConvertOp() { d_ptr_ = new MluResizeConvertPrivate; }

MluResizeConvertOp::~MluResizeConvertOp() {
  Destroy();
  delete d_ptr_;
  d_ptr_ = nullptr;
}

const MluResizeConvertOp::Attr& MluResizeConvertOp::GetAttr() { return d_ptr_->attr_; }

MluTaskQueue_t MluResizeConvertOp::GetMluQueue() const { return d_ptr_->queue_; }

void MluResizeConvertOp::SetMluQueue(MluTaskQueue_t queue) {
  if (queue) {
    d_ptr_->queue_ = queue;
    d_ptr_->shared_queue_ = true;
  } else {
    LOGW(RESIZE_CONVERT) << "SetMluQueue(): param queue is nullptr";
  }
}

bool MluResizeConvertOp::IsSharedQueue() const { return d_ptr_->shared_queue_; }

std::string MluResizeConvertOp::GetLastError() const { return d_ptr_->estr_; }

#define CHECK_CNRT_RET(cnrt_ret, _estr, msg, code, ret_value) \
  do {                                                        \
    if (cnrt_ret != CNRT_RET_SUCCESS) {                       \
      _estr = msg;                                            \
      { code }                                                \
      return ret_value;                                       \
    }                                                         \
  } while (0)

bool MluResizeConvertOp::Init(const MluResizeConvertOp::Attr& attr) {
  d_ptr_->attr_ = attr;

  int batchsize = attr.batch_size;

  d_ptr_->y_ptrs_cpu_ = new void*[batchsize];
  d_ptr_->uv_ptrs_cpu_ = new void*[batchsize];
  cnrtRet_t cnret;
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->y_ptrs_mlu_), sizeof(void*) * batchsize);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Malloc mlu buffer failed. cnrt error code:" + std::to_string(cnret), {}, false);
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->uv_ptrs_mlu_), sizeof(void*) * batchsize);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Malloc mlu buffer failed. cnrt error code:" + std::to_string(cnret), {}, false);
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_whs_mlu_tmp_), sizeof(int) * batchsize * 2);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Malloc mlu buffer failed. cnrt error code:" + std::to_string(cnret), {}, false);
  d_ptr_->src_whs_cpu_ = new int[batchsize * 2];
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_rois_mlu_tmp_), sizeof(int) * batchsize * 4);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Malloc mlu buffer failed. cnrt error code:" + std::to_string(cnret), {}, false);
  d_ptr_->src_rois_cpu_ = new int[batchsize * 4];
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_whs_mlu_), sizeof(int*) * batchsize);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Malloc mlu buffer failed. cnrt error code:" + std::to_string(cnret), {}, false);
  cnret = cnrtMalloc(reinterpret_cast<void**>(&d_ptr_->src_rois_mlu_), sizeof(int*) * batchsize);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Malloc mlu buffer failed. cnrt error code:" + std::to_string(cnret), {}, false);
  int** wh_mlu_ptrs_tmp = new int*[batchsize];
  int** roi_mlu_ptrs_tmp = new int*[batchsize];
  for (int i = 0; i < batchsize; ++i) {
    wh_mlu_ptrs_tmp[i] = d_ptr_->src_whs_mlu_tmp_ + 2 * i;
    roi_mlu_ptrs_tmp[i] = d_ptr_->src_rois_mlu_tmp_ + 4 * i;
  }
  cnret = cnrtMemcpy(reinterpret_cast<void*>(d_ptr_->src_whs_mlu_), reinterpret_cast<void*>(wh_mlu_ptrs_tmp),
                     sizeof(int*) * batchsize, CNRT_MEM_TRANS_DIR_HOST2DEV);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Memcpy h2d failed. cnrt error code:" + std::to_string(cnret), {}, false);
  cnret = cnrtMemcpy(reinterpret_cast<void*>(d_ptr_->src_rois_mlu_), reinterpret_cast<void*>(roi_mlu_ptrs_tmp),
                     sizeof(int*) * batchsize, CNRT_MEM_TRANS_DIR_HOST2DEV);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Memcpy h2d failed. cnrt error code:" + std::to_string(cnret), {}, false);
  delete[] wh_mlu_ptrs_tmp;
  delete[] roi_mlu_ptrs_tmp;

  int core_number = attr.core_number == 0 ? 4 : attr.core_number;

  switch (core_number) {
    case 1:
    case 4:
    case 8:
    case 16:
      break;
    default:
      d_ptr_->estr_ = "Unsupport core number. Only support 1, 4, 8, 16";
      return false;
  }

  d_ptr_->attr_.core_number = core_number;

  // clang-format off
  LOGD(RESIZE_CONVERT) << "Init ResizeAndConvert Operator:\n"
                       << "\t [batchsize " + std::to_string(d_ptr_->attr_.batch_size) + "], "
                       << "[core_number: " + std::to_string(d_ptr_->attr_.core_number) + "],\n"
                       << "\t [keep_aspect_ratio " << d_ptr_->attr_.keep_aspect_ratio << "],\n"
                       << "\t [core_version " << static_cast<int>(d_ptr_->attr_.core_version) << "],\n"
                       << "\t [color_mode " << static_cast<int>(d_ptr_->attr_.color_mode) << "], "
                       << "[data_mode " << static_cast<int>(d_ptr_->attr_.data_mode) << "]\n"
                       << "\t [pad_method " << d_ptr_->attr_.padMethod << "]\n";

  return ::PrepareKernelParam(d_ptr_->attr_.dst_h, d_ptr_->attr_.dst_w,
                              static_cast<int>(d_ptr_->attr_.color_mode),
                              static_cast<int>(d_ptr_->attr_.data_mode),
                              d_ptr_->attr_.batch_size,
                              d_ptr_->attr_.keep_aspect_ratio, &d_ptr_->kparam_,
                              static_cast<int>(d_ptr_->attr_.core_version),
                              d_ptr_->attr_.padMethod, &d_ptr_->estr_);
  // clang-format on
}

bool MluResizeConvertPrivate::PrepareTaskQueue() {
  queue_ = MluTaskQueue::Create();
  shared_queue_ = false;
  return true;
}

void MluResizeConvertOp::BatchingUp(const InputData& input_data) {
  InputData t;
  t.src_w = input_data.src_w;
  t.src_h = input_data.src_h;
  if (t.src_h % 2) t.src_h--;
  t.src_stride = input_data.src_w > input_data.src_stride ? input_data.src_w : input_data.src_stride;
  t.crop_x = input_data.crop_x;
  t.crop_y = input_data.crop_y;
  t.crop_w = input_data.crop_w == 0 ? t.src_w : input_data.crop_w;
  t.crop_w = std::min(t.src_w - t.crop_x, t.crop_w);
  t.crop_h = input_data.crop_h == 0 ? t.src_h : input_data.crop_h;
  t.crop_h = std::min(t.src_h - t.crop_y, t.crop_h);
  t.planes[0] = input_data.planes[0];
  t.planes[1] = input_data.planes[1];

  LOGT(RESIZE_CONVERT) << "Store resize and convert operator input for batching," << t;
  d_ptr_->input_datas_cache_.emplace_back(std::move(t));
}

bool MluResizeConvertOp::SyncOneOutput(void* dst) {
  if (nullptr == d_ptr_->queue_) {
    LOGD(RESIZE_CONVERT) << "MluTaskQueue has not been set, MluResizeConvertOp will create a new one";
    if (!d_ptr_->PrepareTaskQueue()) {
      return false;
    }
  }
  if (d_ptr_->input_datas_cache_.size() == 0) {
    LOGW(RESIZE_CONVERT) << "No data batched , do nothing.";
    return false;
  }
  // while cache count less than batch size, fill with copy to batch size
  while (static_cast<int>(d_ptr_->input_datas_cache_.size()) < d_ptr_->attr_.batch_size) {
    d_ptr_->input_datas_cache_.push_back(d_ptr_->input_datas_cache_[0]);
  }
  for (int bi = 0; bi < d_ptr_->attr_.batch_size; ++bi) {
    InputData& input_data = d_ptr_->input_datas_cache_.front();
    d_ptr_->y_ptrs_cpu_[bi] = input_data.planes[0];
    d_ptr_->uv_ptrs_cpu_[bi] = input_data.planes[1];
    d_ptr_->src_whs_cpu_[bi * 2 + 0] = input_data.src_stride;
    d_ptr_->src_whs_cpu_[bi * 2 + 1] = input_data.src_h;
    d_ptr_->src_rois_cpu_[bi * 4 + 0] = input_data.crop_x;
    d_ptr_->src_rois_cpu_[bi * 4 + 1] = input_data.crop_y;
    d_ptr_->src_rois_cpu_[bi * 4 + 2] = input_data.crop_w;
    d_ptr_->src_rois_cpu_[bi * 4 + 3] = input_data.crop_h;
    d_ptr_->input_datas_cache_.pop_front();
  }
  // clang-format off
  cnrtRet_t cnret = cnrtMemcpy(reinterpret_cast<void*>(d_ptr_->y_ptrs_mlu_),
                               reinterpret_cast<void*>(d_ptr_->y_ptrs_cpu_),
                               sizeof(void*) * d_ptr_->attr_.batch_size,
                               CNRT_MEM_TRANS_DIR_HOST2DEV);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Memcpy host to device failed. cnrt error code:" + std::to_string(cnret), {},
                 false);
  cnret = cnrtMemcpy(reinterpret_cast<void*>(d_ptr_->uv_ptrs_mlu_),
                     reinterpret_cast<void*>(d_ptr_->uv_ptrs_cpu_),
                     sizeof(void*) * d_ptr_->attr_.batch_size,
                     CNRT_MEM_TRANS_DIR_HOST2DEV);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Memcpy host to device failed. cnrt error code:" + std::to_string(cnret), {},
                 false);
  cnret = cnrtMemcpy(reinterpret_cast<void*>(d_ptr_->src_whs_mlu_tmp_),
                     reinterpret_cast<void*>(d_ptr_->src_whs_cpu_),
                     sizeof(int) * 2 * d_ptr_->attr_.batch_size,
                     CNRT_MEM_TRANS_DIR_HOST2DEV);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Memcpy width and height failed. cnrt error code:" + std::to_string(cnret), {},
                 false);
  cnret = cnrtMemcpy(reinterpret_cast<void*>(d_ptr_->src_rois_mlu_tmp_),
                     reinterpret_cast<void*>(d_ptr_->src_rois_cpu_),
                     sizeof(int) * d_ptr_->attr_.batch_size * 4,
                     CNRT_MEM_TRANS_DIR_HOST2DEV);
  CHECK_CNRT_RET(cnret, d_ptr_->estr_, "Memcpy rois failed. cnrt error code:" + std::to_string(cnret), {}, false);
  // clang-format on

  cnrtDim3_t dim;
  dim.x = 4;
  dim.y = d_ptr_->attr_.core_number >= 4 ? d_ptr_->attr_.core_number / 4 : 1;
  dim.z = 1;

  LOGT(RESIZE_CONVERT) << "(SyncOneOutput) Do resize and convert process, dst: " << dst;
  // clang-format off
  bool ret = -1 != ::ResizeAndConvert(dst, d_ptr_->y_ptrs_mlu_, d_ptr_->uv_ptrs_mlu_,
                                      d_ptr_->src_whs_mlu_, d_ptr_->src_rois_mlu_,
                                      d_ptr_->src_rois_cpu_,
                                      d_ptr_->kparam_, CNRT_FUNC_TYPE_UNION1, dim,
                                      MluTaskQueueProxy::GetCnrtQueue(d_ptr_->queue_),
                                      static_cast<int>(d_ptr_->attr_.core_version),
                                      &d_ptr_->estr_);
  // clang-format on

  if (!ret) {
    LOGE(RESIZE_CONVERT) << "Resize convert failed. Info: ";
    LOGE(RESIZE_CONVERT) << "dst w, dst h: " << d_ptr_->attr_.dst_w << " " << d_ptr_->attr_.dst_h;
    LOGE(RESIZE_CONVERT) << "keep aspect ratio: " << d_ptr_->attr_.keep_aspect_ratio;
    LOGE(RESIZE_CONVERT) << "batchsize: " << d_ptr_->attr_.batch_size;
    auto inputs = GetLastBatchInput();
    for (const auto& it : inputs) LOGE(RESIZE_CONVERT) << it;
    cnrtMemset(dst, 0, size_t(1) * d_ptr_->attr_.batch_size * d_ptr_->attr_.dst_w * d_ptr_->attr_.dst_h * 4);
    if (!IsSharedQueue()) {
      // queue becomes invalid when an error occurs.
      d_ptr_->queue_ = MluTaskQueue::Create();
    }
  }
  return ret;
}

std::vector<MluResizeConvertOp::InputData> MluResizeConvertOp::GetLastBatchInput() const {
  std::vector<MluResizeConvertOp::InputData> datas;
  for (int bi = 0; bi < d_ptr_->attr_.batch_size; ++bi) {
    InputData t;
    t.planes[0] = d_ptr_->y_ptrs_cpu_[bi];
    t.planes[1] = d_ptr_->uv_ptrs_cpu_[bi];
    t.src_w = d_ptr_->src_whs_cpu_[bi * 2 + 0];
    t.src_stride = t.src_w;
    t.src_h = d_ptr_->src_whs_cpu_[bi * 2 + 1];
    t.crop_x = d_ptr_->src_rois_cpu_[bi * 4 + 0];
    t.crop_y = d_ptr_->src_rois_cpu_[bi * 4 + 1];
    t.crop_w = d_ptr_->src_rois_cpu_[bi * 4 + 2];
    t.crop_h = d_ptr_->src_rois_cpu_[bi * 4 + 3];
    datas.emplace_back(std::move(t));
  }
  return datas;
}

void MluResizeConvertOp::Destroy() {
  if (d_ptr_->kparam_) {
    ::FreeKernelParam(d_ptr_->kparam_);
    d_ptr_->kparam_ = nullptr;
  }
  if (d_ptr_->y_ptrs_cpu_) {
    delete[] d_ptr_->y_ptrs_cpu_;
    d_ptr_->y_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->uv_ptrs_cpu_) {
    delete[] d_ptr_->uv_ptrs_cpu_;
    d_ptr_->uv_ptrs_cpu_ = nullptr;
  }
  if (d_ptr_->y_ptrs_mlu_) {
    cnrtFree(reinterpret_cast<void*>(d_ptr_->y_ptrs_mlu_));
    d_ptr_->y_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->uv_ptrs_mlu_) {
    cnrtFree(reinterpret_cast<void*>(d_ptr_->uv_ptrs_mlu_));
    d_ptr_->uv_ptrs_mlu_ = nullptr;
  }
  if (d_ptr_->src_whs_mlu_) {
    cnrtFree(reinterpret_cast<void*>(d_ptr_->src_whs_mlu_));
    d_ptr_->src_whs_mlu_ = nullptr;
  }
  if (d_ptr_->src_whs_mlu_tmp_) {
    cnrtFree(d_ptr_->src_whs_mlu_tmp_);
    d_ptr_->src_whs_mlu_tmp_ = nullptr;
  }
  if (d_ptr_->src_whs_cpu_) {
    delete[] d_ptr_->src_whs_cpu_;
    d_ptr_->src_whs_cpu_ = nullptr;
  }
  if (d_ptr_->src_rois_mlu_) {
    cnrtFree(reinterpret_cast<void*>(d_ptr_->src_rois_mlu_));
    d_ptr_->src_rois_mlu_ = nullptr;
  }
  if (d_ptr_->src_rois_mlu_tmp_) {
    cnrtFree(d_ptr_->src_rois_mlu_tmp_);
    d_ptr_->src_rois_mlu_tmp_ = nullptr;
  }
  if (d_ptr_->src_rois_cpu_) {
    delete[] d_ptr_->src_rois_cpu_;
    d_ptr_->src_rois_cpu_ = nullptr;
  }
  d_ptr_->input_datas_cache_.clear();
}

}  // namespace edk
