/*********************************************************************************************************
 * All modification made by Cambricon Corporation: © 2018--2019 Cambricon Corporation
 * All rights reserved.
 * All other contributions:
 * Copyright (c) 2014--2018, the respective contributors
 * All rights reserved.
 * For the list of contributors go to https://github.com/BVLC/caffe/blob/master/CONTRIBUTORS.md
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Intel Corporation nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************************************************/

#ifndef GST_SYNCED_MEMORY_H_
#define GST_SYNCED_MEMORY_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>

  /**
 * @file gst_synced_memory.h
 *
 * This file contains a declaration of the SyncedMemory class.
 */

  /**
 * Synced head.
 */
  enum CNSyncHead
  {
    GST_SYNCHEAD_UNINITIALIZED, ///< The memory has not been allocated.
    GST_SYNCHEAD_AT_CPU,        ///< The data has been updated to CPU but has not been synced to MLU yet.
    GST_SYNCHEAD_AT_MLU,        ///< The data has been updated to MLU but has not been synced to CPU yet.
    GST_SYNCHEAD_SYNCED         ///< The data has been synced to both CPU and MLU.
  };

  /**
 * @struct GstSyncedMemory
 *
 * GstSyncedMemory used to sync memory between CPU and MLU.
 */
  struct GstSyncedMemory;
  typedef struct GstSyncedMemory* GstSyncedMemory_t;

  /**
 * Construct a new GstSyncedMemory instance.
 *
 * @param size The size of the memory.
 * @return a new GstSyncedMemory.
 */
  GstSyncedMemory_t cn_syncedmem_new(size_t size);

  /**
 * Destruct a GstSyncedMemory instance.
 * 
 * @param mem A GstSyncedMemory.
 * @return false if error occured, true otherwise.
 */
  bool cn_syncedmem_free(GstSyncedMemory_t mem);

  /**
 * Gets the host data.
 *
 * @return the host data pointer.
 * @note If the size is 0, always returns nullptr.
 */
  const void* cn_syncedmem_get_host_data(GstSyncedMemory_t mem);

  /**
 * Sets the host data.
 *
 * @param mem A GstSyncedMemory.
 * @param data The data pointer on host.
 * @return false if error occured, true otherwise.
 */
  bool cn_syncedmem_set_host_data(GstSyncedMemory_t mem, void* data);

  /**
 * Gets the device data.
 *
 * @param mem A GstSyncedMemory.
 * @return the device data pointer.
 * @note If the size is 0, always returns nullptr.
 */
  const void* cn_syncedmem_get_dev_data(GstSyncedMemory_t mem);

  /**
 * Sets the device data.
 *
 * @param mem A GstSyncedMemory.
 * @param data The data pointer on dev
 * @return false if error occured, true otherwise.
 */
  bool cn_syncedmem_set_dev_data(GstSyncedMemory_t mem, void* data);

  /**
 * Sets the device context
 *
 * @param mem A GstSyncedMemory.
 * @param dev_id MLU device id that is incremented from 0.
 * @param ddr_chn MLU DDR channel id that is greater than or equal to 0, and less than
 *                4. It specifies which piece of DDR the memory allocated on.
 * @return false if error occured, true otherwise.
 * 
 * @note Do this before all getter and setter.
 */
  bool cn_syncedmem_set_device_context(GstSyncedMemory_t mem, int dev_id, int ddr_chn);

  /**
 * Gets the MLU device id.
 *
 * @param mem A GstSyncedMemory.
 * @return the device that the MLU memory allocated on.
 */
  int cn_syncedmem_get_dev_id(GstSyncedMemory_t mem);

  /**
 * Gets the channel id of the MLU DDR.
 *
 * @param mem A GstSyncedMemory.
 * @return the DDR channel of the MLU memory allocated on.
 */
  int cn_syncedmem_get_ddr_channel(GstSyncedMemory_t mem);

  /**
 * Gets the mutable host data.
 *
 * @param mem A GstSyncedMemory.
 * @return the host data pointer.
 */
  void* cn_syncedmem_get_mutable_host_data(GstSyncedMemory_t mem);

  /**
 * Gets the mutable device data.
 *
 * @param mem A GstSyncedMemory.
 * @return the device data pointer.
 */
  void* cn_syncedmem_get_mutable_dev_data(GstSyncedMemory_t mem);

  /**
 * Gets the last error message.
 * 
 * @param mem A GstSyncedMemory.
 * @return the error message.
 */
  const char* cn_syncedmem_get_last_errmsg(GstSyncedMemory_t mem);

  /**
 * Gets synced head.
 *
 * @param mem A GstSyncedMemory.
 * @return synced head.
 */
  enum CNSyncHead cn_syncedmem_get_head(GstSyncedMemory_t mem);

  /**
 * Gets data bytes.
 *
 * @param mem A GstSyncedMemory.
 * @return returns data bytes.
 */
  size_t cn_syncedmem_get_size(GstSyncedMemory_t mem);

#ifdef __cplusplus
}
#endif

#endif // GST_SYNCMEM_HPP_
