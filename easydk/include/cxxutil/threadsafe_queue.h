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

#ifndef CXXUTIL_THREADSAFE_QUEUE_H_
#define CXXUTIL_THREADSAFE_QUEUE_H_

#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>

namespace edk {

/**
 * @brief encapsulated queue with threadsafe operation
 */
template <typename T>
class ThreadSafeQueue {
 public:
  ThreadSafeQueue() = default;
  ThreadSafeQueue(const ThreadSafeQueue& other) = delete;
  ThreadSafeQueue& operator=(const ThreadSafeQueue& other) = delete;

  /**
   * @brief try to pop an element from queue
   *
   * @param value reference to poped element
   * @retval true if succeeded
   * @retval false if failed
   */
  bool TryPop(T& value);  // NOLINT

  /**
   * @brief wait until queue is not empty, and pop an element
   *
   * @param value reference to poped element
   */
  void WaitAndPop(T& value);  // NOLINT

  /**
   * @brief wait until queue is not empty and pop an element, or timeout reached
   *
   * @param value reference to poped element
   * @param timeout maximum duration to block for
   * @retval true if succeeded
   * @retval false if timeout reached
   */
  bool WaitAndTryPop(T& value, const std::chrono::microseconds timeout); //NOLINT

  /**
   * @brief Push the given element value to the end of the queue
   *
   * @param new_value the value of the element to push
   */
  void Push(const T& new_value);

  /**
   * @brief Push the given element value to the end of the queue
   *
   * @param new_value the value of the element to push
   */
  void Push(T&& new_value);

  /**
   * @brief Check if the queue has no elements
   *
   * @retval true if the queue is empty
   * @retval false otherwise
   */
  bool Empty() {
    std::lock_guard<std::mutex> lk(data_m_);
    return q_.empty();
  }

  /**
   * @brief Returns the number of elements in the queue
   *
   * @return The number of elements in the queue
   */
  uint32_t Size() {
    std::lock_guard<std::mutex> lk(data_m_);
    return q_.size();
  }

 private:
  std::mutex data_m_;
  std::queue<T> q_;
  std::condition_variable notempty_cond_;
};

template <typename T>
bool ThreadSafeQueue<T>::TryPop(T& value) { //NOLINT
  std::lock_guard<std::mutex> lk(data_m_);
  if (q_.empty()) {
    return false;
  } else {
    value = q_.front();
    q_.pop();
    return true;
  }
}

template <typename T>
void ThreadSafeQueue<T>::WaitAndPop(T& value) { //NOLINT
  std::unique_lock<std::mutex> lk(data_m_);
  notempty_cond_.wait(lk, [&] { return !q_.empty(); });
  value = q_.front();
  q_.pop();
}

template <typename T>
bool ThreadSafeQueue<T>::WaitAndTryPop(T& value, const std::chrono::microseconds rel_time) { //NOLINT
  std::unique_lock<std::mutex> lk(data_m_);
  if (notempty_cond_.wait_for(lk, rel_time, [&] { return !q_.empty(); })) {
    value = q_.front();
    q_.pop();
    return true;
  } else {
    return false;
  }
}

template <typename T>
void ThreadSafeQueue<T>::Push(const T& new_value) {
  std::lock_guard<std::mutex> lk(data_m_);
  q_.push(new_value);
  notempty_cond_.notify_one();
}

template <typename T>
void ThreadSafeQueue<T>::Push(T&& new_value) {
  std::lock_guard<std::mutex> lk(data_m_);
  q_.push(std::move(new_value));
  notempty_cond_.notify_one();
}

}  // namespace edk
#endif  // CXXUTIL_THREADSAFE_QUEUE_H_
