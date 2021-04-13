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
 * @file easy_decode.h
 *
 * This file contains a declaration of the EasyEncode class and involved structures.
 */

#ifndef EASYCODEC_EASY_DECODE_H_
#define EASYCODEC_EASY_DECODE_H_

#include <functional>
#include <memory>

#include "cxxutil/edk_attribute.h"
#include "cxxutil/exception.h"
#include "easycodec/vformat.h"

namespace edk {

/**
 * @brief Decode packet callback function type
 * @param CnFrame Frame containing decoded frame information
 */
using DecodeFrameCallback = std::function<void(const CnFrame&)>;

/// Decode EOS callback function type
using DecodeEOSCallback = std::function<void()>;

class DecodeHandler;

/**
 * @brief Easy decode class, provide a fast and easy API to decode on MLU platform
 */
class EasyDecode {
 public:
  /**
   * @brief params for creating EasyDecode
   */
  struct Attr {
    /// The frame resolution that this decoder can handle.
    Geometry frame_geometry;

    /// Video codec type
    CodecType codec_type;

    /// The pixel format of output frames.
    PixelFmt pixel_format;

    /// Color space standard
    ColorStd color_std = ColorStd::ITU_BT_709;

    /// The input buffer count
    uint32_t input_buffer_num = 2;

    /// The output buffer count.
    uint32_t output_buffer_num = 3;

    /// Interlaced data or progressive data
    bool interlaced = false;

    /// Frame callback
    DecodeFrameCallback frame_callback = NULL;

    /// EOS callback
    DecodeEOSCallback eos_callback = NULL;

    /// whether to print useful messages.
    bool silent = false;

    /// Create decoder on which device
    int dev_id = 0;

    /// Set align value (2^n = 1,4,8...), MLU270: 1 -- MLU220 scalar: 128
    /// @note only work on video decode, JPEG stride align is fixed to 64
    int stride_align = 1;
  };  // struct Attr

  /**
   * @brief Decoder status enumeration
   */
  enum class Status {
    RUNNING,  ///< running, SendData and Callback are active.
    PAUSED,   ///< pause, SendData and Callback are blocked.
    STOP,     ///< stopped, decoder was destroied.
    EOS       ///< received eos.
  };          // Enum Status

  /**
   * @brief Create decoder by attr. Throw a Exception while error encountered.
   * @param attr Decoder attribute description
   * @attention status is RUNNING after object be constructed.
   * @return Pointer to new decoder instance
   */
  static std::unique_ptr<EasyDecode> New(const Attr& attr) noexcept(false);

  /**
   * @brief Get the decoder instance attribute
   * @return Decoder attribute
   */
  Attr GetAttr() const;

  /**
   * @brief Get current state of decoder
   * @return Decoder status
   */
  Status GetStatus() const;

  /**
   * @brief Pause the decode process
   */
  bool Pause();

  /**
   * @brief Resume the decode process
   */
  bool Resume();

  /**
   * @brief Abort decoder instance at once
   * @note aborted decoder instance cannot be used any more
   */
  void AbortDecoder();

  /**
   * @brief Send data to decoder, block when STATUS is pause.
   *        An Exception is thrown when send data failed.
   *
   * @deprecated use `bool FeedData(const CnPacket&, bool)` and `bool FeedEos()` instead
   * @param packet bytestream data
   * @param eos indicate whether this packet is end-of-stream
   * @param integral_frame indicate whether packet contain an integral frame
   *
   * @return return false when STATUS is not UNINITIALIZED or STOP.
   */
  attribute_deprecated bool SendData(const CnPacket& packet, bool eos = false,
                                     bool integral_frame = false) noexcept(false);

  /**
   * @brief Send data to decoder, block when STATUS is pause.
   *        An Exception is thrown when send data failed.
   *
   * @param packet bytestream data
   * @param integral_frame indicate whether packet contain an integral frame
   *
   * @retval true Feed data succeed
   * @retval false otherwise
   */
  bool FeedData(const CnPacket& packet, bool integral_frame = true) noexcept(false);

  /**
   * @brief Send EOS to decoder, block when STATUS is pause.
   *        An Exception is thrown when send EOS failed.
   *
   * @retval false if an EOS has been sent
   * @retval true otherwise.
   */
  bool FeedEos() noexcept(false);

  /**
   * @brief Release decoder's buffer.
   * @note Release decoder buffer While buffer content will not be used, or decoder may be blocked.
   * @param buf_id Codec buffer id.
   */
  void ReleaseBuffer(uint64_t buf_id);

  /**
   * @brief copy frame from device to host.
   * @param dst copy destination
   * @param frame Frame you want to copy
   * @return when error occurs, return false.
   */
  bool CopyFrameD2H(void* dst, const CnFrame& frame);

  /**
   * @brief Gets the minimum decode output buffer count.
   * @return Return the minimum decode output buffer count.
   *
   * @note It only returns right value after received the first frame.
   **/
  int GetMinimumOutputBufferCount() const;

  /**
   * @brief Destroy the Easy Decode object
   */
  ~EasyDecode();

  friend class DecodeHandler;

 private:
  explicit EasyDecode(const Attr& attr);
  EasyDecode(const EasyDecode&) = delete;
  EasyDecode& operator=(const EasyDecode&) = delete;
  EasyDecode(EasyDecode&&) = delete;
  EasyDecode& operator=(EasyDecode&&) = delete;

  DecodeHandler* handler_ = nullptr;
};  // class EasyDecode

}  // namespace edk

#endif  // EASYCODEC_EASY_DECODE_H_
