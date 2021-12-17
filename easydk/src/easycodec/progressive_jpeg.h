/*************************************************************************
 * Copyright (C) [2021] by Cambricon, Inc. All rights reserved
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

#ifndef EASYCODEC_PROGRESSIVE_JPEG_H_
#define EASYCODEC_PROGRESSIVE_JPEG_H_

#include <cnrt.h>
#include <unordered_map>

#include "cxxutil/log.h"
#include "easycodec/easy_decode.h"
#include "format_info.h"
#ifdef ENABLE_TURBOJPEG
#include "cxxutil/threadsafe_queue.h"
extern "C" {
#include "turbojpeg.h"
}
#endif

#define ALIGN(size, alignment) (((u32_t)(size) + (alignment)-1) & ~((alignment)-1))

#define CALL_CNRT_FUNC(func, msg)                                                            \
  do {                                                                                       \
    int ret = (func);                                                                        \
    if (0 != ret) {                                                                          \
      LOGE(DECODE) << msg << " error code: " << ret;                                         \
      THROW_EXCEPTION(Exception::INTERNAL, msg " cnrt error code : " + std::to_string(ret)); \
    }                                                                                        \
  } while (0)

namespace edk {
namespace detail {

#ifdef ENABLE_TURBOJPEG
bool BGRToNV21(uint8_t* src, uint8_t* dst_y, int dst_y_stride, uint8_t* dst_uv, int dst_uv_stride, int width,
               int height);

bool BGRToNV12(uint8_t* src, uint8_t* dst_y, int dst_y_stride, uint8_t* dst_vu, int dst_vu_stride, int width,
               int height);
#endif

int CheckProgressiveMode(uint8_t* data, uint64_t length);
}  // namespace detail

#ifdef ENABLE_TURBOJPEG
class ProgressiveJpegDecoder {
 public:
  ProgressiveJpegDecoder(uint32_t width, uint32_t height, uint32_t stride, uint32_t output_buf_num, PixelFmt fmt,
                         int device_id)
      : fmt_(fmt), device_id_(device_id) {
    if (fmt != PixelFmt::NV12 && fmt != PixelFmt::NV21) {
      THROW_EXCEPTION(Exception::UNSUPPORTED, "Not support output type.");
    }
    auto fmt_info = FormatInfo::GetFormatInfo(fmt);
    uint64_t size = 0;
    uint32_t plane_num = 2;
    for (uint32_t j = 0; j < plane_num; ++j) {
      size += fmt_info->GetPlaneSize(stride, height, j);
    }
    for (size_t i = 0; i < output_buf_num; ++i) {
      void* mlu_ptr = nullptr;
      CALL_CNRT_FUNC(cnrtMalloc(reinterpret_cast<void**>(&mlu_ptr), size), "Malloc decode output buffer failed");
      memory_pool_map_[output_buf_num + i] = mlu_ptr;
      memory_ids_.Push(output_buf_num + i);
    }
    yuv_cpu_data_ = new uint8_t[stride * height * 3 / 2];
    bgr_cpu_data_ = new uint8_t[width * height * 3];
    tjinstance_ = tjInitDecompress();
  }

  ~ProgressiveJpegDecoder() {
    for (auto& iter : memory_pool_map_) {
      cnrtFree(iter.second);
    }
    tjDestroy(tjinstance_);
    if (yuv_cpu_data_) {
      delete[] yuv_cpu_data_;
    }
    if (bgr_cpu_data_) {
      delete[] bgr_cpu_data_;
    }
  }

  CnFrame Decode(const CnPacket& packet) {
    int jpegSubsamp, width, height;
    tjDecompressHeader2(tjinstance_, reinterpret_cast<uint8_t*>(packet.data), packet.length, &width, &height,
                        &jpegSubsamp);
    tjDecompress2(tjinstance_, reinterpret_cast<uint8_t*>(packet.data), packet.length, bgr_cpu_data_, width,
                  0 /*pitch*/, height, TJPF_RGB, TJFLAG_FASTDCT);
    int y_stride = ALIGN(width, 128);
    int uv_stride = ALIGN(width, 128);
    uint64_t data_length = height * y_stride * 3 / 2;
    if (fmt_ == PixelFmt::NV21) {
      detail::BGRToNV21(bgr_cpu_data_, yuv_cpu_data_, y_stride, yuv_cpu_data_ + height * y_stride, uv_stride, width,
                        height);
    } else if (fmt_ == PixelFmt::NV12) {
      detail::BGRToNV12(bgr_cpu_data_, yuv_cpu_data_, y_stride, yuv_cpu_data_ + height * y_stride, uv_stride, width,
                        height);
    } else {
      LOGF(DECODE) << "Not support output pixel format " << FormatInfo::GetFormatInfo(fmt_)->fmt_str;
    }

    size_t buf_id;
    memory_ids_.TryPop(buf_id);  // get one available buffer
    void* mlu_ptr = memory_pool_map_[buf_id];
    CALL_CNRT_FUNC(cnrtMemcpy(mlu_ptr, yuv_cpu_data_, data_length, CNRT_MEM_TRANS_DIR_HOST2DEV), "Memcpy failed");

    // 2. config CnFrame for user callback.
    CnFrame finfo;
    finfo.pts = packet.pts;
    finfo.cpu_decode = true;
    finfo.device_id = device_id_;
    finfo.buf_id = buf_id;
    finfo.width = width;
    finfo.height = height;
    finfo.n_planes = 2;
    finfo.frame_size = height * y_stride * 3 / 2;
    finfo.strides[0] = y_stride;
    finfo.strides[1] = uv_stride;
    finfo.ptrs[0] = mlu_ptr;
    finfo.ptrs[1] = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(mlu_ptr) + height * y_stride);
    finfo.pformat = fmt_;

    LOGT(DECODE) << "Frame: width " << finfo.width << " height " << finfo.height << " planes " << finfo.n_planes
                 << " frame size " << finfo.frame_size;
    return finfo;
  }

  bool ReleaseBuffer(uint64_t buf_id) {
    if (memory_pool_map_.find(buf_id) != memory_pool_map_.end()) {
      memory_ids_.Push(buf_id);
      return true;
    }
    return false;
  }

 private:
  std::unordered_map<uint64_t, void*> memory_pool_map_;
  ThreadSafeQueue<uint64_t> memory_ids_;
  tjhandle tjinstance_;
  uint8_t* yuv_cpu_data_ = nullptr;
  uint8_t* bgr_cpu_data_ = nullptr;
  PixelFmt fmt_;
  int device_id_;
};
#endif

}  // namespace edk

#endif  // EASYCODEC_PROGRESSIVE_JPEG_H_
