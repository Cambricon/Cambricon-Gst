/* 
 *  Copyright (C) [2019-2020] by Cambricon, Inc.
 * 
 *  This file is part of CNStream-Gst.
 *
 *  CNStream-Gst is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 * 
 *  CNStream-Gst is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 * 
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with CNStream-Gst.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "gstcndecode.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <cstring>
#include <functional>
#include <mutex>
#include <set>

#include "common/frame_deallocator.h"
#include "common/mlu_memory_meta.h"
#include "common/mlu_utils.h"
#include "easycodec/easy_decode.h"
#include "easycodec/vformat.h"
#include "easyinfer/mlu_context.h"

GST_DEBUG_CATEGORY_EXTERN(gst_cambricon_debug);
#define GST_CAT_DEFAULT gst_cambricon_debug

#define GST_CNDECODE_ERROR(el, domain, code, msg) GST_ELEMENT_ERROR(el, domain, code, msg, ("None"))

static constexpr int DEFAULT_DEVICE_ID = 0;
static constexpr int DEFAULT_STREAM_ID = 0;

static guint g_stream_id = 0;
static std::set<guint> g_stream_id_set;
static std::mutex stream_id_mutex;

/* Cndecode args */
enum
{
  PROP_0,
  PROP_SILENT,
  PROP_DEVICE_ID,
  PROP_STREAM_ID,
};

struct DecodeFrameDeallocator : public FrameDeallocator
{
  DecodeFrameDeallocator(edk::EasyDecode* decode, uint64_t buf_id)
  {
    decode_ = decode;
    buf_id_ = buf_id;
  }
  ~DecodeFrameDeallocator() {}
  void deallocate() override
  {
    if (decode_) {
      decode_->ReleaseBuffer(buf_id_);
    }
    decode_ = nullptr;
  }

private:
  DecodeFrameDeallocator(const DecodeFrameDeallocator&) = delete;
  const DecodeFrameDeallocator& operator=(const DecodeFrameDeallocator&) = delete;
  edk::EasyDecode* decode_;
  uint64_t buf_id_;
};

struct GstCndecodePrivate
{
  edk::EasyDecode* decode;
  edk::CodecType codec_type;
  edk::CnPacket packet;

  GstVideoInfo sink_info;
  GstVideoInfo src_info;
  guint channel_id;
  gboolean eos;

  GstClockTime duration;
};

G_DEFINE_TYPE_WITH_PRIVATE(GstCndecode, gst_cndecode, GST_TYPE_ELEMENT);
// gst_cndecode_parent_class is defined in G_DEFINE_TYPE macro
#define PARENT_CLASS gst_cndecode_parent_class

static inline GstCndecodePrivate*
gst_cndecode_get_private(GstCndecode* object)
{
  return reinterpret_cast<GstCndecodePrivate*>(gst_cndecode_get_instance_private(object));
}

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory =
  GST_STATIC_PAD_TEMPLATE("sink",
                          GST_PAD_SINK,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS("video/x-h264, stream-format=byte-stream, alignment=au;"
                                          "video/x-h265;"
                                          "image/jpeg, parsed=true"));

static GstStaticPadTemplate src_factory =
  GST_STATIC_PAD_TEMPLATE("src",
                          GST_PAD_SRC,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS("video/x-raw(memory:mlu), format={NV12, NV21};"));

// method declarations
static void
gst_cndecode_finalize(GObject* gobject);
static void
gst_cndecode_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec);
static void
gst_cndecode_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec);

static gboolean
gst_cndecode_sink_event(GstPad* pad, GstObject* parent, GstEvent* event);
static GstFlowReturn
gst_cndecode_chain(GstPad* pad, GstObject* parent, GstBuffer* buf);
static GstStateChangeReturn
gst_cndecode_change_state(GstElement* element, GstStateChange transition);

static gboolean
gst_cndecode_set_caps(GstCndecode* self, GstCaps* caps);

// public method
static gboolean
gst_cndecode_start(GstCndecode* self);
static gboolean
gst_cndecode_stop(GstCndecode* self);
static gboolean
gst_cndecode_init_decoder(GstCndecode* self);
static gboolean
gst_cndecode_destroy_decoder(GstCndecode* self);

// private method
static gboolean
feed_data(GstCndecode* self, GstBuffer* buf);
static void
handle_eos(GstCndecode* self);
static void
handle_output(GstCndecode* self, const edk::CnFrame& frame);

/* 1. GObject vmethod implementations */

static void
gst_cndecode_finalize(GObject* object)
{
  g_stream_id_set.erase(GST_CNDECODE(object)->stream_id);

  G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

// initialize the cndecode's class
static void
gst_cndecode_class_init(GstCndecodeClass* klass)
{
  GObjectClass* gobject_class;
  GstElementClass* gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  gobject_class->set_property = gst_cndecode_set_property;
  gobject_class->get_property = gst_cndecode_get_property;
  gobject_class->finalize = gst_cndecode_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_cndecode_change_state);

  klass->start = GST_DEBUG_FUNCPTR(gst_cndecode_start);
  klass->stop = GST_DEBUG_FUNCPTR(gst_cndecode_stop);
  klass->init_decoder = GST_DEBUG_FUNCPTR(gst_cndecode_init_decoder);
  klass->destroy_decoder = GST_DEBUG_FUNCPTR(gst_cndecode_destroy_decoder);

  g_object_class_install_property(
    gobject_class, PROP_SILENT,
    g_param_spec_boolean("silent", "Silent", "Produce verbose output ?", FALSE, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, PROP_DEVICE_ID,
                                  g_param_spec_int("device-id", "device id", "device identification", -1, 10,
                                                   DEFAULT_DEVICE_ID,
                                                   (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_STREAM_ID,
                                  g_param_spec_uint("stream-id", "stream id", "stream id", 0, 255, DEFAULT_STREAM_ID,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  gst_element_class_set_details_simple(gstelement_class, "Cndecode", "Generic/Decoder", "Cambricon decoder",
                                       "Cambricon Video");

  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&src_factory));
  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&sink_factory));
}

static inline edk::PixelFmt
video_format_cast(const GstVideoFormat fmt)
{
  switch (fmt) {
    case GST_VIDEO_FORMAT_NV12:
      return edk::PixelFmt::NV12;
    case GST_VIDEO_FORMAT_NV21:
      return edk::PixelFmt::NV21;
    default:
      return edk::PixelFmt::NV12;
  }
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_cndecode_init(GstCndecode* self)
{
  self->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  gst_pad_set_event_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_cndecode_sink_event));
  gst_pad_set_chain_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_cndecode_chain));
  GST_PAD_SET_ACCEPT_INTERSECT(self->sinkpad);
  gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  GST_PAD_SET_ACCEPT_INTERSECT(self->srcpad);
  gst_element_add_pad(GST_ELEMENT(self), self->srcpad);

  GstCndecodePrivate* priv = gst_cndecode_get_private(self);

  self->silent = FALSE;
  self->device_id = DEFAULT_DEVICE_ID;
  priv->channel_id = 0;
  priv->codec_type = edk::CodecType::H264;
  priv->duration = GST_CLOCK_TIME_NONE;
  priv->eos = FALSE;
  std::unique_lock<std::mutex> lk(stream_id_mutex);
  do {
    self->stream_id = g_stream_id++;
  } while (!g_stream_id_set.insert(self->stream_id).second);
  lk.unlock();
}

static void
gst_cndecode_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
  GstCndecode* self = GST_CNDECODE(object);
  guint tmp = 0;

  switch (prop_id) {
    case PROP_SILENT:
      self->silent = g_value_get_boolean(value);
      break;
    case PROP_DEVICE_ID:
      self->device_id = g_value_get_int(value);
      break;
    case PROP_STREAM_ID: {
      tmp = self->stream_id;
      std::unique_lock<std::mutex> lk(stream_id_mutex);
      g_stream_id_set.erase(self->stream_id);
      self->stream_id = g_value_get_uint(value);
      if (!g_stream_id_set.insert(self->stream_id).second) {
        GST_ERROR_OBJECT(self, "stream id %u already exists, use default stream id instead", self->stream_id);
        self->stream_id = tmp;
        g_stream_id_set.insert(self->stream_id);
      }
      lk.unlock();
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void
gst_cndecode_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
  GstCndecode* self = GST_CNDECODE(object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean(value, self->silent);
      break;
    case PROP_DEVICE_ID:
      g_value_set_int(value, self->device_id);
      break;
    case PROP_STREAM_ID:
      g_value_set_uint(value, self->stream_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

/* 2. GstElement vmethod implementations */

static GstStateChangeReturn
gst_cndecode_change_state(GstElement* element, GstStateChange transition)
{
  GstCndecodePrivate* priv = nullptr;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstCndecodeClass* klass = GST_CNDECODE_GET_CLASS(element);
  GstCndecode* self = GST_CNDECODE(element);

  ret = GST_ELEMENT_CLASS(PARENT_CLASS)->change_state(element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      priv = gst_cndecode_get_private(self);
      if (priv->decode) {
        klass->stop(self);
        klass->destroy_decoder(self);
      }
      break;
    default:
      break;
  }

  return ret;
}

/* this function handles sink events */
static gboolean
gst_cndecode_sink_event(GstPad* pad, GstObject* parent, GstEvent* event)
{
  GstCndecode* self;
  gboolean ret = FALSE;

  self = GST_CNDECODE(parent);

  GST_LOG_OBJECT(self, "Received %s event: %" GST_PTR_FORMAT, GST_EVENT_TYPE_NAME(event), event);

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
      GstCaps* caps;
      gst_event_parse_caps(event, &caps);
      GST_INFO_OBJECT(self, "caps are %" GST_PTR_FORMAT, caps);
      ret = gst_cndecode_set_caps(self, caps);
      if (!ret) {
        GST_ERROR_OBJECT(self, "set caps failed");
      }
      gst_event_unref(event);
      break;
    }
    case GST_EVENT_EOS: {
      GstCndecodePrivate* priv = gst_cndecode_get_private(self);
      GST_INFO_OBJECT(self, "stream id %d receive EOS event", self->stream_id);
      priv->eos = TRUE;
      memset(&priv->packet, 0, sizeof(edk::CnPacket));
      priv->decode->SendData(priv->packet, true);
      gst_event_unref(event);
      break;
    }
    default:
      ret = gst_pad_event_default(pad, parent, event);
      break;
  }
  return ret;
}

static gboolean
gst_cndecode_set_caps(GstCndecode* self, GstCaps* target_caps)
{
  // check caps
  auto sinkcaps = gst_pad_get_pad_template_caps(self->sinkpad);
  auto caps = gst_caps_intersect(sinkcaps, target_caps);
  gst_caps_unref(sinkcaps);
  if (gst_caps_is_empty(caps)) {
    gst_caps_unref(caps);
    return FALSE;
  }

  // get information from sink caps
  auto structure = gst_caps_get_structure(caps, 0);
  auto name = gst_structure_get_name(structure);
  GstCndecodePrivate* priv = gst_cndecode_get_private(self);
  if (g_strcmp0(name, "video/x-h264") == 0) {
    priv->codec_type = edk::CodecType::H264;
  } else if (g_strcmp0(name, "video/x-h265") == 0) {
    priv->codec_type = edk::CodecType::H265;
  } else if (g_strcmp0(name, "image/jpeg") == 0) {
    priv->codec_type = edk::CodecType::JPEG;
  } else {
    GST_ERROR_OBJECT(self, "Unsupport Codec Type");
    gst_caps_unref(caps);
    return FALSE;
  }

  if (!gst_video_info_from_caps(&priv->sink_info, caps)) {
    GST_ERROR_OBJECT(self, "Get video info from sink caps failed");
    gst_caps_unref(caps);
    return FALSE;
  }

  gst_caps_unref(caps);
  if (priv->sink_info.width == 0 || priv->sink_info.height == 0) {
    GST_ERROR_OBJECT(self, "get invalid width or height from upstream");
    return FALSE;
  }

  // config src caps
  GstCaps* src_caps = nullptr;
  GstCndecodeClass* klass = GST_CNDECODE_GET_CLASS(self);
  src_caps = gst_caps_from_string("video/x-raw(memory:mlu), format={NV12, NV21};");

  GstCaps* peer_caps = gst_pad_peer_query_caps(self->srcpad, src_caps);
  gst_caps_unref(src_caps);

  if (gst_caps_is_any(peer_caps)) {
    GST_ERROR_OBJECT(self, "srcpad not linked");
    return FALSE;
  }
  if (gst_caps_is_empty(peer_caps)) {
    GST_ERROR_OBJECT(self, "do not have intersection with downstream element");
    return FALSE;
  }

  // set intersection caps to srcpad
  peer_caps = gst_caps_truncate(gst_caps_normalize(peer_caps));
  gst_caps_set_simple(peer_caps, "width", G_TYPE_INT, priv->sink_info.width, "height", G_TYPE_INT,
                      priv->sink_info.height, NULL);
  gst_caps_set_simple(peer_caps, "framerate", GST_TYPE_FRACTION, priv->sink_info.fps_n, priv->sink_info.fps_d, NULL);
  GST_INFO_OBJECT(self, "cndecode setcaps %" GST_PTR_FORMAT, peer_caps);
  gst_pad_use_fixed_caps(self->srcpad);
  if (!gst_pad_set_caps(self->srcpad, peer_caps)) {
    gst_caps_unref(peer_caps);
    return FALSE;
  }

  // get information from intersection caps
  if (!gst_video_info_from_caps(&priv->src_info, peer_caps)) {
    GST_ERROR_OBJECT(self, "Get video info from src caps failed");
    gst_caps_unref(peer_caps);
    return FALSE;
  }

  if (priv->src_info.finfo->format != GST_VIDEO_FORMAT_NV12 && priv->src_info.finfo->format != GST_VIDEO_FORMAT_NV21) {
    GST_ERROR_OBJECT(self, "Unsupport Pixel Format");
    gst_caps_unref(peer_caps);
    return FALSE;
  }

  gst_caps_unref(peer_caps);

  if (priv->decode) {
    GST_INFO_OBJECT(self, "Destroy previous decoder before Init");
    g_return_val_if_fail(klass->stop(self), FALSE);
    g_return_val_if_fail(klass->destroy_decoder(self), FALSE);
  }

  priv->eos = FALSE;
  GST_INFO_OBJECT(self, "Init and start decoder");

  g_return_val_if_fail(klass->init_decoder(self), FALSE);
  if (!klass->start(self)) {
    klass->destroy_decoder(self);
    return FALSE;
  }

  return TRUE;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_cndecode_chain(GstPad* pad, GstObject* parent, GstBuffer* buf)
{
  GstCndecode* self;
  self = GST_CNDECODE(parent);
  GstCndecodePrivate* priv = gst_cndecode_get_private(self);

  if (!priv->eos) {
    // save duration and pass it to next plugin
    priv->duration = GST_BUFFER_DURATION(buf);
    GST_TRACE_OBJECT(self, "cndecode feed one package\n");
    if (!feed_data(self, buf))
      return GST_FLOW_ERROR;
  }
  gst_buffer_unref(buf);
  return GST_FLOW_OK;
}

/* 3. GstCndecode method implementations */

static gboolean
gst_cndecode_start(GstCndecode* self)
{
  GstCndecodePrivate* priv = gst_cndecode_get_private(self);
  if (priv->decode) {
    if (priv->decode->Resume()) {
      GST_CNDECODE_ERROR(self, LIBRARY, INIT, ("start decoder failed"));
      return FALSE;
    }
  } else {
    GST_ERROR_OBJECT(self, "decoder is not initialized");
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_cndecode_stop(GstCndecode* self)
{
  GstCndecodePrivate* priv = gst_cndecode_get_private(self);
  if (priv->decode) {
    if (priv->decode->Pause()) {
      GST_CNDECODE_ERROR(self, LIBRARY, SHUTDOWN, ("stop decoder failed"));
      return FALSE;
    }
  } else {
    GST_ERROR_OBJECT(self, "decoder has not been initialized");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cndecode_init_decoder(GstCndecode* self)
{
  thread_local bool cnrt_env = false;
  if (!cnrt_env) {
    g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id), FALSE);
    cnrt_env = true;
  }

  GstCndecodePrivate* priv = gst_cndecode_get_private(self);
  edk::EasyDecode::Attr attr;
  attr.frame_geometry.w = priv->sink_info.width;
  attr.frame_geometry.h = priv->sink_info.height;
  attr.codec_type = priv->codec_type;
  attr.interlaced = priv->sink_info.interlace_mode != GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
  attr.pixel_format = video_format_cast(priv->src_info.finfo->format);
  attr.silent = self->silent;
  attr.dev_id = self->device_id;
  attr.buf_strategy = edk::BufferStrategy::CNCODEC;
  attr.frame_callback = std::bind(&handle_output, self, std::placeholders::_1);
  attr.eos_callback = std::bind(&handle_eos, self);
  try {
    priv->decode = edk::EasyDecode::Create(attr);
  } catch (edk::Exception& e) {
    GST_CNDECODE_ERROR(self, LIBRARY, INIT, ("%s", e.what()));
    priv->decode = nullptr;
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cndecode_destroy_decoder(GstCndecode* self)
{
  GstCndecodePrivate* priv = gst_cndecode_get_private(self);
  g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id), FALSE);
  delete priv->decode;
  priv->decode = nullptr;

  return TRUE;
}

static gboolean
feed_data(GstCndecode* self, GstBuffer* buf)
{
  bool ret = true;
  GstMapInfo info;

  if (!gst_buffer_map(buf, &info, (GstMapFlags)GST_MAP_READWRITE)) {
    GST_CNDECODE_ERROR(self, RESOURCE, OPEN_READ_WRITE, ("buffer map failed%" GST_PTR_FORMAT, buf));
    return FALSE;
  }

  thread_local bool cnrt_env = false;
  if (!cnrt_env) {
    g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id), FALSE);
    cnrt_env = true;
  }

  GstCndecodePrivate* priv = gst_cndecode_get_private(self);

  priv->packet.data = reinterpret_cast<gpointer>(info.data);
  priv->packet.length = info.size;
  priv->packet.pts = GST_BUFFER_PTS(buf);

  try {
    ret = priv->decode->SendData(priv->packet);
  } catch (edk::Exception& err) {
    GST_CNDECODE_ERROR(self, STREAM, DECODE, ("%s", err.what()));
    ret = false;
  }

  gst_buffer_unmap(buf, &info);

  return ret ? TRUE : FALSE;
}

static void
handle_eos(GstCndecode* self)
{
  if (GST_STATE(GST_ELEMENT_CAST(self)) <= GST_STATE_READY)
    return;

  gst_pad_push_event(self->srcpad, gst_event_new_eos());
}

static void
handle_output(GstCndecode* self, const edk::CnFrame& frame)
{
  GstBuffer* buffer = nullptr;
  thread_local bool cnrt_env = false;
  GstCndecodePrivate* priv = gst_cndecode_get_private(self);
  auto tick = g_get_monotonic_time();

  if (!cnrt_env) {
    g_return_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id));
    cnrt_env = true;
  }

  if (GST_STATE(GST_ELEMENT_CAST(self)) <= GST_STATE_READY) {
    priv->decode->ReleaseBuffer(frame.buf_id);
    return;
  }

  // prepare data
  GstMluFrame_t mlu_frame = gst_mlu_frame_new();
  for (uint32_t i = 0; i < frame.n_planes; ++i) {
    size_t plane_size = frame.strides[0] * frame.height;
    plane_size = i == 0 ? plane_size : plane_size >> 1;
    mlu_frame->data[i] = cn_syncedmem_new(plane_size);
    cn_syncedmem_set_dev_data(mlu_frame->data[i], reinterpret_cast<void*>(frame.ptrs[i]));
  }
  mlu_frame->device_id = frame.device_id;
  mlu_frame->channel_id = frame.channel_id;
  mlu_frame->n_planes = frame.n_planes;
  mlu_frame->height = frame.height;
  mlu_frame->width = frame.width;
  for (uint32_t i = 0; i < frame.n_planes; ++i) {
    mlu_frame->stride[i] = frame.strides[i];
  }
  mlu_frame->deallocator = new DecodeFrameDeallocator(priv->decode, frame.buf_id);

  buffer = gst_buffer_new();
  auto meta = gst_buffer_add_mlu_memory_meta(buffer, mlu_frame, "cndecode");
  if (!meta) {
    GST_WARNING_OBJECT(self, "since pipeline stopped, request GstMluFrame failed\n");
    priv->decode->ReleaseBuffer(frame.buf_id);
    return;
  }

  GST_BUFFER_PTS(buffer) = frame.pts;
  GST_BUFFER_DURATION(buffer) = priv->duration;

  if (!GST_PAD_IS_EOS(self->srcpad)) {
    GST_TRACE_OBJECT(self, "Push frame to srcpad");
    GstFlowReturn ret = gst_pad_push(self->srcpad, buffer);
    if (GST_FLOW_OK != ret) {
      GST_ERROR_OBJECT(self, "gst pad push error: %d", ret);
    }
  } else {
    auto meta = gst_buffer_get_meta(buffer, MLU_MEMORY_META_API_TYPE);
    if (meta)
      gst_buffer_remove_meta(buffer, meta);
    gst_buffer_unref(buffer);
  }

  tick = (g_get_monotonic_time() - tick) / G_TIME_SPAN_MILLISECOND;
  if (tick > 60) {
    GST_WARNING_OBJECT(self, "%s(%d,%p) takes %ldms\n", __FUNCTION__, self->stream_id, buffer, tick);
  }
}
