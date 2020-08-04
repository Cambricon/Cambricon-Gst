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

#include "synced_memory.h"

#include <string>

#include "device/mlu_context.h"
#include "easyinfer/mlu_memory_op.h"

constexpr static uint32_t SYNCMEM_ERRMSG_LENGTH = 1024;

struct GstSyncedMemory
{
  void* host_ptr = nullptr; ///< CPU data pointer.
  void* dev_ptr = nullptr;  ///< MLU data pointer.

  /*
   * Allocates memory by GstSyncedMemory if true.
   */
  bool own_host_data = false; ///< Whether CPU data is allocated by GstSyncedMemory.
  bool own_dev_data = false;  ///< Whether MLU data is allocated by GstSyncedMemory.

  CNSyncHead head = GST_SYNCHEAD_UNINITIALIZED; ///< Identifies which device is the data synchronized on.
  size_t size;                                  ///< The data size.

  char err_msg[SYNCMEM_ERRMSG_LENGTH];

  edk::MluContext ctx;
  edk::MluMemoryOp mem_op;
}; // struct GstSyncedMemory

GstSyncedMemory_t
cn_syncedmem_new(size_t size)
{
  if (size) {
    auto mem = new GstSyncedMemory;
    mem->size = size;
    return mem;
  } else {
    return nullptr;
  }
}

bool
cn_syncedmem_free(GstSyncedMemory_t mem)
{
  if (mem->host_ptr && mem->own_host_data) {
    free(mem->host_ptr);
    mem->own_host_data = false;
  }
  if (mem->dev_ptr && mem->own_dev_data) {
    // set device id before call cnrt functions, or CNRT_RET_ERR_EXISTS will be returned from cnrt function
    try {
      mem->mem_op.FreeMlu(mem->dev_ptr);
    } catch (edk::Exception& e) {
      snprintf(mem->err_msg, SYNCMEM_ERRMSG_LENGTH, "%s", e.what());
      return false;
    }
    mem->own_dev_data = false;
  }
  delete mem;
  return true;
}

static inline void
to_cpu(GstSyncedMemory_t mem)
{
  switch (mem->head) {
    case GST_SYNCHEAD_UNINITIALIZED:
      mem->host_ptr = malloc(mem->size);
      mem->head = GST_SYNCHEAD_AT_CPU;
      mem->own_host_data = true;
      break;
    case GST_SYNCHEAD_AT_MLU:
      if (NULL == mem->host_ptr) {
        mem->host_ptr = malloc(mem->size);
        mem->own_host_data = true;
      }
      mem->mem_op.MemcpyD2H(mem->host_ptr, mem->dev_ptr, mem->size, 1);
      mem->head = GST_SYNCHEAD_SYNCED;
      break;
    case GST_SYNCHEAD_AT_CPU:
    case GST_SYNCHEAD_SYNCED:
      break;
  }
}

static inline void
to_mlu(GstSyncedMemory_t mem)
{
  switch (mem->head) {
    case GST_SYNCHEAD_UNINITIALIZED:
      mem->dev_ptr = mem->mem_op.AllocMlu(mem->size, 1);
      mem->head = GST_SYNCHEAD_AT_MLU;
      mem->own_dev_data = true;
      break;
    case GST_SYNCHEAD_AT_CPU:
      if (NULL == mem->dev_ptr) {
        mem->dev_ptr = mem->mem_op.AllocMlu(mem->size, 1);
        mem->own_dev_data = true;
      }
      mem->mem_op.MemcpyH2D(mem->dev_ptr, mem->host_ptr, mem->size, 1);
      mem->head = GST_SYNCHEAD_SYNCED;
      break;
    case GST_SYNCHEAD_AT_MLU:
    case GST_SYNCHEAD_SYNCED:
      break;
  }
}

const void*
cn_syncedmem_get_host_data(GstSyncedMemory_t mem)
{
  to_cpu(mem);
  return const_cast<const void*>(mem->host_ptr);
}

bool
cn_syncedmem_set_host_data(GstSyncedMemory_t mem, void* data)
{
  if (nullptr == data) {
    snprintf(mem->err_msg, SYNCMEM_ERRMSG_LENGTH, "data is NULL.");
    return false;
  }
  if (mem->own_host_data) {
    free(mem->host_ptr);
  }
  mem->host_ptr = data;
  mem->head = GST_SYNCHEAD_AT_CPU;
  mem->own_host_data = false;
  return true;
}

const void*
cn_syncedmem_get_dev_data(GstSyncedMemory_t mem)
{
  to_mlu(mem);
  return const_cast<const void*>(mem->dev_ptr);
}

bool
cn_syncedmem_set_dev_data(GstSyncedMemory_t mem, void* data)
{
  if (nullptr == data) {
    snprintf(mem->err_msg, SYNCMEM_ERRMSG_LENGTH, "data is NULL.");
    return false;
  }
  if (mem->own_dev_data) {
    try {
      mem->mem_op.FreeMlu(mem->dev_ptr);
    } catch (edk::Exception& e) {
      snprintf(mem->err_msg, SYNCMEM_ERRMSG_LENGTH, "%s", e.what());
      return false;
    }
  }
  mem->dev_ptr = data;
  mem->head = GST_SYNCHEAD_AT_MLU;
  mem->own_dev_data = false;
  return true;
}

bool
cn_syncedmem_set_device_context(GstSyncedMemory_t mem, int dev_id, int ddr_chn)
{
  if (!mem->ctx.CheckDeviceId(dev_id)) {
    snprintf(mem->err_msg, SYNCMEM_ERRMSG_LENGTH, "Cannot find device %d", dev_id);
    return false;
  }
  if (ddr_chn < 0 || ddr_chn >= 4) {
    snprintf(mem->err_msg, SYNCMEM_ERRMSG_LENGTH, "Invalid ddr channel [0,4) : %d", ddr_chn);
    return false;
  }

  mem->ctx.SetDeviceId(dev_id);
  mem->ctx.SetChannelId(ddr_chn);
  return true;
}

int
cn_syncedmem_get_dev_id(GstSyncedMemory_t mem)
{
  return mem->ctx.DeviceId();
}

int
cn_syncedmem_get_ddr_channel(GstSyncedMemory_t mem)
{
  return mem->ctx.ChannelId();
}

void*
cn_syncedmem_get_mutable_host_data(GstSyncedMemory_t mem)
{
  to_cpu(mem);
  return mem->host_ptr;
}

void*
cn_syncedmem_get_mutable_dev_data(GstSyncedMemory_t mem)
{
  to_mlu(mem);
  return mem->dev_ptr;
}

const char*
cn_syncedmem_get_last_errmsg(GstSyncedMemory_t mem)
{
  return mem->err_msg;
}

CNSyncHead
cn_syncedmem_get_head(GstSyncedMemory_t mem)
{
  return mem->head;
}

size_t
cn_syncedmem_get_size(GstSyncedMemory_t mem)
{
  return mem->size;
}
