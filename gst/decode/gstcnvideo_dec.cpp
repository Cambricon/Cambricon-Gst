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

#include "gstcnvideo_dec.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <queue>
#include <set>
#include <thread>

#include "cn_codec_common.h"
#include "cn_video_dec.h"
#include "common/frame_deallocator.h"
#include "common/mlu_memory_meta.h"
#include "common/mlu_utils.h"
#include "device/mlu_context.h"
#include "easyinfer/mlu_memory_op.h"

GST_DEBUG_CATEGORY_EXTERN(gst_cambricon_debug);
#define GST_CAT_DEFAULT gst_cambricon_debug

#define GST_CNVIDEODEC_ERROR(el, domain, code, msg) GST_ELEMENT_ERROR(el, domain, code, msg, ("None"))

// cncodec add version macro since v1.6.0
#ifndef CNCODEC_VERSION
#define CNCODEC_VERSION 0
#endif

// default params
static constexpr gint DEFAULT_DEVICE_ID = 0;
static constexpr guint DEFAULT_STREAM_ID = 0;
static constexpr guint DEFAULT_INPUT_BUFFER_NUM = 4;
static constexpr guint DEFAULT_OUTPUT_BUFFER_NUM = 4;

// use set to avoid duplicated id
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
  PROP_INPUT_BUFFER_NUM,
  PROP_OUTPUT_BUFFER_NUM,
};

static inline void
release_buffer(GstCnvideodec* self, cnvideoDecoder decode, uint64_t buf_id)
{
  if (decode) {
    auto ret = cnvideoDecReleaseReference(decode, reinterpret_cast<cncodecFrame*>(buf_id));
    if (ret != CNCODEC_SUCCESS) {
      GST_CNVIDEODEC_ERROR(self, RESOURCE, FAILED, ("cnvideoDecode Release reference failed, error code: %d", ret));
    }
  }
}

struct DecodeFrameDeallocator : public FrameDeallocator
{
  DecodeFrameDeallocator(GstCnvideodec* ele, void* decode, uint64_t buf_id)
  {
    element_ = ele;
    decode_ = decode;
    buf_id_ = buf_id;
  }
  ~DecodeFrameDeallocator() {}
  void deallocate() override
  {
    release_buffer(element_, decode_, buf_id_);
    element_ = nullptr;
    decode_ = nullptr;
  }

private:
  DecodeFrameDeallocator(const DecodeFrameDeallocator&) = delete;
  const DecodeFrameDeallocator& operator=(const DecodeFrameDeallocator&) = delete;
  GstCnvideodec* element_ = nullptr;
  cnvideoDecoder decode_ = nullptr;
  uint64_t buf_id_ = 0;
};

struct GstCnvideodecPrivateCpp
{
  std::mutex eos_mtx;
  std::condition_variable eos_cond;

  std::thread event_loop;
  std::queue<cncodecCbEventType> event_queue;
  std::mutex event_mtx;
  std::condition_variable event_cond;
};

struct GstCnvideodecPrivate
{
  cnvideoDecoder decode;
  cnvideoDecCreateInfo params;
  cncodecType codec_type;

  GstVideoInfo sink_info;
  GstVideoInfo src_info;
  guint channel_id;
  gboolean output_on_cpu;
  gboolean send_eos;
  gboolean got_eos;

  GstCnvideodecPrivateCpp* cpp;
  GstClockTime duration;
};

G_DEFINE_TYPE_WITH_PRIVATE(GstCnvideodec, gst_cnvideodec, GST_TYPE_ELEMENT);
// gst_cnvideodec_parent_class is defined in G_DEFINE_TYPE macro
#define PARENT_CLASS gst_cnvideodec_parent_class

static inline GstCnvideodecPrivate*
gst_cnvideodec_get_private(GstCnvideodec* object)
{
  return reinterpret_cast<GstCnvideodecPrivate*>(gst_cnvideodec_get_instance_private(object));
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
                                          "video/x-h265;"));

static GstStaticPadTemplate src_factory =
  GST_STATIC_PAD_TEMPLATE("src",
                          GST_PAD_SRC,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS("video/x-raw(memory:mlu), format={NV12, NV21, I420};"
                                          "video/x-raw, format={NV12, NV21, I420};"));

// method declarations
static void
gst_cnvideodec_finalize(GObject* gobject);
static void
gst_cnvideodec_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec);
static void
gst_cnvideodec_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec);

static gboolean
gst_cnvideodec_sink_event(GstPad* pad, GstObject* parent, GstEvent* event);
static GstFlowReturn
gst_cnvideodec_chain(GstPad* pad, GstObject* parent, GstBuffer* buf);
static GstStateChangeReturn
gst_cnvideodec_change_state(GstElement* element, GstStateChange transition);

static gboolean
gst_cnvideodec_set_caps(GstCnvideodec* self, GstCaps* caps);

// public method
static gboolean
gst_cnvideodec_init_decoder(GstCnvideodec* self);
static gboolean
gst_cnvideodec_destroy_decoder(GstCnvideodec* self);

// private method
static void
handle_frame(GstCnvideodec* self, cnvideoDecOutput* out);
static void
handle_sequence(GstCnvideodec* self, cnvideoDecSequenceInfo* info);
static i32_t
event_handler(cncodecCbEventType type, void* user_data, void* package);
static void
print_create_attr(cnvideoDecCreateInfo* p_attr);
static void
event_task_runner(GstCnvideodec* self);
static void
handle_event(GstCnvideodec* self, cncodecCbEventType type);
static gboolean
feed_data(GstCnvideodec* self, GstBuffer* buf);
static void
handle_eos(GstCnvideodec* self);
static void
handle_frame(GstCnvideodec* self, cnvideoDecOutput* out);

/* 1. GObject vmethod implementations */

static void
gst_cnvideodec_finalize(GObject* object)
{
  g_stream_id_set.erase(GST_CNVIDEODEC(object)->stream_id);
  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(GST_CNVIDEODEC(object));
  delete priv->cpp;

  G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

// initialize the cnvideodec's class
static void
gst_cnvideodec_class_init(GstCnvideodecClass* klass)
{
  GObjectClass* gobject_class;
  GstElementClass* gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  gobject_class->set_property = gst_cnvideodec_set_property;
  gobject_class->get_property = gst_cnvideodec_get_property;
  gobject_class->finalize = gst_cnvideodec_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_cnvideodec_change_state);

  klass->init_decoder = GST_DEBUG_FUNCPTR(gst_cnvideodec_init_decoder);
  klass->destroy_decoder = GST_DEBUG_FUNCPTR(gst_cnvideodec_destroy_decoder);

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
  g_object_class_install_property(gobject_class, PROP_INPUT_BUFFER_NUM,
                                  g_param_spec_uint("input-buffer-num", "input buffer num", "input buffer number", 0,
                                                    20, DEFAULT_INPUT_BUFFER_NUM,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_OUTPUT_BUFFER_NUM,
                                  g_param_spec_uint("output-buffer-num", "output buffer num", "output buffer number", 0,
                                                    20, DEFAULT_OUTPUT_BUFFER_NUM,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  gst_element_class_set_details_simple(gstelement_class, "cnvideo_dec", "Generic/Decoder", "Cambricon video decoder",
                                       "Cambricon Solution SDK");

  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&src_factory));
  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&sink_factory));
}

static inline cncodecPixelFormat
video_format_cast(const GstVideoFormat fmt)
{
  switch (fmt) {
    case GST_VIDEO_FORMAT_NV12:
      return CNCODEC_PIX_FMT_NV12;
    case GST_VIDEO_FORMAT_NV21:
      return CNCODEC_PIX_FMT_NV21;
    case GST_VIDEO_FORMAT_I420:
      return CNCODEC_PIX_FMT_I420;
    default:
      return CNCODEC_PIX_FMT_NV12;
  }
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_cnvideodec_init(GstCnvideodec* self)
{
  self->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  gst_pad_set_event_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_cnvideodec_sink_event));
  gst_pad_set_chain_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_cnvideodec_chain));
  GST_PAD_SET_ACCEPT_INTERSECT(self->sinkpad);
  gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  GST_PAD_SET_ACCEPT_INTERSECT(self->srcpad);
  gst_element_add_pad(GST_ELEMENT(self), self->srcpad);

  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);

  self->silent = FALSE;
  self->device_id = DEFAULT_DEVICE_ID;
  self->input_buffer_num = DEFAULT_INPUT_BUFFER_NUM;
  self->output_buffer_num = DEFAULT_OUTPUT_BUFFER_NUM;
  priv->channel_id = 0;
  priv->codec_type = CNCODEC_H264;
  priv->duration = GST_CLOCK_TIME_NONE;
  priv->output_on_cpu = FALSE;
  priv->send_eos = FALSE;
  priv->got_eos = FALSE;
  priv->cpp = new GstCnvideodecPrivateCpp;
  std::unique_lock<std::mutex> lk(stream_id_mutex);
  do {
    self->stream_id = g_stream_id++;
  } while (!g_stream_id_set.insert(self->stream_id).second);
  lk.unlock();
}

static void
gst_cnvideodec_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
  GstCnvideodec* self = GST_CNVIDEODEC(object);
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
        GST_WARNING_OBJECT(self, "stream id %u already exists, use default stream id instead", self->stream_id);
        self->stream_id = tmp;
        g_stream_id_set.insert(self->stream_id);
      }
      lk.unlock();
      break;
    }
    case PROP_INPUT_BUFFER_NUM:
      self->input_buffer_num = g_value_get_uint(value);
      break;
    case PROP_OUTPUT_BUFFER_NUM:
      self->output_buffer_num = g_value_get_uint(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void
gst_cnvideodec_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
  GstCnvideodec* self = GST_CNVIDEODEC(object);

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
    case PROP_INPUT_BUFFER_NUM:
      g_value_set_uint(value, self->input_buffer_num);
      break;
    case PROP_OUTPUT_BUFFER_NUM:
      g_value_set_uint(value, self->output_buffer_num);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

/* 2. GstElement vmethod implementations */

static GstStateChangeReturn
gst_cnvideodec_change_state(GstElement* element, GstStateChange transition)
{
  GstCnvideodecPrivate* priv = nullptr;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstCnvideodecClass* klass = GST_CNVIDEODEC_GET_CLASS(element);
  GstCnvideodec* self = GST_CNVIDEODEC(element);

  ret = GST_ELEMENT_CLASS(PARENT_CLASS)->change_state(element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      priv = gst_cnvideodec_get_private(self);
      if (priv->decode) {
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
gst_cnvideodec_sink_event(GstPad* pad, GstObject* parent, GstEvent* event)
{
  GstCnvideodec* self;
  gboolean ret = FALSE;

  self = GST_CNVIDEODEC(parent);

  GST_LOG_OBJECT(self, "Received %s event: %" GST_PTR_FORMAT, GST_EVENT_TYPE_NAME(event), event);

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
      GstCaps* caps;
      gst_event_parse_caps(event, &caps);
      GST_INFO_OBJECT(self, "caps are %" GST_PTR_FORMAT, caps);
      ret = gst_cnvideodec_set_caps(self, caps);
      if (!ret) {
        GST_ERROR_OBJECT(self, "set caps failed");
      }
      gst_event_unref(event);
      break;
    }
    case GST_EVENT_EOS: {
      GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);
      GST_INFO_OBJECT(self, "stream id %d receive EOS event", self->stream_id);
      std::unique_lock<std::mutex> lk(priv->cpp->eos_mtx);
      priv->send_eos = TRUE;
      cnvideoDecInput input;
      input.streamBuf = nullptr;
      input.streamLength = 0;
      input.pts = 0;
      input.flags = CNVIDEODEC_FLAG_EOS;
      auto ret = cnvideoDecFeedData(priv->decode, &input, 10000);
      if (ret != CNCODEC_SUCCESS) {
        GST_CNVIDEODEC_ERROR(self, STREAM, DECODE, ("cnvideo Decode feed data failed, error code: %d", ret));
      }
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
gst_cnvideodec_set_caps(GstCnvideodec* self, GstCaps* target_caps)
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
  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);
  if (g_strcmp0(name, "video/x-h264") == 0) {
    priv->codec_type = CNCODEC_H264;
  } else if (g_strcmp0(name, "video/x-h265") == 0) {
    priv->codec_type = CNCODEC_HEVC;
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
  GstCnvideodecClass* klass = GST_CNVIDEODEC_GET_CLASS(self);
  src_caps = gst_caps_from_string("video/x-raw(memory:mlu), format={NV12, NV21, I420};"
                                  "video/x-raw, format={NV12, NV21, I420};");

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
  GST_INFO_OBJECT(self, "cnvideo_dec setcaps %" GST_PTR_FORMAT, peer_caps);
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

  if (priv->src_info.finfo->format != GST_VIDEO_FORMAT_NV12 && priv->src_info.finfo->format != GST_VIDEO_FORMAT_NV21 &&
      priv->src_info.finfo->format != GST_VIDEO_FORMAT_I420) {
    GST_ERROR_OBJECT(self, "Unsupport Pixel Format");
    gst_caps_unref(peer_caps);
    return FALSE;
  }

  GST_INFO_OBJECT(self, "fixed caps: %" GST_PTR_FORMAT, peer_caps);
  priv->output_on_cpu = !gst_caps_features_contains(gst_caps_get_features(peer_caps, 0), "memory:mlu");

  gst_caps_unref(peer_caps);

  if (priv->decode) {
    GST_INFO_OBJECT(self, "Destroy previous decoder before Init");
    g_return_val_if_fail(klass->destroy_decoder(self), FALSE);
  }

  priv->send_eos = FALSE;
  priv->got_eos = FALSE;
  GST_INFO_OBJECT(self, "Init and start decoder");

  g_return_val_if_fail(klass->init_decoder(self), FALSE);

  return TRUE;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_cnvideodec_chain(GstPad* pad, GstObject* parent, GstBuffer* buf)
{
  GstCnvideodec* self;
  self = GST_CNVIDEODEC(parent);
  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);

  if (!priv->send_eos) {
    // save duration and pass it to next plugin
    priv->duration = GST_BUFFER_DURATION(buf);
    GST_TRACE_OBJECT(self, "cnvideodec feed one package\n");
    if (!feed_data(self, buf))
      return GST_FLOW_ERROR;
  }
  gst_buffer_unref(buf);
  return GST_FLOW_OK;
}

/* 3. GstCnvideodec method implementations */

static gboolean
gst_cnvideodec_init_decoder(GstCnvideodec* self)
{
  thread_local bool cnrt_env = false;
  if (!cnrt_env) {
    g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id), FALSE);
    cnrt_env = true;
  }

  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);
  // start event loop
  priv->cpp->event_loop = std::thread(&event_task_runner, self);

  cnvideoDecCreateInfo& params = priv->params;
  memset(&params, 0, sizeof(cnvideoDecCreateInfo));
  if (const char* turbo_env_p = std::getenv("VPU_TURBO_MODE")) {
    GST_INFO_OBJECT(self, "VPU Turbo mode : %s", turbo_env_p);
    static std::mutex vpu_instance_mutex;
    std::unique_lock<std::mutex> lk(vpu_instance_mutex);
    static int _vpu_inst_cnt = 0;
    static cnvideoDecInstance _instances[] = {
      // 100 channels:20+14+15+15+14+22
      CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1, CNVIDEODEC_INSTANCE_2, CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_4,
      CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1, CNVIDEODEC_INSTANCE_2, CNVIDEODEC_INSTANCE_3,
      CNVIDEODEC_INSTANCE_4, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1, CNVIDEODEC_INSTANCE_2,
      CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_4, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1,
      CNVIDEODEC_INSTANCE_2, CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_4, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0,
      CNVIDEODEC_INSTANCE_1, CNVIDEODEC_INSTANCE_2, CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_4, CNVIDEODEC_INSTANCE_5,
      CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1, CNVIDEODEC_INSTANCE_2, CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_4,
      CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1, CNVIDEODEC_INSTANCE_2, CNVIDEODEC_INSTANCE_3,
      CNVIDEODEC_INSTANCE_4, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1, CNVIDEODEC_INSTANCE_2,
      CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_4, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1,
      CNVIDEODEC_INSTANCE_2, CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_4, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0,
      CNVIDEODEC_INSTANCE_1, CNVIDEODEC_INSTANCE_2, CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_4, CNVIDEODEC_INSTANCE_5,
      CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1, CNVIDEODEC_INSTANCE_2, CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_4,
      CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1, CNVIDEODEC_INSTANCE_2, CNVIDEODEC_INSTANCE_3,
      CNVIDEODEC_INSTANCE_4, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1, CNVIDEODEC_INSTANCE_2,
      CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_4, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_1,
      CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_4, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_5,
      CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0,
      CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_0, CNVIDEODEC_INSTANCE_5,
      CNVIDEODEC_INSTANCE_3, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_5, CNVIDEODEC_INSTANCE_2, CNVIDEODEC_INSTANCE_2
    };
    params.instance = _instances[_vpu_inst_cnt++ % 100];
  } else {
    params.instance = CNVIDEODEC_INSTANCE_AUTO;
  }
  params.codec = priv->codec_type;
  params.pixelFmt = video_format_cast(priv->src_info.finfo->format);
  params.colorSpace = CNCODEC_COLOR_SPACE_BT_709;
  params.width = priv->sink_info.width;
  params.height = priv->sink_info.height;
  // bit depth minux is 2 only when pixel format is P010
  params.bitDepthMinus8 = 0;
  params.progressive = priv->sink_info.interlace_mode != GST_VIDEO_INTERLACE_MODE_PROGRESSIVE ? 0 : 1;
  params.inputBufNum = self->input_buffer_num;
  params.outputBufNum = self->output_buffer_num;
  params.deviceId = self->device_id;
  params.allocType = CNCODEC_BUF_ALLOC_LIB;
  params.userContext = reinterpret_cast<void*>(self);

  if (!self->silent) {
    print_create_attr(&params);
  }

  auto ret = cnvideoDecCreate(&priv->decode, &event_handler, &params);
  if (ret != CNCODEC_SUCCESS) {
    GST_CNVIDEODEC_ERROR(self, LIBRARY, INIT, ("Create video decode instance failed, error code: %d", ret));
    priv->decode = nullptr;
    return FALSE;
  }

  int stride_align = 1;
  ret = cnvideoDecSetAttributes(priv->decode, CNVIDEO_DEC_ATTR_OUT_BUF_ALIGNMENT, &stride_align);
  if (ret != CNCODEC_SUCCESS) {
    GST_CNVIDEODEC_ERROR(self, LIBRARY, INIT, ("cnvideo decode set attributes faild, error code: %d", ret));
    priv->decode = nullptr;
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cnvideodec_destroy_decoder(GstCnvideodec* self)
{
  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);
  g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id), FALSE);

  /**
   * Release resources.
   */
  std::unique_lock<std::mutex> eos_lk(priv->cpp->eos_mtx);
  if (!priv->got_eos) {
    if (!priv->send_eos && priv->decode) {
      eos_lk.unlock();
      GST_INFO_OBJECT(self, "Send EOS in destruct");
      cnvideoDecInput input;
      input.streamBuf = nullptr;
      input.streamLength = 0;
      input.pts = 0;
      input.flags = CNVIDEODEC_FLAG_EOS;
      auto ret = cnvideoDecFeedData(priv->decode, &input, 10000);
      if (ret != CNCODEC_SUCCESS) {
        GST_CNVIDEODEC_ERROR(self, STREAM, DECODE, ("cnvideo Decode feed data failed, error code: %d", ret));
      }
      priv->send_eos = TRUE;
    } else {
      if (!priv->decode)
        priv->got_eos = true;
    }
  }

  if (!eos_lk.owns_lock()) {
    eos_lk.lock();
  }

  if (!priv->got_eos) {
    GST_INFO_OBJECT(self, "Wait EOS in destruct");
    priv->cpp->eos_cond.wait(eos_lk, [priv]() -> bool { return priv->got_eos; });
  }

  priv->cpp->event_cond.notify_all();
  if (priv->cpp->event_loop.joinable()) {
    priv->cpp->event_loop.join();
  }

  if (priv->decode) {
    // destroy vpu decoder
    GST_INFO_OBJECT(self, "Stop video decoder channel");
    auto ecode = cnvideoDecStop(priv->decode);
    if (CNCODEC_SUCCESS != ecode) {
      GST_ERROR_OBJECT(self, "Decoder stop failed Error Code: %d", ecode);
    }

    GST_INFO_OBJECT(self, "Destroy video decoder channel");
    ecode = cnvideoDecDestroy(priv->decode);
    if (CNCODEC_SUCCESS != ecode) {
      GST_ERROR_OBJECT(self, "Decoder destroy failed Error Code: %d", ecode);
    }
    priv->decode = nullptr;
  }

  return TRUE;
}

static gboolean
feed_data(GstCnvideodec* self, GstBuffer* buf)
{
  GstMapInfo info;

  if (!gst_buffer_map(buf, &info, (GstMapFlags)GST_MAP_READWRITE)) {
    GST_CNVIDEODEC_ERROR(self, RESOURCE, OPEN_READ_WRITE, ("buffer map failed%" GST_PTR_FORMAT, buf));
    return FALSE;
  }

  thread_local bool cnrt_env = false;
  if (!cnrt_env) {
    g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id), FALSE);
    cnrt_env = true;
  }

  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);

  if (info.data != NULL && info.size > 0) {
    cnvideoDecInput input;
    memset(&input, 0, sizeof(cnvideoDecInput));
    input.streamBuf = reinterpret_cast<u8_t*>(info.data);
    input.streamLength = info.size;
    input.pts = GST_BUFFER_PTS(buf);
    input.flags = CNVIDEODEC_FLAG_TIMESTAMP;
#if CNCODEC_VERSION >= 10600
    input.flags |= CNVIDEODEC_FLAG_END_OF_FRAME;
#endif
    GST_TRACE_OBJECT(self, "Feed stream info, data: %p, length: %u, pts: %lu", input.streamBuf, input.streamLength,
                     input.pts);

    auto ecode = cnvideoDecFeedData(priv->decode, &input, 10000);
    if (CNCODEC_SUCCESS != ecode) {
      GST_ERROR_OBJECT(self, "send data failed. Error code: %d", ecode);
      gst_buffer_unmap(buf, &info);
      return FALSE;
    }
  }

  gst_buffer_unmap(buf, &info);

  return TRUE;
}

static void
print_create_attr(cnvideoDecCreateInfo* p_attr)
{
  printf("%-32s%s\n", "param", "value");
  printf("-------------------------------------\n");
  printf("%-32s%u\n", "Codectype", p_attr->codec);
  printf("%-32s%u\n", "Instance", p_attr->instance);
  printf("%-32s%u\n", "DeviceID", p_attr->deviceId);
  printf("%-32s%u\n", "MemoryAllocate", p_attr->allocType);
  printf("%-32s%u\n", "PixelFormat", p_attr->pixelFmt);
  printf("%-32s%u\n", "Progressive", p_attr->progressive);
  printf("%-32s%u\n", "Width", p_attr->width);
  printf("%-32s%u\n", "Height", p_attr->height);
  printf("%-32s%u\n", "BitDepthMinus8", p_attr->bitDepthMinus8);
  printf("%-32s%u\n", "InputBufferNum", p_attr->inputBufNum);
  printf("%-32s%u\n", "OutputBufferNum", p_attr->outputBufNum);
  printf("-------------------------------------\n");
}

static void
abort_decoder(GstCnvideodec* self)
{
  GST_WARNING_OBJECT(self, "Abort decoder");
  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);
  if (priv->decode) {
    cnvideoDecAbort(priv->decode);
    priv->decode = nullptr;
    handle_eos(self);

    std::unique_lock<std::mutex> eos_lk(priv->cpp->eos_mtx);
    priv->got_eos = TRUE;
    priv->cpp->eos_cond.notify_one();
  } else {
    GST_ERROR_OBJECT(self, "Won't do abort, since cncodec handler has not been initialized");
  }
}

static void
event_task_runner(GstCnvideodec* self)
{
  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);
  std::unique_lock<std::mutex> lock(priv->cpp->event_mtx);
  while (!priv->cpp->event_queue.empty() || !priv->got_eos) {
    priv->cpp->event_cond.wait(lock, [priv] { return !priv->cpp->event_queue.empty() || priv->got_eos; });

    if (priv->cpp->event_queue.empty()) {
      // notified by eos
      continue;
    }

    cncodecCbEventType type = priv->cpp->event_queue.front();
    priv->cpp->event_queue.pop();
    lock.unlock();

    switch (type) {
      case CNCODEC_CB_EVENT_EOS:
        handle_eos(self);
        break;
      case CNCODEC_CB_EVENT_SW_RESET:
      case CNCODEC_CB_EVENT_HW_RESET:
        GST_CNVIDEODEC_ERROR(self, LIBRARY, FAILED, ("Decode firmware crash event"));
        abort_decoder(self);
        break;
      case CNCODEC_CB_EVENT_OUT_OF_MEMORY:
        GST_CNVIDEODEC_ERROR(self, LIBRARY, FAILED, ("Out of memory error thrown from cncodec"));
        abort_decoder(self);
        break;
      case CNCODEC_CB_EVENT_ABORT_ERROR:
        GST_CNVIDEODEC_ERROR(self, LIBRARY, FAILED, ("Abort error thrown from cncodec"));
        abort_decoder(self);
        break;
#if CNCODEC_VERSION >= 10600
      case CNCODEC_CB_EVENT_STREAM_CORRUPT:
        GST_WARNING_OBJECT(self, "Stream corrupt, discard frame");
        break;
#endif
      default:
        GST_CNVIDEODEC_ERROR(self, LIBRARY, FAILED, ("Unknown event type"));
        abort_decoder(self);
        break;
    }

    lock.lock();
  }
}

static i32_t
event_handler(cncodecCbEventType type, void* user_data, void* package)
{
  auto handler = reinterpret_cast<GstCnvideodec*>(user_data);
  // [ACQUIRED BY CNCODEC]
  // NEW_FRAME and SEQUENCE event must handled in callback thread,
  // The other events must handled in a different thread.
  if (handler != nullptr) {
    switch (type) {
      case CNCODEC_CB_EVENT_NEW_FRAME:
        handle_frame(handler, reinterpret_cast<cnvideoDecOutput*>(package));
        break;
      case CNCODEC_CB_EVENT_SEQUENCE:
        handle_sequence(handler, reinterpret_cast<cnvideoDecSequenceInfo*>(package));
        break;
      default:
        handle_event(handler, type);
        break;
    }
  }
  return 0;
}

static void
handle_sequence(GstCnvideodec* self, cnvideoDecSequenceInfo* info)
{
  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);
  auto& params = priv->params;
  params.codec = info->codec;
  params.pixelFmt = video_format_cast(priv->src_info.finfo->format);
  params.width = info->width;
  params.height = info->height;

  if (info->minInputBufNum > params.inputBufNum) {
    params.inputBufNum = info->minInputBufNum;
  }
  if (info->minOutputBufNum > params.outputBufNum) {
    params.outputBufNum = info->minOutputBufNum;
  }

  params.userContext = reinterpret_cast<void*>(self);

  auto ecode = cnvideoDecStart(priv->decode, &params);
  if (ecode != CNCODEC_SUCCESS) {
    GST_CNVIDEODEC_ERROR(self, LIBRARY, INIT, ("Start Decoder failed, error code: %d", ecode));
  }
}

static void
handle_event(GstCnvideodec* self, cncodecCbEventType type)
{
  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);
  std::lock_guard<std::mutex> lock(priv->cpp->event_mtx);
  priv->cpp->event_queue.push(type);
  priv->cpp->event_cond.notify_one();
}

static void
handle_eos(GstCnvideodec* self)
{
  GST_INFO_OBJECT(self, "receive EOS from cncodec");
  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);
  std::unique_lock<std::mutex> eos_lk(priv->cpp->eos_mtx);
  priv->got_eos = TRUE;
  priv->cpp->eos_cond.notify_all();

  if (GST_STATE(GST_ELEMENT_CAST(self)) <= GST_STATE_READY)
    return;

  gst_pad_push_event(self->srcpad, gst_event_new_eos());
}

static void
clear_alignment(GstMemory* out_mem, const GstMapInfo& info, cncodecFrame* frame, GstVideoFormat fmt) {
  GstMapInfo cp_info;
  gst_memory_map(out_mem, &cp_info, GST_MAP_WRITE);
  if (fmt == GST_VIDEO_FORMAT_I420) {
    guint8* dst_y = cp_info.data;
    guint8* dst_u = cp_info.data + frame->width * frame->height;
    guint8* dst_v = cp_info.data + frame->width * frame->height + (frame->width * frame->height >> 2);
    guint8* src_y = info.data;
    guint8* src_u = info.data + frame->stride[0] * frame->height;
    guint8* src_v = info.data + frame->stride[0] * frame->height + (frame->stride[1] * frame->height >> 1);
    for (uint32_t i = 0; i < frame->height; i++) {
      memcpy(dst_y + i * frame->width, src_y + i * frame->stride[0], frame->width);
      if (i % 2 == 0) {
        memcpy(dst_u + i * frame->width / 4, src_u + i * frame->stride[1] / 2, frame->width / 2);
        memcpy(dst_v + i * frame->width / 4, src_v + i * frame->stride[2] / 2, frame->width / 2);
      }
    }
  } else {
    // (fmt == GST_VIDEO_FORMAT_NV12 || fmt == GST_VIDEO_FORMAT_NV21)
    guint8* dst_y = cp_info.data;
    guint8* dst_uv = cp_info.data + frame->width * frame->height;
    guint8* src_y = info.data;
    guint8* src_uv = info.data + frame->stride[0] * frame->height;
    for (uint32_t i = 0; i < frame->height; i++) {
      memcpy(dst_y + i * frame->width, src_y + i * frame->stride[0], frame->width);
      if (i % 2 == 0) {
        memcpy(dst_uv + i * frame->width / 2, src_uv + i * frame->stride[1] / 2, frame->width);
      }
    }
  }
  gst_memory_unmap(out_mem, &cp_info);
}

static GstMemory*
copy_frame_d2h(GstCnvideodec* self, cncodecFrame* frame)
{
  GstMemory* mem = nullptr;
  thread_local GstMapInfo info;
  thread_local edk::MluMemoryOp mem_op;
  GST_DEBUG_OBJECT(self, "transform from device(MLU) memory to host memory");

  // prepare memory
  mem = gst_allocator_alloc(NULL, frame->stride[0] * frame->height * 3 / 2, NULL);
  gst_memory_map(mem, &info, (GstMapFlags)GST_MAP_READWRITE);

  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);
  auto fmt = priv->src_info.finfo->format;

  // copy data from device to host
#define CNCODEC_PLANE_DATA(frame, index) reinterpret_cast<void*>(frame->plane[index].addr)
  if (fmt == GST_VIDEO_FORMAT_NV12 || fmt == GST_VIDEO_FORMAT_NV21) {
    mem_op.MemcpyD2H(info.data, CNCODEC_PLANE_DATA(frame, 0), frame->stride[0] * frame->height, 1);
    mem_op.MemcpyD2H(info.data + frame->stride[0] * frame->height, CNCODEC_PLANE_DATA(frame, 1),
                     (frame->stride[1] * frame->height) >> 1, 1);
  } else if (fmt == GST_VIDEO_FORMAT_I420) {
    mem_op.MemcpyD2H(info.data, CNCODEC_PLANE_DATA(frame, 0), frame->stride[0] * frame->height, 1);
    mem_op.MemcpyD2H(info.data + frame->stride[0] * frame->height, CNCODEC_PLANE_DATA(frame, 1),
                     (frame->stride[1] * frame->height) >> 1, 1);
    mem_op.MemcpyD2H(info.data + frame->stride[0] * frame->height + (frame->stride[1] * frame->height >> 1), CNCODEC_PLANE_DATA(frame, 2),
                     (frame->stride[2] * frame->height) >> 1, 1);
  } else {
    gst_memory_unmap(mem, &info);
    gst_memory_unref(mem);
    GST_CNVIDEODEC_ERROR(self, STREAM, FORMAT, ("unsupport pixel format"));
    return nullptr;
  }
#undef CNCODEC_PLANE_DATA
  // clear alignment
  if (frame->stride[0] != frame->width) {
    GST_INFO_OBJECT(self, "clear frame alignment");
    GstMemory* cp_mem = gst_allocator_alloc(NULL, frame->width * frame->height * 3 / 2, NULL);
    clear_alignment(cp_mem, info, frame, fmt);
    gst_memory_unmap(mem, &info);
    gst_memory_unref(mem);
    mem = cp_mem;
  } else {
    gst_memory_unmap(mem, &info);
  }

  return mem;
}

static void
handle_frame(GstCnvideodec* self, cnvideoDecOutput* out)
{
  GstBuffer* buffer = nullptr;
  thread_local bool cnrt_env = false;
  GstCnvideodecPrivate* priv = gst_cnvideodec_get_private(self);
  auto tick = g_get_monotonic_time();

  if (!cnrt_env) {
    g_return_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id));
    cnrt_env = true;
  }

  cncodecFrame* frame = &out->frame;
  if (GST_STATE(GST_ELEMENT_CAST(self)) <= GST_STATE_READY) {
    release_buffer(self, priv->decode, reinterpret_cast<uint64_t>(frame));
    return;
  }

  // prepare data
  buffer = gst_buffer_new();
  if (priv->output_on_cpu) {
    auto mem = copy_frame_d2h(self, frame);
    cnvideoDecAddReference(priv->decode, frame);
    cnvideoDecReleaseReference(priv->decode, frame);
    if (mem) {
      gst_buffer_append_memory(buffer, mem);
    } else {
      gst_buffer_unref(buffer);
      return;
    }
  } else {
    GstMluFrame_t mlu_frame = gst_mlu_frame_new();
    for (uint32_t i = 0; i < frame->planeNum; ++i) {
      size_t plane_size = frame->stride[i] * frame->height;
      plane_size = i == 0 ? plane_size : plane_size >> 1;
      mlu_frame->data[i] = cn_syncedmem_new(plane_size);
      cn_syncedmem_set_dev_data(mlu_frame->data[i], reinterpret_cast<void*>(frame->plane[i].addr));
    }
    mlu_frame->device_id = self->device_id;
    mlu_frame->channel_id = frame->channel;
    mlu_frame->n_planes = frame->planeNum;
    mlu_frame->height = frame->height;
    mlu_frame->width = frame->width;
    for (uint32_t i = 0; i < frame->planeNum; ++i) {
      mlu_frame->stride[i] = frame->stride[i];
    }
    mlu_frame->deallocator = new DecodeFrameDeallocator(self, priv->decode, reinterpret_cast<uint64_t>(frame));

    cnvideoDecAddReference(priv->decode, frame);

    auto meta = gst_buffer_add_mlu_memory_meta(buffer, mlu_frame, "cnvideo_dec");
    if (!meta) {
      GST_WARNING_OBJECT(self, "since pipeline stopped, request GstMluFrame failed\n");
      gst_mlu_frame_unref(mlu_frame);
      gst_buffer_unref(buffer);
      return;
    }
  }

  GST_BUFFER_PTS(buffer) = out->pts;
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
