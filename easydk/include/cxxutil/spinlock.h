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
 * @file spinlock.h
 *
 * This file contains a declaration of the SpinLock class, and helper class SpinLockGuard.
 */

#ifndef CXXUTIL_SPIN_LOCK_H_
#define CXXUTIL_SPIN_LOCK_H_

#include <pthread.h>
#include <atomic>

#include "cxxutil/noncopy.h"

namespace edk {

// FIXME(dmh): pthread return code is not handle
/**
 * @brief Spin lock implementation using pthread
 */
class SpinLock : public NonCopyable {
 public:
  /**
   * @brief Construct a new SpinLock object
   */
  SpinLock() { pthread_spin_init(&lock_, PTHREAD_PROCESS_PRIVATE); }

  /**
   * @brief Destroy the SpinLock object
   */
  ~SpinLock() { pthread_spin_destroy(&lock_); }

  /**
   * @brief Lock the spinlock
   */
  void Lock() { pthread_spin_lock(&lock_); }

  /**
   * @brief Unlock the spinlock
   */
  void Unlock() { pthread_spin_unlock(&lock_); }

 private:
  pthread_spinlock_t lock_;
};

/**
 * @brief Spin lock helper class, provide RAII management
 */
class SpinLockGuard {
 public:
  /**
   * Constructor, lock the spinlock in construction
   * @param lock Spin lock instance.
   */
  explicit SpinLockGuard(SpinLock &lock) : lock_(lock) {  // NOLINT
    lock_.Lock();
    is_locked.store(true, std::memory_order_release);
  }

  /**
   * @brief Lock the spinlock if have not been locked by this guard
   */
  void Lock() {
    if (!is_locked.load(std::memory_order_consume)) {
      lock_.Lock();
      is_locked.store(true, std::memory_order_release);
    }
  }

  /**
   * @brief Unlock the spinlock if have been locked by this guard
   */
  void Unlock() {
    if (is_locked.load(std::memory_order_consume)) {
      lock_.Unlock();
      is_locked.store(false, std::memory_order_release);
    }
  }

  /**
   * Destructor, unlock the spinlock in destruction
   */
  ~SpinLockGuard() {
    if (is_locked.load(std::memory_order_consume)) {
      lock_.Unlock();
    }
  }

 private:
  SpinLock &lock_;
  std::atomic<bool> is_locked{false};
};

}  // namespace edk

#endif  // CXXUTIL_SPIN_LOCK_H_
