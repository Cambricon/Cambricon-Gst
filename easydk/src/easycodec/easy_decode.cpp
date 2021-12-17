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

#include "easycodec/easy_decode.h"

#include <cn_codec_common.h>
#include <cn_jpeg_dec.h>
#include <cn_video_dec.h>
#include <cnrt.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

#include "cxxutil/log.h"
#include "format_info.h"
#include "vpu_turbo_table.h"

#include "progressive_jpeg.h"

using std::mutex;
using std::string;
using std::to_string;
using std::unique_lock;

#define ALIGN(size, alignment) (((u32_t)(size) + (alignment)-1) & ~((alignment)-1))

// cncodec add version macro since v1.6.0
#ifndef CNCODEC_VERSION
#define CNCODEC_VERSION 0
#endif

namespace edk {

static void PrintCreateAttr(cnvideoDecCreateInfo* p_attr) {
  printf("%-32s%s\n", "param", "value");
  printf("-------------------------------------\n");
  printf("%-32s%u\n", "Codectype", p_attr->codec);
  printf("%-32s%u\n", "Instance", p_attr->instance);
  printf("%-32s%u\n", "DeviceID", p_attr->deviceId);
  printf("%-32s%u\n", "PixelFormat", p_attr->pixelFmt);
  printf("%-32s%u\n", "Progressive", p_attr->progressive);
  printf("%-32s%u\n", "Width", p_attr->width);
  printf("%-32s%u\n", "Height", p_attr->height);
  printf("%-32s%u\n", "OutputBufferNum", p_attr->outputBufNum);
  printf("-------------------------------------\n");
}

static void PrintCreateAttr(cnjpegDecCreateInfo* p_attr) {
  printf("%-32s%s\n", "param", "value");
  printf("-------------------------------------\n");
  printf("%-32s%u\n", "Instance", p_attr->instance);
  printf("%-32s%u\n", "DeviceID", p_attr->deviceId);
  printf("%-32s%u\n", "PixelFormat", p_attr->pixelFmt);
  printf("%-32s%u\n", "Width", p_attr->width);
  printf("%-32s%u\n", "Height", p_attr->height);
  printf("%-32s%u\n", "OutputBufferNum", p_attr->outputBufNum);
  printf("%-32s%u\n", "InputBufferSize", p_attr->suggestedLibAllocBitStrmBufSize);
  printf("-------------------------------------\n");
}

class DecodeHandler {
 public:
  explicit DecodeHandler(EasyDecode* decoder);
  ~DecodeHandler();

  void InitVideoDecode(const EasyDecode::Attr& attr);
  void InitJpegDecode(const EasyDecode::Attr& attr);

  void DecodeProgressiveJpeg(const CnPacket& packet);
  void FeedJpegData(const CnPacket& packet, int progressive_mode);
  void FeedVideoData(const CnPacket& packet, bool integral_frame);
  bool FeedEos();

  void AbortDecoder();

  void ReceiveEvent(cncodecCbEventType type);
  void ReceiveFrame(void* out);
  int ReceiveSequence(cnvideoDecSequenceInfo* info);
  void ReceiveEOS();
  void ReleaseBuffer(uint64_t buf_id);

  friend class EasyDecode;

 private:
  void EventTaskRunner();
  uint32_t SetVpuTimestamp(uint64_t pts);
  bool GetVpuTimestamp(uint32_t key, uint64_t *pts);

  // event handle context
  std::queue<cncodecCbEventType> event_queue_;
  std::mutex event_mtx_;
  std::condition_variable event_cond_;
  std::thread event_loop_;

  EasyDecode* decoder_ = nullptr;
  // cncodec handle
  void* handle_ = nullptr;

  EasyDecode::Attr attr_;
  cnvideoDecCreateInfo vparams_;
  cnjpegDecCreateInfo jparams_;
  const FormatInfo* pixel_fmt_info_;

  uint32_t packets_count_ = 0;
  uint32_t frames_count_ = 0;
  int minimum_buf_cnt_ = 0;

#ifdef ENABLE_TURBOJPEG
  std::unique_ptr<ProgressiveJpegDecoder> progressive_jpeg_decoder_{nullptr};
#endif

  std::atomic<EasyDecode::Status> status_{EasyDecode::Status::RUNNING};

  /// eos workarround
  std::mutex eos_mtx_;
  std::condition_variable eos_cond_;
  bool send_eos_ = false;
  bool got_eos_ = false;
  bool jpeg_decode_ = false;

  // For m200 vpu-decoder, m200 vpu-codec does not 64bits timestamp, we have to implement it.
  uint32_t pts_key_ = 0;
  std::unordered_map<uint32_t, uint64_t> vpu_pts_map_;
  std::mutex pts_map_mtx_;
};  // class DecodeHandler

static i32_t EventHandler(cncodecCbEventType type, void* user_data, void* package) {
  auto handler = reinterpret_cast<DecodeHandler*>(user_data);
  // [ACQUIRED BY CNCODEC]
  // NEW_FRAME and SEQUENCE event must handled in callback thread,
  // The other events must handled in a different thread.
  if (handler != nullptr) {
    switch (type) {
      case CNCODEC_CB_EVENT_NEW_FRAME:
        handler->ReceiveFrame(package);
        break;
      case CNCODEC_CB_EVENT_SEQUENCE:
        handler->ReceiveSequence(reinterpret_cast<cnvideoDecSequenceInfo*>(package));
        break;
      default:
        handler->ReceiveEvent(type);
        break;
    }
  }
  return 0;
}

DecodeHandler::DecodeHandler(EasyDecode* decoder) : decoder_(decoder) {
  event_loop_ = std::thread(&DecodeHandler::EventTaskRunner, this);
}

DecodeHandler::~DecodeHandler() {
  try {
    /**
     * Decode destroied. status set to STOP.
     */
    status_.store(EasyDecode::Status::STOP);
    /**
     * Release resources.
     */
    unique_lock<mutex> eos_lk(eos_mtx_);
    if (!got_eos_) {
      if (!send_eos_ && handle_) {
        eos_lk.unlock();
        LOGI(DECODE) << "Send EOS in destruct";
        decoder_->FeedEos();
      } else {
        if (!handle_) got_eos_ = true;
      }
    }

    if (!eos_lk.owns_lock()) {
      eos_lk.lock();
    }

    if (!got_eos_) {
      LOGI(DECODE) << "Wait EOS in destruct";
      eos_cond_.wait(eos_lk, [this]() -> bool { return got_eos_; });
    }

    event_cond_.notify_all();
    event_loop_.join();

    if (handle_) {
      if (jpeg_decode_) {
        // Destroy jpu decoder
        LOGI(DECODE) << "Destroy jpeg decoder channel";
        auto ecode = cnjpegDecDestroy(handle_);
        if (CNCODEC_SUCCESS != ecode) {
          LOGE(DECODE) << "Decoder destroy failed Error Code: " << ecode;
        }
      } else {
        // destroy vpu decoder
        LOGI(DECODE) << "Stop video decoder channel";
        auto ecode = cnvideoDecStop(handle_);
        if (CNCODEC_SUCCESS != ecode) {
          LOGE(DECODE) << "Decoder stop failed Error Code: " << ecode;
        }

        LOGI(DECODE) << "Destroy video decoder channel";
        ecode = cnvideoDecDestroy(handle_);
        if (CNCODEC_SUCCESS != ecode) {
          LOGE(DECODE) << "Decoder destroy failed Error Code: " << ecode;
        }
      }
      handle_ = nullptr;
    }
  } catch (Exception& e) {
    LOGE(DECODE) << e.what();
  }
}

void DecodeHandler::ReceiveEvent(cncodecCbEventType type) {
  std::lock_guard<std::mutex> lock(event_mtx_);
  event_queue_.push(type);
  event_cond_.notify_one();
}

void DecodeHandler::EventTaskRunner() {
  unique_lock<std::mutex> lock(event_mtx_);
  while (!event_queue_.empty() || !got_eos_) {
    event_cond_.wait(lock, [this] { return !event_queue_.empty() || got_eos_; });

    if (event_queue_.empty()) {
      // notified by eos
      continue;
    }

    cncodecCbEventType type = event_queue_.front();
    event_queue_.pop();
    lock.unlock();

    switch (type) {
      case CNCODEC_CB_EVENT_EOS:
        ReceiveEOS();
        break;
      case CNCODEC_CB_EVENT_SW_RESET:
      case CNCODEC_CB_EVENT_HW_RESET:
        LOGE(DECODE) << "Decode firmware crash event: " << type;
        AbortDecoder();
        break;
      case CNCODEC_CB_EVENT_OUT_OF_MEMORY:
        LOGE(DECODE) << "Out of memory error thrown from cncodec";
        AbortDecoder();
        break;
      case CNCODEC_CB_EVENT_ABORT_ERROR:
        LOGE(DECODE) << "Abort error thrown from cncodec";
        AbortDecoder();
        break;
#if CNCODEC_VERSION >= 10600
      case CNCODEC_CB_EVENT_STREAM_CORRUPT:
        LOGW(DECODE) << "Stream corrupt, discard frame";
        break;
#endif
      default:
        LOGE(DECODE) << "Unknown event type";
        AbortDecoder();
        break;
    }

    lock.lock();
  }
}

void DecodeHandler::AbortDecoder() {
  LOGW(DECODE) << "Abort decoder";
  if (handle_) {
    if (jpeg_decode_) {
      cnjpegDecAbort(handle_);
    } else {
      cnvideoDecAbort(handle_);
    }
    handle_ = nullptr;
    status_.store(EasyDecode::Status::EOS);
    if (attr_.eos_callback) {
      attr_.eos_callback();
    }

    unique_lock<mutex> eos_lk(eos_mtx_);
    got_eos_ = true;
    eos_cond_.notify_one();
  } else {
    LOGE(DECODE) << "Won't do abort, since cndecode handler has not been initialized";
  }
}

uint32_t DecodeHandler::SetVpuTimestamp(uint64_t pts) {
  std::lock_guard<std::mutex> lock(pts_map_mtx_);
  uint32_t key = pts_key_++;
  vpu_pts_map_[key] = pts;
  return key;
}

bool DecodeHandler::GetVpuTimestamp(uint32_t key, uint64_t *pts) {
  std::lock_guard<std::mutex> lock(pts_map_mtx_);
  auto iter = vpu_pts_map_.find(key);
  if (iter != vpu_pts_map_.end()) {
    if (pts) {
      *pts = iter->second;
      vpu_pts_map_.erase(iter);
      return true;
    }
    vpu_pts_map_.erase(iter);
    return false;
  }
  return false;
}

void DecodeHandler::InitJpegDecode(const EasyDecode::Attr& attr) {
  attr_ = attr;
  jpeg_decode_ = true;
  pixel_fmt_info_ = FormatInfo::GetFormatInfo(attr.pixel_format);

  memset(&jparams_, 0, sizeof(cnjpegDecCreateInfo));
  jparams_.deviceId = attr.dev_id;
  jparams_.instance = CNJPEGDEC_INSTANCE_AUTO;
  jparams_.pixelFmt = pixel_fmt_info_->cncodec_fmt;
  jparams_.colorSpace = ColorStdCast(attr.color_std);
  jparams_.width = attr.frame_geometry.w;
  jparams_.height = attr.frame_geometry.h;
  jparams_.inputBufNum = attr.input_buffer_num;
  jparams_.outputBufNum = attr.output_buffer_num;
  jparams_.bitDepthMinus8 = 0;
  jparams_.allocType = CNCODEC_BUF_ALLOC_LIB;
  jparams_.userContext = reinterpret_cast<void*>(this);
  jparams_.suggestedLibAllocBitStrmBufSize = (4U << 20);
  jparams_.enablePreparse = 0;
  if (!attr.silent) {
    PrintCreateAttr(&jparams_);
  }
  int ecode = cnjpegDecCreate(&handle_, CNJPEGDEC_RUN_MODE_ASYNC, &EventHandler, &jparams_);
  if (0 != ecode) {
    THROW_EXCEPTION(Exception::INIT_FAILED, "Create jpeg decode failed: " + to_string(ecode));
  }
}

void DecodeHandler::InitVideoDecode(const EasyDecode::Attr& attr) {
  attr_ = attr;
  // 1. decoder create parameters.
  jpeg_decode_ = false;
  pixel_fmt_info_ = FormatInfo::GetFormatInfo(attr.pixel_format);

  memset(&vparams_, 0, sizeof(cnvideoDecCreateInfo));
  vparams_.deviceId = attr.dev_id;
  if (const char* turbo_env_p = std::getenv("VPU_TURBO_MODE")) {
    LOGI(DECODE) << "VPU Turbo mode : " << turbo_env_p;
    static std::mutex vpu_instance_mutex;
    std::unique_lock<std::mutex> lk(vpu_instance_mutex);
    static int _vpu_inst_cnt = 0;
    vparams_.instance = g_vpudec_instances[_vpu_inst_cnt++ % 100];
  } else {
    vparams_.instance = CNVIDEODEC_INSTANCE_AUTO;
  }
  vparams_.codec = CodecTypeCast(attr.codec_type);
  vparams_.pixelFmt = pixel_fmt_info_->cncodec_fmt;
  vparams_.colorSpace = ColorStdCast(attr.color_std);
  vparams_.width = attr.frame_geometry.w;
  vparams_.height = attr.frame_geometry.h;
  vparams_.bitDepthMinus8 = attr.pixel_format == PixelFmt::P010 ? 2 : 0;
  vparams_.progressive = attr.interlaced ? 0 : 1;
  vparams_.inputBufNum = attr.input_buffer_num;
  vparams_.outputBufNum = attr.output_buffer_num;
  vparams_.allocType = CNCODEC_BUF_ALLOC_LIB;
  vparams_.userContext = reinterpret_cast<void*>(this);

  if (!attr.silent) {
    PrintCreateAttr(&vparams_);
  }

  int ecode = cnvideoDecCreate(&handle_, &EventHandler, &vparams_);
  if (0 != ecode) {
    THROW_EXCEPTION(Exception::INIT_FAILED, "Create video decode failed: " + to_string(ecode));
  }

  ecode = cnvideoDecSetAttributes(handle_, CNVIDEO_DEC_ATTR_OUT_BUF_ALIGNMENT, &(attr_.stride_align));
  if (CNCODEC_SUCCESS != ecode) {
    THROW_EXCEPTION(Exception::INIT_FAILED, "cnvideo decode set attributes faild: " + to_string(ecode));
  }
}

void DecodeHandler::ReceiveFrame(void* out) {
  // config CnFrame for user callback.
  CnFrame finfo;
  cncodecFrame* frame = nullptr;
  if (jpeg_decode_) {
    auto o = reinterpret_cast<cnjpegDecOutput*>(out);
    finfo.pts = o->pts;
    frame = &o->frame;
    LOGT(DECODE) << "Receive one jpeg frame, " << frame;
  } else {
    auto o = reinterpret_cast<cnvideoDecOutput*>(out);
    uint64_t usr_pts;
    if (GetVpuTimestamp(o->pts, &usr_pts)) {
      finfo.pts = usr_pts;
    } else {
      // need to return, if GetVpuTimestamp failed?
      LOGW(DECODE) << "Failed to query timetamp,"
                   << ", use timestamp from vpu-decoder:" << o->pts;
    }
    frame = &o->frame;
    LOGT(DECODE) << "Receive one video frame, " << frame;
  }
  if (frame->width == 0 || frame->height == 0 || frame->planeNum == 0) {
    LOGW(DECODE) << "Receive empty frame";
    return;
  }
  finfo.device_id = attr_.dev_id;
  finfo.channel_id = frame->channel;
  finfo.buf_id = reinterpret_cast<uint64_t>(frame);
  finfo.width = frame->width;
  finfo.height = frame->height;
  finfo.n_planes = frame->planeNum;
  finfo.frame_size = 0;
  for (uint32_t pi = 0; pi < frame->planeNum; ++pi) {
    finfo.strides[pi] = frame->stride[pi];
    finfo.ptrs[pi] = reinterpret_cast<void*>(frame->plane[pi].addr);
    finfo.frame_size += pixel_fmt_info_->GetPlaneSize(frame->stride[pi], frame->height, pi);
  }
  finfo.pformat = attr_.pixel_format;
  finfo.color_std = attr_.color_std;

  LOGT(DECODE) << "Frame: width " << finfo.width << " height " << finfo.height << " planes " << finfo.n_planes
               << " frame size " << finfo.frame_size;

  if (NULL != attr_.frame_callback) {
    LOGD(DECODE) << "Add decode buffer Reference " << finfo.buf_id;
    if (jpeg_decode_) {
      cnjpegDecAddReference(handle_, frame);
    } else {
      cnvideoDecAddReference(handle_, frame);
    }
    attr_.frame_callback(finfo);
    frames_count_++;
  }
}

int DecodeHandler::ReceiveSequence(cnvideoDecSequenceInfo* info) {
  LOGI(DECODE) << "Receive sequence";

  vparams_.codec = info->codec;
  vparams_.pixelFmt = pixel_fmt_info_->cncodec_fmt;
  vparams_.width = info->width;
  vparams_.height = info->height;
  minimum_buf_cnt_ = info->minOutputBufNum;

  if (info->minInputBufNum > vparams_.inputBufNum) {
    vparams_.inputBufNum = info->minInputBufNum;
  }
  if (info->minOutputBufNum > vparams_.outputBufNum) {
    vparams_.outputBufNum = info->minOutputBufNum;
  }

  vparams_.userContext = reinterpret_cast<void*>(this);

  int ecode = cnvideoDecStart(handle_, &vparams_);
  if (ecode != CNCODEC_SUCCESS) {
    LOGE(DECODE) << "Start Decoder failed.";
    return -1;
  }
  return 0;
}

void DecodeHandler::ReceiveEOS() {
  LOGI(DECODE) << "Thread id: " << std::this_thread::get_id() << ",Received EOS from cncodec";

  status_.store(EasyDecode::Status::EOS);
  if (attr_.eos_callback) {
    attr_.eos_callback();
  }

  unique_lock<mutex> eos_lk(eos_mtx_);
  got_eos_ = true;
  eos_cond_.notify_one();
}

#ifdef ENABLE_TURBOJPEG
void DecodeHandler::DecodeProgressiveJpeg(const CnPacket& packet) {
  if (!progressive_jpeg_decoder_) {
    const size_t stride = ALIGN(attr_.frame_geometry.w, 128);
    progressive_jpeg_decoder_.reset(new ProgressiveJpegDecoder(attr_.frame_geometry.w, attr_.frame_geometry.h, stride,
                                                               jparams_.outputBufNum, attr_.pixel_format,
                                                               attr_.dev_id));
  }
  CnFrame finfo = progressive_jpeg_decoder_->Decode(packet);
  if (NULL != attr_.frame_callback) {
    LOGD(DECODE) << "Add decode buffer Reference " << finfo.buf_id;
    attr_.frame_callback(finfo);
  }
}
#else
void DecodeHandler::DecodeProgressiveJpeg(const CnPacket& packet) {
  THROW_EXCEPTION(Exception::UNSUPPORTED, "Unsupport decode progressive JPEG");
}
#endif

void DecodeHandler::ReleaseBuffer(uint64_t buf_id) {
  if (jpeg_decode_) {
    bool is_progressive = false;
#ifdef ENABLE_TURBOJPEG
    is_progressive = progressive_jpeg_decoder_ && progressive_jpeg_decoder_->ReleaseBuffer(buf_id);
#endif
    if (!is_progressive) {
      cnjpegDecReleaseReference(handle_, reinterpret_cast<cncodecFrame*>(buf_id));
    }
  } else {
    cnvideoDecReleaseReference(handle_, reinterpret_cast<cncodecFrame*>(buf_id));
  }
}

void DecodeHandler::FeedVideoData(const CnPacket& packet, bool integral_frame) {
  cnvideoDecInput input;
  memset(&input, 0, sizeof(cnvideoDecInput));
  input.streamBuf = reinterpret_cast<u8_t*>(packet.data);
  input.streamLength = packet.length;
  input.pts = SetVpuTimestamp(packet.pts);
  input.flags = CNVIDEODEC_FLAG_TIMESTAMP;
#if CNCODEC_VERSION >= 10600
  if (integral_frame) {
    input.flags |= CNVIDEODEC_FLAG_END_OF_FRAME;
  }
#endif
  LOGT(DECODE) << "Feed stream info, data: " << reinterpret_cast<void*>(input.streamBuf)
               << " ,length: " << input.streamLength << " ,pts: " << input.pts;

  int retry_time = 3;
  while (retry_time--) {
    auto ecode = cnvideoDecFeedData(handle_, &input, 10000);
    if (-CNCODEC_TIMEOUT == ecode) {
      LOGW(DECODE) << "cnvideoDecFeedData timeout, retry feed data, time: " << 3 - retry_time;
      if (!retry_time) {
        GetVpuTimestamp(input.pts, nullptr);  // Failed to feeddata, erase record
        THROW_EXCEPTION(Exception::TIMEOUT, "easydecode timeout");
      }
      continue;
    } else if (CNCODEC_SUCCESS != ecode) {
      GetVpuTimestamp(input.pts, nullptr);  // Failed to feeddata, erase record
      THROW_EXCEPTION(Exception::INTERNAL, "Feed data failed. cncodec error code: " + to_string(ecode));
    } else {
      break;
    }
  }

  packets_count_++;
}

void DecodeHandler::FeedJpegData(const CnPacket& packet, int progressive_mode) {
  cnjpegDecInput input;
  CHECK(DECODE, progressive_mode >= 0);

  memset(&input, 0, sizeof(cnjpegDecInput));
  input.streamBuffer = reinterpret_cast<uint8_t*>(packet.data);
  input.streamLength = packet.length;
  input.pts = packet.pts;
  input.flags = CNJPEGDEC_FLAG_TIMESTAMP;
  LOGT(DECODE) << "Feed stream info, data: " << reinterpret_cast<void*>(input.streamBuffer)
               << " ,length: " << input.streamLength << " ,pts: " << input.pts;

  int retry_time = 3;
  while (retry_time--) {
    auto ecode = cnjpegDecFeedData(handle_, &input, 10000);
    if (-CNCODEC_TIMEOUT == ecode) {
      LOGW(DECODE) << "cnjpegDecFeedData timeout, retry feed data, time: " << 3 - retry_time;
      if (!retry_time) {
        GetVpuTimestamp(input.pts, nullptr);  // Failed to feeddata, erase record
        THROW_EXCEPTION(Exception::TIMEOUT, "easydecode timeout");
      }
      continue;
    } else if (CNCODEC_SUCCESS != ecode) {
      GetVpuTimestamp(input.pts, nullptr);  // Failed to feeddata, erase record
      THROW_EXCEPTION(Exception::INTERNAL, "Feedd data failed. cncodec error code: " + to_string(ecode));
    } else {
      break;
    }
  }
}

bool DecodeHandler::FeedEos() {
  unique_lock<mutex> eos_lk(eos_mtx_);
  if (send_eos_) {
    LOGW(DECODE) << "EOS had been feed, won't feed again";
    return false;
  }

  i32_t ecode = CNCODEC_SUCCESS;
  LOGI(DECODE) << "Thread id: " << std::this_thread::get_id() << ",Feed EOS data";
  if (jpeg_decode_) {
    cnjpegDecInput input;
    input.streamBuffer = nullptr;
    input.streamLength = 0;
    input.pts = 0;
    input.flags = CNJPEGDEC_FLAG_EOS;
    ecode = cnjpegDecFeedData(handle_, &input, 10000);
  } else {
    cnvideoDecInput input;
    input.streamBuf = nullptr;
    input.streamLength = 0;
    input.pts = 0;
    input.flags = CNVIDEODEC_FLAG_EOS;
    ecode = cnvideoDecFeedData(handle_, &input, 10000);
  }

  if (-CNCODEC_TIMEOUT == ecode) {
    THROW_EXCEPTION(Exception::TIMEOUT, "EasyDecode feed EOS timeout");
  } else if (CNCODEC_SUCCESS != ecode) {
    THROW_EXCEPTION(Exception::INTERNAL, "Feed EOS failed. cncodec error code: " + to_string(ecode));
  }

  send_eos_ = true;
  return true;
}

std::unique_ptr<EasyDecode> EasyDecode::New(const Attr& attr) {
  struct __ShowCodecVersion {
    __ShowCodecVersion() {
      u8_t* version = cncodecGetVersion();
      LOGI(DECODE) << "CNCodec Version: " << static_cast<const unsigned char*>(version);
    }
  };
  static __ShowCodecVersion show_version;

  return std::unique_ptr<EasyDecode>(new EasyDecode(attr));
}

EasyDecode::EasyDecode(const Attr& attr) {
  handler_ = new DecodeHandler(this);
  bool jpeg_decode = attr.codec_type == CodecType::JPEG || attr.codec_type == CodecType::MJPEG;
  try {
    if (jpeg_decode) {
      handler_->InitJpegDecode(attr);
    } else {
      handler_->InitVideoDecode(attr);
    }
  } catch (...) {
    delete handler_;
    handler_ = nullptr;
    throw;
  }
}

EasyDecode::~EasyDecode() {
  if (handler_) {
    delete handler_;
    handler_ = nullptr;
  }
}

bool EasyDecode::Pause() {
  Status expected = Status::RUNNING;
  if (handler_->status_.compare_exchange_strong(expected, Status::PAUSED)) {
    return true;
  }
  return false;
}

bool EasyDecode::Resume() {
  Status expected = Status::PAUSED;
  if (handler_->status_.compare_exchange_strong(expected, Status::RUNNING)) {
    return true;
  }
  return false;
}

void EasyDecode::AbortDecoder() { handler_->AbortDecoder(); }

EasyDecode::Status EasyDecode::GetStatus() const { return handler_->status_.load(); }

bool EasyDecode::FeedData(const CnPacket& packet, bool integral_frame) {
  if (!handler_->handle_) {
    LOGE(DECODE) << "Decoder has not been init";
    return false;
  }
  if (handler_->send_eos_) {
    LOGW(DECODE) << "EOS had been sent, won't feed data";
    return false;
  }
  if (Status::PAUSED == handler_->status_.load()) {
    return false;
  }
  if (packet.length == 0 || (!packet.data)) {
    LOGE(DECODE) << "Packet do not have data. The packet will not be sent.";
    return false;
  }

  if (!handler_->jpeg_decode_) {
    handler_->FeedVideoData(packet, integral_frame);
  } else {
    int progressive_mode = detail::CheckProgressiveMode(reinterpret_cast<uint8_t*>(packet.data), packet.length);
    if (progressive_mode < 0) {
      // not jpeg
      return false;
    } else if (progressive_mode == 0) {
      // not progressive
      handler_->FeedJpegData(packet, progressive_mode);
    } else {
      // progressive
      handler_->DecodeProgressiveJpeg(packet);
    }
  }
  return true;
}

bool EasyDecode::FeedEos() { return handler_->FeedEos(); }

void EasyDecode::ReleaseBuffer(uint64_t buf_id) {
  LOGD(DECODE) << "Release decode buffer reference " << buf_id;
  handler_->ReleaseBuffer(buf_id);
}

bool EasyDecode::CopyFrameD2H(void* dst, const CnFrame& frame) {
  if (!dst) {
    THROW_EXCEPTION(Exception::INVALID_ARG, "CopyFrameD2H: destination is nullptr");
    return false;
  }
  auto odata = reinterpret_cast<uint8_t*>(dst);
  LOGT(DECODE) << "Copy codec frame from device to host";
  LOGT(DECODE) << "device address: (plane 0) " << frame.ptrs[0] << ", (plane 1) " << frame.ptrs[1];
  LOGT(DECODE) << "host address: " << reinterpret_cast<int64_t>(odata);

  switch (frame.pformat) {
    case PixelFmt::NV21:
    case PixelFmt::NV12: {
      size_t len_y = frame.strides[0] * frame.height;
      size_t len_uv = frame.strides[1] * frame.height / 2;
      CALL_CNRT_FUNC(cnrtMemcpy(reinterpret_cast<void*>(odata), frame.ptrs[0], len_y, CNRT_MEM_TRANS_DIR_DEV2HOST),
                     "Decode copy frame plane luminance failed.");
      CALL_CNRT_FUNC(
          cnrtMemcpy(reinterpret_cast<void*>(odata + len_y), frame.ptrs[1], len_uv, CNRT_MEM_TRANS_DIR_DEV2HOST),
          "Decode copy frame plane chroma failed.");
      break;
    }
    case PixelFmt::I420: {
      size_t len_y = frame.strides[0] * frame.height;
      size_t len_u = frame.strides[1] * frame.height / 2;
      size_t len_v = frame.strides[2] * frame.height / 2;
      CALL_CNRT_FUNC(cnrtMemcpy(reinterpret_cast<void*>(odata), frame.ptrs[0], len_y, CNRT_MEM_TRANS_DIR_DEV2HOST),
                     "Decode copy frame plane y failed.");
      CALL_CNRT_FUNC(
          cnrtMemcpy(reinterpret_cast<void*>(odata + len_y), frame.ptrs[1], len_u, CNRT_MEM_TRANS_DIR_DEV2HOST),
          "Decode copy frame plane u failed.");
      CALL_CNRT_FUNC(
          cnrtMemcpy(reinterpret_cast<void*>(odata + len_y + len_u), frame.ptrs[2], len_v, CNRT_MEM_TRANS_DIR_DEV2HOST),
          "Decode copy frame plane v failed.");
      break;
    }
    default:
      LOGE(DECODE) << "don't support format: " << static_cast<int>(frame.pformat);
      break;
  }

  return true;
}

EasyDecode::Attr EasyDecode::GetAttr() const { return handler_->attr_; }

int EasyDecode::GetMinimumOutputBufferCount() const { return handler_->minimum_buf_cnt_; }

}  // namespace edk
