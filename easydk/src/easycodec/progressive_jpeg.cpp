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

#include "progressive_jpeg.h"

#include "cxxutil/log.h"

#ifdef ENABLE_TURBOJPEG
extern "C" {
#include "libyuv.h"
}
#endif

namespace edk {
namespace detail {

#ifdef ENABLE_TURBOJPEG
bool BGRToNV21(uint8_t* src, uint8_t* dst_y, int dst_y_stride, uint8_t* dst_uv, int dst_uv_stride, int width,
               int height) {
  int i420_stride_y = width;
  int i420_stride_u = width / 2;
  int i420_stride_v = width / 2;
  uint8_t* i420 = new uint8_t[width * height * 3 / 2];
  // clang-format off
  libyuv::RGB24ToI420(src, width * 3,
                      i420, i420_stride_y,
                      i420 + width * height, i420_stride_u,
                      i420 + width * height * 5 / 4, i420_stride_v,
                      width, height);

  libyuv::I420ToNV21(i420, i420_stride_y,
                     i420 + width * height, i420_stride_u,
                     i420 + width * height * 5 / 4, i420_stride_v,
                     dst_y, dst_y_stride,
                     dst_uv, dst_uv_stride,
                     width, height);
  // clang-format on
  delete[] i420;
  return true;
}

bool BGRToNV12(uint8_t* src, uint8_t* dst_y, int dst_y_stride, uint8_t* dst_vu, int dst_vu_stride, int width,
               int height) {
  int i420_stride_y = width;
  int i420_stride_u = width / 2;
  int i420_stride_v = width / 2;
  uint8_t* i420 = new uint8_t[width * height * 3 / 2];
  // clang-format off
  libyuv::RGB24ToI420(src, width * 3,
                      i420, i420_stride_y,
                      i420 + width * height, i420_stride_u,
                      i420 + width * height * 5 / 4, i420_stride_v,
                      width, height);


  libyuv::I420ToNV12(i420, i420_stride_y,
                     i420 + width * height, i420_stride_u,
                     i420 + width * height * 5 / 4, i420_stride_v,
                     dst_y, dst_y_stride,
                     dst_vu, dst_vu_stride,
                     width, height);
  // clang-format on
  delete[] i420;
  return true;
}
#endif

int CheckProgressiveMode(uint8_t* data, uint64_t length) {
  static constexpr uint16_t kJPEG_HEADER = 0xFFD8;
  uint64_t i = 0;
  uint16_t header = (data[i] << 8) | data[i + 1];
  if (header != kJPEG_HEADER) {
    LOGE(DECODE) << "Not Support image format, header is: " << header;
    return -1;
  }
  i = i + 2;  // jump jpeg header
  while (i < length) {
    uint16_t seg_header = (data[i] << 8) | data[i + 1];
    if (seg_header == 0xffc2 || seg_header == 0xffca) {
      return 1;
    }
    uint16_t step = (data[i + 2] << 8) | data[i + 3];
    i += 2;     // jump seg header
    i += step;  // jump whole seg
  }
  return 0;
}

}  // namespace detail
}  // namespace edk

