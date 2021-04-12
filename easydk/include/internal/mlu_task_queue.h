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

#ifndef EASYINFER_MLU_TASK_QUEUE_H_
#define EASYINFER_MLU_TASK_QUEUE_H_

#include <cnrt.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "device/mlu_context.h"

namespace edk {

#define CALL_CNRT_FUNC(func, msg)                                                                            \
  do {                                                                                                       \
    cnrtRet_t ret = (func);                                                                                  \
    if (CNRT_RET_SUCCESS != ret) {                                                                           \
      THROW_EXCEPTION(Exception::INTERNAL, std::string(msg) + ", cnrt error code : " + std::to_string(ret)); \
    }                                                                                                        \
  } while (0)

class TimeMark {
 public:
  TimeMark() { CALL_CNRT_FUNC(cnrtCreateNotifier(&base_), "Create notifier failed"); }

  TimeMark(TimeMark&& other) : base_(other.base_) { other.base_ = nullptr; }

  TimeMark& operator=(TimeMark&& other) {
    base_ = other.base_;
    other.base_ = nullptr;
    return *this;
  }

  ~TimeMark() {
    if (nullptr != base_) cnrtDestroyNotifier(&base_);
  }

  void Mark(cnrtQueue_t queue) { CALL_CNRT_FUNC(cnrtPlaceNotifier(base_, queue), "cnrtPlaceNotifier failed"); }

  void Mark(MluTaskQueue_t queue);

  cnrtNotifier_t GetNotifier() noexcept { return base_; }

  // get hardware time in ms
  static float Count(const TimeMark& start, const TimeMark& end) {
    float dura;
    CALL_CNRT_FUNC(cnrtNotifierDuration(start.base_, end.base_, &dura), "Calculate elapsed time failed.");
    dura /= 1000;
    return dura;
  }

 private:
  TimeMark(const TimeMark&) = delete;
  TimeMark& operator=(const TimeMark&) = delete;
  cnrtNotifier_t base_{nullptr};
};

struct MluTaskQueuePrivate {
  ~MluTaskQueuePrivate();
  cnrtQueue_t queue = nullptr;
  std::vector<TimeMark> marks;
  std::vector<bool> marks_valid;
};

inline void MluTaskQueue::_PrivDelete::operator()(MluTaskQueuePrivate* p) { delete p; }

class MluTaskQueueProxy {
 public:
  static cnrtQueue_t GetCnrtQueue(MluTaskQueue_t q) noexcept { return q->priv_->queue; }

  static void SetCnrtQueue(MluTaskQueue_t q, cnrtQueue_t cnrt_q) {
    if (q->priv_->queue) {
      q->priv_.reset(new MluTaskQueuePrivate);
    }
    q->priv_->queue = cnrt_q;
  }

  static MluTaskQueue_t Wrap(cnrtQueue_t cnrt_q) {
    auto q = std::shared_ptr<MluTaskQueue>(new MluTaskQueue);
    q->priv_->queue = cnrt_q;
    return q;
  }
};

inline void TimeMark::Mark(MluTaskQueue_t queue) { Mark(MluTaskQueueProxy::GetCnrtQueue(std::move(queue))); }

}  // namespace edk

#endif  // EASYINFER_MLU_TASK_QUEUE_H_
