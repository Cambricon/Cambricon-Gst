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

/**
 * @file mlu_context.h
 *
 * This file contains a declaration of the MluContext class.
 */

#ifndef EDK_MLU_CONTEXT_H_
#define EDK_MLU_CONTEXT_H_

#include <functional>
#include <memory>
#include <utility>

#include "cxxutil/edk_attribute.h"
#include "cxxutil/exception.h"

namespace edk {

/**
 * @brief Enumeration to describe MLU core version
 */
enum class CoreVersion {
  MLU220 = 1,  ///< MLU220 platform
  MLU270 = 2,  ///< MLU270 platform
};

struct MluTaskQueuePrivate;
class MluTaskQueueProxy;
/**
 * @brief encapsulation of cnrtQueue
 */
struct MluTaskQueue {
 public:
  /**
   * @brief Create a MluTaskQueue
   *
   * @return a MluTaskQueue_t
   */
  static std::shared_ptr<MluTaskQueue> Create();

  /**
   * @brief Sync MluTaskQueue
   */
  void Sync();

  /**
   * @brief TimeMark holder class
   *
   * @note Mark maps to device notifier resources, store Mark without release will cause increase of resources,
   *       an exception will be thrown while number of Marks reaches limit
   */
  class Mark {
   public:
    /**
     * @brief Construct a new Mark object
     *
     * @param release release function
     * @param idx index of TimeMark
     */
    Mark(std::function<void(int)> release, int idx) : release_(release), idx_(idx) {}

    /**
     * @brief Destroy the Mark object
     */
    ~Mark() {
      if (release_) release_(idx_);
    }

    /**
     * @brief Move construct a new Mark object
     *
     * @param other another Mark in right value reference
     */
    Mark(Mark&& other) : release_(std::move(other.release_)), idx_(other.idx_) { other.release_ = nullptr; }

    /**
     * @brief Move assign the Mark object
     *
     * @param other another Mark in right value reference
     * @return Mark& reference to this object
     */
    Mark& operator=(Mark&& other) {
      release_ = std::move(other.release_);
      idx_ = other.idx_;
      other.release_ = nullptr;
      return *this;
    }

    /**
     * @brief Get TimeMark index
     *
     * @return int Mapped TimeMark index
     */
    int Index() const noexcept { return idx_; }

   private:
    Mark() = delete;
    Mark(const Mark&) = delete;
    Mark& operator=(const Mark&) = delete;
    std::function<void(int)> release_{nullptr};
    int idx_{0};
  };

  /**
   * @brief Place a mark to measure hardware time of task between two mark
   *
   * @return Mark a Mark
   */
  Mark PlaceMark();

  /**
   * @brief Calculate hardware time between two mark
   *
   * @param start Mark before task
   * @param end Mark after task
   * @return float hardware time in milliseconds
   */
  float Count(const Mark& start, const Mark& end) const;

 private:
  struct _PrivDelete {
    void operator()(MluTaskQueuePrivate* p);
  };
  friend class MluTaskQueueProxy;
  MluTaskQueue();
  std::unique_ptr<MluTaskQueuePrivate, _PrivDelete> priv_{nullptr};
};

/**
 * @brief convience alias of shared pointer to MluTaskQueue
 */
using MluTaskQueue_t = std::shared_ptr<MluTaskQueue>;

/**
 * @brief MLU environment helper class
 */
class MluContext {
 public:
  /**
   * @brief Construct a new Mlu Context object
   */
  MluContext() = default;

  /**
   * @brief Construct a new Mlu Context object
   *
   * @param dev_id Device id
   */
  explicit MluContext(int dev_id) : dev_id_(dev_id) {}

  /**
   * @brief Get the device id
   *
   * @return Device id
   */
  inline int DeviceId() const { return dev_id_; }

  /**
   * @brief Set the device id
   *
   * @param id Device id
   */
  inline void SetDeviceId(int id) { dev_id_ = id; }

  /**
   * @brief Get available device number
   *
   * @return Available device number
   */
  static uint32_t GetDeviceNum();

  /**
   * @brief Check whether device exists
   *
   * @param id Device id
   * @return true if device exists, otherwise false returned
   */
  bool CheckDeviceId(int id);

  /**
   * @brief Get the MLU channel id
   * @deprecated
   *
   * @return MLU Channel id
   */
  attribute_deprecated inline int ChannelId() const { return channel_id_; }

  /**
   * @brief Set the MLU channel id in range [0, 3]
   * @deprecated
   *
   * @param id MLU channel id
   */
  attribute_deprecated inline void SetChannelId(int id) { channel_id_ = id; }

  /**
   * @brief Bind MLU device
   * @note Any process on MLU need to bind MLU device
   */
  void BindDevice();

  /**
   * @brief Get MLU core version
   *
   * @return MLU core version
   */
  CoreVersion GetCoreVersion();

 private:
  int dev_id_ = 0;
  int channel_id_ = -1;
};  // class MluContext

}  // namespace edk

#endif  // EDK_MLU_CONTEXT_H_
