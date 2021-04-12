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

#include "gstcnconvert.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <cstring>

#include "common/mlu_memory_meta.h"
#include "common/mlu_utils.h"
#include "device/mlu_context.h"
#include "easybang/resize_and_colorcvt.h"
#include "easyinfer/mlu_memory_op.h"
#include "easyinfer/model_loader.h"

GST_DEBUG_CATEGORY_EXTERN(gst_cambricon_debug);
#define GST_CAT_DEFAULT gst_cambricon_debug

#define GST_CNCONVERT_ERROR(el, domain, code, msg) GST_ELEMENT_ERROR(el, domain, code, msg, ("None"))

/* the capabilities of the inputs and outputs. */
static GstStaticPadTemplate sink_factory =
  GST_STATIC_PAD_TEMPLATE("sink",
                          GST_PAD_SINK,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS("video/x-raw(memory:mlu), format={NV12, NV21, I420};"));

static GstStaticPadTemplate src_factory =
  GST_STATIC_PAD_TEMPLATE("src",
                          GST_PAD_SRC,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS("video/x-raw(memory:mlu), format={NV12, NV21, RGBA, ARGB, BGRA, ABGR};"
                                          "video/x-raw, format={NV12, NV21, I420, RGBA, ARGB, BGRA, ABGR};"));

struct GstCnconvertPrivate
{
  edk::MluResizeConvertOp* rcop;
  GstVideoInfo sink_info;
  GstVideoInfo src_info;
  GstSyncedMemory_t mlu_dst_mem;
  gint device_id;
  gboolean keep_on_mlu;
  gboolean disable_resize;
  gboolean disable_convert;
  gboolean pass_through;
};

G_DEFINE_TYPE_WITH_PRIVATE(GstCnconvert, gst_cnconvert, GST_TYPE_ELEMENT);
// gst_cnconvert_parent_class is defined in G_DEFINE_TYPE macro
#define PARENT_CLASS gst_cnconvert_parent_class

static inline GstCnconvertPrivate*
gst_cnconvert_get_private(GstCnconvert* object)
{
  return reinterpret_cast<GstCnconvertPrivate*>(gst_cnconvert_get_instance_private(object));
}

// GObject vmethod
static void
gst_cnconvert_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec);
static void
gst_cnconvert_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec);
static void
gst_cnconvert_finalize(GObject* gobject);
static gboolean
gst_cnconvert_sink_event(GstPad* pad, GstObject* parent, GstEvent* event);
static GstFlowReturn
gst_cnconvert_chain(GstPad* pad, GstObject* parent, GstBuffer* buffer);

// GstCnconvert private method
static gboolean
gst_cnconvert_setcaps(GstCnconvert* self, GstCaps* sinkcaps);
static gboolean
resize(GstCnconvert* self, GstMluFrame_t frame);
static gboolean
resize_convert(GstCnconvert* self, GstMluFrame_t frame);
static GstBuffer*
transform_to_cpu(GstCnconvert* self, GstBuffer* buffer, GstMluFrame_t frame, GstVideoFormat fmt);

/* GObject vmethod implementations */

static void
gst_cnconvert_class_init(GstCnconvertClass* klass)
{
  GObjectClass* gobject_class;
  GstElementClass* gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  gobject_class->set_property = gst_cnconvert_set_property;
  gobject_class->get_property = gst_cnconvert_get_property;
  gobject_class->finalize = gst_cnconvert_finalize;

  gst_element_class_set_details_simple(gstelement_class, "cnconvert", "Generic/Convertor", "Cambricon convertor",
                                       "Cambricon Solution SDK");

  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&src_factory));
  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&sink_factory));
}

static void
gst_cnconvert_init(GstCnconvert* self)
{
  self->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  gst_pad_set_event_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_cnconvert_sink_event));
  gst_pad_set_chain_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_cnconvert_chain));
  gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  gst_element_add_pad(GST_ELEMENT(self), self->srcpad);

  GstCnconvertPrivate* priv = gst_cnconvert_get_private(self);
  priv->mlu_dst_mem = nullptr;
  priv->rcop = nullptr;
}

static void
gst_cnconvert_finalize(GObject* object)
{
  G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
  auto self = GST_CNCONVERT(object);
  GstCnconvertPrivate* priv = gst_cnconvert_get_private(self);

  if (priv->mlu_dst_mem) {
    if (cn_syncedmem_free(priv->mlu_dst_mem)) {
      GST_ERROR_OBJECT(self, "Free mlu memory failed");
    }
    priv->mlu_dst_mem = nullptr;
  }
  if (priv->rcop) {
    priv->rcop->Destroy();
    delete priv->rcop;
    priv->rcop = nullptr;
  }
}

static void
gst_cnconvert_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void
gst_cnconvert_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_cnconvert_sink_event(GstPad* pad, GstObject* parent, GstEvent* event)
{
  GstCnconvert* self;
  gboolean ret = TRUE;
  self = GST_CNCONVERT(parent);
  GST_LOG_OBJECT(self, "received %s event: %" GST_PTR_FORMAT, GST_EVENT_TYPE_NAME(event), event);

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
      GstCaps* caps;
      gst_event_parse_caps(event, &caps);
      ret = gst_cnconvert_setcaps(self, caps);
      if (!ret) {
        GST_ERROR_OBJECT(self, "set caps failed");
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

// unsupported in this version
static gboolean
resize(GstCnconvert* self, GstMluFrame_t frame)
{
  return FALSE;
}

static inline edk::MluResizeConvertOp::ColorMode
color_mode_cast(GstVideoFormat src, GstVideoFormat dst)
{
  if (src == GST_VIDEO_FORMAT_NV12) {
    switch (dst) {
      case GST_VIDEO_FORMAT_RGBA:
        return edk::MluResizeConvertOp::ColorMode::YUV2RGBA_NV12;
      case GST_VIDEO_FORMAT_BGRA:
        return edk::MluResizeConvertOp::ColorMode::YUV2BGRA_NV12;
      case GST_VIDEO_FORMAT_ARGB:
        return edk::MluResizeConvertOp::ColorMode::YUV2ARGB_NV12;
      case GST_VIDEO_FORMAT_ABGR:
        return edk::MluResizeConvertOp::ColorMode::YUV2ABGR_NV12;
      default:
        g_printerr("unsupport video format convert\n");
        return edk::MluResizeConvertOp::ColorMode::RGBA2RGBA;
    }
  } else if (src == GST_VIDEO_FORMAT_NV21) {
    switch (dst) {
      case GST_VIDEO_FORMAT_RGBA:
        return edk::MluResizeConvertOp::ColorMode::YUV2RGBA_NV21;
      case GST_VIDEO_FORMAT_BGRA:
        return edk::MluResizeConvertOp::ColorMode::YUV2BGRA_NV21;
      case GST_VIDEO_FORMAT_ARGB:
        return edk::MluResizeConvertOp::ColorMode::YUV2ARGB_NV21;
      case GST_VIDEO_FORMAT_ABGR:
        return edk::MluResizeConvertOp::ColorMode::YUV2ABGR_NV21;
      default:
        g_printerr("unsupport video format convert\n");
        return edk::MluResizeConvertOp::ColorMode::RGBA2RGBA;
    }
  } else {
    g_printerr("unsupport pixel format convert\n");
    return edk::MluResizeConvertOp::ColorMode::RGBA2RGBA;
  }
}

static gboolean
resize_convert(GstCnconvert* self, GstMluFrame_t frame)
{
  GstCnconvertPrivate* priv = gst_cnconvert_get_private(self);
  // init rcop
  if (!priv->rcop) {
    priv->rcop = new edk::MluResizeConvertOp;
    edk::MluResizeConvertOp::Attr attr;
    edk::MluContext ctx;
    attr.dst_w = priv->src_info.width;
    attr.dst_h = priv->src_info.height;
    attr.batch_size = 1;
    attr.core_version = ctx.GetCoreVersion();
    attr.color_mode = color_mode_cast(priv->sink_info.finfo->format, priv->src_info.finfo->format);
    if (attr.color_mode == edk::MluResizeConvertOp::ColorMode::RGBA2RGBA)
      return FALSE;
    GST_INFO_OBJECT(self, "dst: %d:%d, color mode:%d, core version: %d\n", attr.dst_w, attr.dst_h,
                    static_cast<int>(attr.color_mode), static_cast<int>(attr.core_version));
    priv->rcop->Init(attr);
  }

  // prepare mlu memory
  if (!priv->mlu_dst_mem) {
    size_t out_size = 0;
    if (priv->src_info.finfo->format == GST_VIDEO_FORMAT_NV12 ||
        priv->src_info.finfo->format == GST_VIDEO_FORMAT_NV21) {
      out_size = (priv->src_info.width * priv->src_info.height * 3) >> 1;
    } else {
      out_size = (priv->src_info.width * priv->src_info.height) << 2;
    }
    GST_DEBUG_OBJECT(self, "new syncedmem, w: %d, h:%d, out size: %lu", priv->src_info.width, priv->src_info.height,
                     out_size);
    priv->mlu_dst_mem = cn_syncedmem_new(out_size);
  }

  edk::MluResizeConvertOp::InputData data;
  data.planes[0] = cn_syncedmem_get_mutable_dev_data(frame->data[0]);
  data.planes[1] = cn_syncedmem_get_mutable_dev_data(frame->data[1]);
  data.src_h = priv->sink_info.height;
  data.src_w = priv->sink_info.width;
  data.src_stride = frame->stride[0];
  priv->rcop->BatchingUp(data);
  if (!priv->rcop->SyncOneOutput(cn_syncedmem_get_mutable_dev_data(priv->mlu_dst_mem))) {
    GST_CNCONVERT_ERROR(self, LIBRARY, SETTINGS, ("%s", priv->rcop->GetLastError().c_str()));
    return FALSE;
  }

  return TRUE;
}

static void
clear_alignment(GstMemory* out_mem, const GstMapInfo& info, GstMluFrame_t frame, GstVideoFormat fmt) {
  GstMapInfo cp_info;
  gst_memory_map(out_mem, &cp_info, GST_MAP_WRITE);
  if (fmt == GST_VIDEO_FORMAT_NV12 || fmt == GST_VIDEO_FORMAT_NV21) {
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
  } else if (fmt == GST_VIDEO_FORMAT_I420) {
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
    for (uint32_t i = 0; i < frame->height; i++) {
      memcpy(cp_info.data + i * frame->width * 3, info.data + i * frame->stride[0] * 3, frame->width * 3);
    }
  }
  gst_memory_unmap(out_mem, &cp_info);
}

static GstBuffer*
transform_to_cpu(GstCnconvert* self, GstBuffer* buffer, GstMluFrame_t frame, GstVideoFormat fmt)
{
  GstMemory* mem = nullptr;
  thread_local GstMapInfo info;
  thread_local edk::MluMemoryOp mem_op;

  GST_DEBUG_OBJECT(self, "transform from device(MLU) memory to host memory");

  // prepare memory
  float scale = 1;
  uint32_t ch = 4;
  if (fmt == GST_VIDEO_FORMAT_NV12 || fmt == GST_VIDEO_FORMAT_NV21 || fmt == GST_VIDEO_FORMAT_I420) {
    scale = 1.5;
    ch = 1;
  }
  mem = gst_allocator_alloc(NULL, frame->stride[0] * frame->height * scale * ch, NULL);
  gst_memory_map(mem, &info, (GstMapFlags)GST_MAP_READWRITE);

  // copy data from device to host
  if (fmt == GST_VIDEO_FORMAT_NV12 || fmt == GST_VIDEO_FORMAT_NV21) {
    mem_op.MemcpyD2H(info.data, cn_syncedmem_get_mutable_dev_data(frame->data[0]), frame->stride[0] * frame->height);
    mem_op.MemcpyD2H(info.data + frame->stride[0] * frame->height, cn_syncedmem_get_mutable_dev_data(frame->data[1]),
                     (frame->stride[1] * frame->height) >> 1);
  } else if (fmt == GST_VIDEO_FORMAT_I420) {
    mem_op.MemcpyD2H(info.data, cn_syncedmem_get_mutable_dev_data(frame->data[0]), frame->stride[0] * frame->height);
    mem_op.MemcpyD2H(info.data + frame->stride[0] * frame->height, cn_syncedmem_get_mutable_dev_data(frame->data[1]),
                     (frame->stride[1] * frame->height) >> 1);
    mem_op.MemcpyD2H(info.data + frame->stride[0] * frame->height + (frame->stride[1] * frame->height >> 1), cn_syncedmem_get_mutable_dev_data(frame->data[2]),
                     (frame->stride[2] * frame->height) >> 1);
  } else {
    mem_op.MemcpyD2H(info.data, cn_syncedmem_get_mutable_dev_data(frame->data[0]), frame->stride[0] * frame->height * 4);
  }

  GST_DEBUG_OBJECT(self, "stride = %d, width = %d\n", frame->stride[0], frame->width);

  // clear alignment
  if (frame->stride[0] != frame->width) {
    GST_INFO_OBJECT(self, "clear frame alignment");
    GstMemory* cp_mem = gst_allocator_alloc(NULL, frame->width * frame->height * scale * ch, NULL);
    clear_alignment(cp_mem, info, frame, fmt);
    gst_memory_unmap(mem, &info);
    gst_memory_unref(mem);
    mem = cp_mem;
  } else {
    gst_memory_unmap(mem, &info);
  }

  auto pts = GST_BUFFER_PTS(buffer);
  auto duration = GST_BUFFER_DURATION(buffer);
  gst_buffer_unref(buffer);

  buffer = gst_buffer_new();
  gst_buffer_append_memory(buffer, mem);
  GST_BUFFER_PTS(buffer) = pts;
  GST_BUFFER_DURATION(buffer) = duration;

  return buffer;
}

static GstFlowReturn
gst_cnconvert_chain(GstPad* pad, GstObject* parent, GstBuffer* buffer)
{
  GstCnconvert* self = GST_CNCONVERT(parent);
  GstCnconvertPrivate* priv = gst_cnconvert_get_private(self);
  auto meta = gst_buffer_get_mlu_memory_meta(buffer);

  if (!meta || !meta->frame) {
    GST_CNCONVERT_ERROR(self, RESOURCE, READ, ("get meta failed"));
  }

  GstMluFrame_t frame = meta->frame;
  // set mlu environment
  thread_local bool cnrt_env = false;
  if (!cnrt_env) {
    priv->device_id = frame->device_id;
    g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), priv->device_id), GST_FLOW_ERROR);
    cnrt_env = true;
  }

  // process
  if (!(priv->disable_convert && priv->disable_resize)) {
    if (priv->disable_convert) {
      // resize, NV12 supported
      if (!resize(self, frame))
        return GST_FLOW_ERROR;
    } else {
      // resize and convert
      if (!resize_convert(self, frame))
        return GST_FLOW_ERROR;
    }

    // update mlu memory meta
    guint channel_id = frame->channel_id;
    gst_mlu_frame_unref(frame);

    meta->meta_src = "convert";
    meta->frame = gst_mlu_frame_new();
    frame = meta->frame;
    frame->device_id = priv->device_id;
    frame->channel_id = channel_id;
    frame->data[0] = priv->mlu_dst_mem;
    priv->mlu_dst_mem = nullptr;
    frame->height = priv->src_info.height;
    frame->width = priv->src_info.width;
    frame->stride[0] = priv->src_info.width;
    frame->n_planes = 1;
  }

  if (!priv->keep_on_mlu) {
    // copyout
    try {
      buffer = transform_to_cpu(self, buffer, frame, priv->src_info.finfo->format);
    } catch (edk::Exception& e) {
      gst_buffer_unref(buffer);
      GST_CNCONVERT_ERROR(self, RESOURCE, OPEN_READ_WRITE, ("%s", e.what()));
      return GST_FLOW_ERROR;
    }
  }

  return gst_pad_push(self->srcpad, buffer);
}

static gboolean
gst_cnconvert_setcaps(GstCnconvert* self, GstCaps* sinkcaps)
{
  GstCaps *src_peer_caps, *filter_caps;
  gboolean ret = FALSE;
  GstCnconvertPrivate* priv = gst_cnconvert_get_private(self);

  // get information from sink caps
  g_return_val_if_fail(gst_video_info_from_caps(&priv->sink_info, sinkcaps), FALSE);

  // config src caps
  switch (priv->sink_info.finfo->format) {
    case GST_VIDEO_FORMAT_NV12:
      filter_caps = gst_caps_from_string("video/x-raw, format={NV12, ARGB, ABGR, BGRA, RGBA};"
                                         "video/x-raw(memory:mlu), format={NV12, ARGB, ABGR, BGRA, RGBA};");
      break;
    case GST_VIDEO_FORMAT_NV21:
      filter_caps = gst_caps_from_string("video/x-raw, format={NV21, ARGB, ABGR, BGRA, RGBA};"
                                         "video/x-raw(memory:mlu), format={NV21, ARGB, ABGR, BGRA, RGBA};");
      break;
    case GST_VIDEO_FORMAT_I420:
      filter_caps = gst_caps_from_string("video/x-raw, format={I420};");
      break;
    default:
      GST_ERROR_OBJECT(self, "unsupport pixel format in caps: %" GST_PTR_FORMAT, sinkcaps);
      return FALSE;
  }
  src_peer_caps = gst_pad_peer_query_caps(self->srcpad, filter_caps);
  gst_caps_unref(filter_caps);
  if (gst_caps_is_any(src_peer_caps)) {
    GST_ERROR_OBJECT(self, "srcpad not linked");
    gst_caps_unref(src_peer_caps);
    return FALSE;
  }
  if (gst_caps_is_empty(src_peer_caps)) {
    GST_ERROR_OBJECT(self, "do not have intersection with downstream element");
    gst_caps_unref(src_peer_caps);
    return FALSE;
  }

  src_peer_caps = gst_caps_truncate(gst_caps_normalize(src_peer_caps));
  gst_caps_set_simple(src_peer_caps, "framerate", GST_TYPE_FRACTION, priv->sink_info.fps_n, priv->sink_info.fps_d,
                      NULL);

  // if downstream have not specify resolution
  auto caps_struct = gst_caps_get_structure(src_peer_caps, 0);
  if (!gst_structure_has_field(caps_struct, "width") || !gst_structure_has_field(caps_struct, "height")) {
    gst_caps_set_simple(src_peer_caps, "width", G_TYPE_INT, priv->sink_info.width, "height", G_TYPE_INT,
                        priv->sink_info.height, NULL);
  } else if (!gst_caps_is_fixed(src_peer_caps)) {
    if (!gst_structure_fixate_field_nearest_int(caps_struct, "width", priv->sink_info.width) ||
        !gst_structure_fixate_field_nearest_int(caps_struct, "height", priv->sink_info.height)) {
      GST_ERROR_OBJECT(self, "can not fixate src caps");
      gst_caps_unref(src_peer_caps);
      return FALSE;
    }
  }

  if (!gst_video_info_from_caps(&priv->src_info, src_peer_caps)) {
    GST_ERROR_OBJECT(self, "Get video info from src caps failed");
    gst_caps_unref(src_peer_caps);
    return FALSE;
  }
  GST_INFO_OBJECT(self, "cnconvert setcaps %" GST_PTR_FORMAT, src_peer_caps);
  gst_pad_use_fixed_caps(self->srcpad);
  ret = gst_pad_set_caps(self->srcpad, src_peer_caps);

  if (!ret) {
    GST_ERROR_OBJECT(self, "set caps failed");
  } else {
    // get information from src caps
    auto feat_str = gst_caps_features_to_string(gst_caps_get_features(src_peer_caps, 0));
    priv->keep_on_mlu = g_strcmp0(feat_str, GST_CAPS_FEATURE_MEMORY_MLU) == 0;
    g_free(feat_str);

    priv->disable_resize =
      priv->sink_info.width == priv->src_info.width && priv->sink_info.height == priv->src_info.height;
    priv->disable_convert = priv->sink_info.finfo->format == priv->src_info.finfo->format;
    priv->pass_through = priv->disable_resize && priv->disable_convert && priv->keep_on_mlu;
    if (priv->disable_convert && !priv->disable_resize) {
      GST_ERROR_OBJECT(self, "this version do not support resize without convert");
    }

    if (priv->mlu_dst_mem) {
      if (cn_syncedmem_free(priv->mlu_dst_mem)) {
        GST_CNCONVERT_ERROR(self, RESOURCE, CLOSE, ("Free mlu memory failed"));
        return FALSE;
      }
      priv->mlu_dst_mem = nullptr;
    }
    if (priv->rcop) {
      priv->rcop->Destroy();
      delete priv->rcop;
      priv->rcop = nullptr;
    }
  }

  gst_caps_unref(src_peer_caps);

  return ret;
}
