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

#include "device/mlu_context.h"

#include <cnrt.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "cxxutil/log.h"
#include "cxxutil/spinlock.h"
#include "internal/mlu_task_queue.h"

using std::string;
using std::to_string;

namespace edk {

#define MLU_CHANNEL_NUM 4

MluTaskQueue::Mark MluTaskQueue::PlaceMark() {
  uint32_t idx = 0;
  for (; idx < priv_->marks_valid.size(); ++idx) {
    if (priv_->marks_valid[idx]) break;
  }

  constexpr uint32_t marks_max_num = 40;
  if (idx == priv_->marks_valid.size()) {
    if (priv_->marks.size() > marks_max_num) {
      THROW_EXCEPTION(Exception::UNAVAILABLE, "marks number reach up limit, please donot store marks");
    }
    priv_->marks.emplace_back();
    priv_->marks_valid.push_back(true);
    LOGT(DEVICE) << "add new TimeMark, total: " << priv_->marks.size();
  }

  priv_->marks[idx].Mark(priv_->queue);
  priv_->marks_valid[idx] = false;
  return MluTaskQueue::Mark([this](int id) { priv_->marks_valid[id] = true; }, idx);
}

float MluTaskQueue::Count(const MluTaskQueue::Mark& s, const MluTaskQueue::Mark& e) const {
  int start = s.Index(), end = e.Index();
  if (start < 0 || start >= static_cast<int>(priv_->marks.size()) ||
      end < 0 || end >= static_cast<int>(priv_->marks.size())) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "Marks not exist");
  }
  if (priv_->marks_valid[start] || priv_->marks_valid[end]) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "Marks has not been placed");
  }
  return TimeMark::Count(priv_->marks[start], priv_->marks[end]);
}

MluTaskQueue::MluTaskQueue() {
  priv_.reset(new MluTaskQueuePrivate);
  constexpr uint32_t init_mark_num = 2;
  priv_->marks.reserve(init_mark_num * 2);
  priv_->marks_valid.reserve(init_mark_num * 2);
  for (uint32_t cnt = 0; cnt < init_mark_num; ++cnt) {
    priv_->marks.emplace_back();
    priv_->marks_valid.push_back(true);
  }
}

std::shared_ptr<MluTaskQueue> MluTaskQueue::Create() {
  auto q = std::shared_ptr<MluTaskQueue>(new MluTaskQueue);
  LOGD(DEVICE) << "Create cnrtQueue";
  CALL_CNRT_FUNC(cnrtCreateQueue(&q->priv_->queue), "Create cnrtQueue failed.");
  return q;
}

void MluTaskQueue::Sync() {
  CHECK(DEVICE, priv_->queue) << "task queue is uninitialized!";
  CALL_CNRT_FUNC(cnrtSyncQueue(priv_->queue), "Sync queue failed.");
  LOGT(DEVICE) << "Sync MLU task queue: " << reinterpret_cast<void*>(priv_->queue);
}

MluTaskQueuePrivate::~MluTaskQueuePrivate() {
  if (queue) {
    LOGD(DEVICE) << "Destroy cnrtQueue";
    cnrtRet_t ret = cnrtDestroyQueue(queue);
    if (ret != CNRT_RET_SUCCESS) {
      LOGE(DEVICE) << "Destroy cnrtQueue failed, error code: " << ret;
    }
  }
}

namespace _cnrt_init_tool {
/**
 * @brief singleton for init cambricon runtime
 */
class CnrtInitTool {
 public:
  CnrtInitTool() : is_initialized_(false) {}

  ~CnrtInitTool() {
    if (is_initialized_) {
      LOGI(DEVICE) << "Cambricon runtime destroy";
      cnrtDestroy();
    }
  }

  void Init() {
    SpinLockGuard lk(lock_);
    if (!is_initialized_) {
      CALL_CNRT_FUNC(cnrtInit(0), "Init cambricon runtime failed.");
      uint32_t dev_cnt;
      CALL_CNRT_FUNC(cnrtGetDeviceCount(&dev_cnt), "Get device count failed.");
      if (0 == dev_cnt) {
        THROW_EXCEPTION(Exception::UNAVAILABLE, "No device found.");
      }
      LOGI(DEVICE) << "Cambricon runtime init success.";
      is_initialized_ = true;
    }
  }

 private:
  std::atomic<bool> is_initialized_;
  SpinLock lock_;

  // disable copy and assign
  CnrtInitTool(const CnrtInitTool&) = delete;
  CnrtInitTool& operator=(const CnrtInitTool&) = delete;
};  // class CnrtInitTool
static CnrtInitTool cnrt_init_tool;
}  // namespace _cnrt_init_tool

bool MluContext::CheckDeviceId(int id) {
  _cnrt_init_tool::cnrt_init_tool.Init();
  cnrtDev_t dev;
  return CNRT_RET_SUCCESS == cnrtGetDeviceHandle(&dev, id);
}

uint32_t MluContext::GetDeviceNum() {
  _cnrt_init_tool::cnrt_init_tool.Init();
  uint32_t dev_cnt;
  CALL_CNRT_FUNC(cnrtGetDeviceCount(&dev_cnt), "Get device count failed.");
  return dev_cnt;
}

void MluContext::BindDevice() {
  _cnrt_init_tool::cnrt_init_tool.Init();
  cnrtDev_t dev;
  CALL_CNRT_FUNC(cnrtGetDeviceHandle(&dev, dev_id_), "Get device failed.");
  CALL_CNRT_FUNC(cnrtSetCurrentDevice(dev), "Set current device failed.");
  LOGT(DEVICE) << "Bind device [" << dev_id_ << "] for this thread";
  if (channel_id_ >= 0) {
    if (channel_id_ >= MLU_CHANNEL_NUM) {
      THROW_EXCEPTION(Exception::INVALID_ARG, "Only " + std::to_string(MLU_CHANNEL_NUM) +
                                                  " channel per mlu, channel id should less than " +
                                                  std::to_string(MLU_CHANNEL_NUM));
    }
    cnrtChannelType_t channel = static_cast<cnrtChannelType_t>(channel_id_);
    CALL_CNRT_FUNC(cnrtSetCurrentChannel(channel), "Set current channel failed.");
    LOGT(DEVICE) << "Bind channel [" << channel_id_ << "] for this thread";
  }
  CALL_CNRT_FUNC(cnrtSetDeviceFlag(1), "Set device flag failed.");
}

CoreVersion MluContext::GetCoreVersion() {
  _cnrt_init_tool::cnrt_init_tool.Init();
  static std::mutex m;
  CoreVersion version;
  cnrtDeviceInfo_t device_info;
  std::unique_lock<std::mutex> lk(m);
  CALL_CNRT_FUNC(cnrtGetDeviceInfo(&device_info, dev_id_), "Get device info failed.");
  lk.unlock();
  switch (device_info.core_version) {
    case CNRT_MLU220: {
      version = CoreVersion::MLU220;
      LOGD(DEVICE) << "Get Core Version MLU220";
      break;
    }
    case CNRT_MLU270: {
      version = CoreVersion::MLU270;
      LOGD(DEVICE) << "Get Core Version MLU270";
      break;
    }
    default:
      THROW_EXCEPTION(Exception::INTERNAL,
                      "Unsupport cnrt core version " + std::to_string(static_cast<int>(device_info.core_version)));
  }
  return version;
}

}  // namespace edk
