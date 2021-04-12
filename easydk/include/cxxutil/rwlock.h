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
#ifndef EDK_CXXUTIL_RWLOCK_H_
#define EDK_CXXUTIL_RWLOCK_H_

#include <pthread.h>

#include "cxxutil/noncopy.h"

namespace edk {

// FIXME(dmh): pthread return code is not handle
/**
 * @brief Read Write lock based on pthread
 */
class RwLock : public NonCopyable {
 public:
  /**
   * @brief Construct a new RwLock object
   */
  RwLock() { pthread_rwlock_init(&rwlock_, NULL); }
  /**
   * @brief Destroy the RwLock object
   */
  ~RwLock() { pthread_rwlock_destroy(&rwlock_); }
  /**
   * @brief Lock with write access
   */
  void WriteLock() { pthread_rwlock_wrlock(&rwlock_); }
  /**
   * @brief Lock with read access
   */
  void ReadLock() { pthread_rwlock_rdlock(&rwlock_); }
  /**
   * @brief Unlock
   */
  void Unlock() { pthread_rwlock_unlock(&rwlock_); }

 private:
  pthread_rwlock_t rwlock_;
};

/**
 * @brief RAII guard of write lock
 */
class WriteLockGuard {
 public:
  /**
   * @brief Construct a new Write Lock Guard object, and lock write
   *
   * @param lock Read write lock
   */
  explicit WriteLockGuard(RwLock& lock) : lock_(lock) { lock_.WriteLock(); }
  /**
   * @brief Destroy the Write Lock Guard object, and unlock
   */
  ~WriteLockGuard() { lock_.Unlock(); }

 private:
  RwLock& lock_;
};

/**
 * @brief RAII guard of read lock
 */
class ReadLockGuard {
 public:
  /**
   * @brief Construct a new Read Lock Guard object, and lock read
   *
   * @param lock Read write lock
   */
  explicit ReadLockGuard(RwLock& lock) : lock_(lock) { lock_.ReadLock(); }
  /**
   * @brief Destroy the Read Lock Guard object, and unlock
   */
  ~ReadLockGuard() { lock_.Unlock(); }

 private:
  RwLock& lock_;
};

}  // namespace edk

#endif  // EDK_CXXUTIL_RWLOCK_H_
