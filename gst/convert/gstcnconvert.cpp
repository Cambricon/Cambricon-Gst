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
#include <mutex>

#include "cncv.h"
#include "cnrt.h"
#include "common/mlu_memory_meta.h"
#include "common/utils.h"
#include "device/mlu_context.h"
#include "easybang/resize_and_colorcvt.h"
#include "easyinfer/mlu_memory_op.h"

enum
{
  PROP_0,
  PROP_DEVICE_ID,
};
static constexpr gint DEFAULT_DEVICE_ID = -1;

GST_DEBUG_CATEGORY_EXTERN(gst_cambricon_debug);
#define GST_CAT_DEFAULT gst_cambricon_debug

#define GST_CNCONVERT_ERROR(el, domain, code, msg) GST_ELEMENT_ERROR(el, domain, code, msg, ("None"))

#define CNRT_SAFECALL(func, val) \
  do { \
    auto ret = func; \
    if (ret != CNRT_RET_SUCCESS) { \
      GST_CNCONVERT_ERROR(self, LIBRARY, FAILED, ("Call [" #func "] failed")); \
      return val; \
    } \
  } while(0)

#define CNCV_SAFECALL(func, val) \
  do { \
    auto ret = func; \
    if (ret != CNCV_STATUS_SUCCESS) { \
      GST_CNCONVERT_ERROR(self, LIBRARY, FAILED, ("Call [" #func "] failed")); \
      return val; \
    } \
  } while(0)

/* the capabilities of the inputs and outputs. */
static GstStaticPadTemplate sink_factory =
  GST_STATIC_PAD_TEMPLATE("sink",
                          GST_PAD_SINK,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS(
                            "video/x-raw(memory:mlu), format={NV12, NV21, I420, RGB, BGR};"
                            "video/x-raw, format={NV12, NV21, RGB, BGR, RGBA, BGRA, ARGB, ABGR}"));

static GstStaticPadTemplate src_factory =
  GST_STATIC_PAD_TEMPLATE("src",
                          GST_PAD_SRC,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS(
                            "video/x-raw(memory:mlu), format={NV12, NV21, I420, RGB, BGR, RGBA, ARGB, BGRA, ABGR};"
                            "video/x-raw, format={NV12, NV21, I420, RGB, BGR, RGBA, ARGB, BGRA, ABGR};"));

struct GstCnconvertPrivate
{
  cncvHandle_t handle;
  cnrtQueue_t queue;
  std::once_flag init_flag;
  GstVideoInfo sink_info;
  GstVideoInfo src_info;
  GstSyncedMemory_t mlu_dst_mem;
  GstSyncedMemory_t tmp_mem;
  GstSyncedMemory_t cncv_workspace;
  gint device_id;
  gboolean input_on_mlu;
  gboolean output_on_mlu;
  gboolean disable_resize;
  gboolean disable_convert;
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

  g_object_class_install_property(gobject_class, PROP_DEVICE_ID,
                                  g_param_spec_int("device-id", "device id", "device identification", -1, 10,
                                                   DEFAULT_DEVICE_ID,
                                                   (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

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
  priv->tmp_mem = nullptr;
  priv->cncv_workspace = nullptr;
  priv->handle = nullptr;
  priv->queue = nullptr;
  priv->device_id = -1;
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
  if (priv->tmp_mem) {
    if (cn_syncedmem_free(priv->tmp_mem)) {
      GST_ERROR_OBJECT(self, "Free mlu memory failed");
    }
    priv->tmp_mem = nullptr;
  }
  if (priv->cncv_workspace) {
    if (cn_syncedmem_free(priv->cncv_workspace)) {
      GST_ERROR_OBJECT(self, "Free mlu memory failed");
    }
    priv->cncv_workspace = nullptr;
  }
  if (priv->handle) {
    CNCV_SAFECALL(cncvDestroy(priv->handle), );
    priv->handle = nullptr;
  }
  if (priv->queue) {
    CNRT_SAFECALL(cnrtDestroyQueue(priv->queue), );
    priv->queue = nullptr;
  }
}

static void
gst_cnconvert_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
  GstCnconvertPrivate* priv = gst_cnconvert_get_private(GST_CNCONVERT(object));
  switch (prop_id) {
    case PROP_DEVICE_ID:
      priv->device_id = g_value_get_int(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void
gst_cnconvert_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
  GstCnconvertPrivate* priv = gst_cnconvert_get_private(GST_CNCONVERT(object));
  switch (prop_id) {
    case PROP_DEVICE_ID:
      g_value_set_int(value, priv->device_id);
      break;
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

static inline int get_channel_num_plane0(GstVideoFormat fmt)
{
  switch (fmt) {
    case GST_VIDEO_FORMAT_NV12: case GST_VIDEO_FORMAT_NV21: case GST_VIDEO_FORMAT_I420:
      return 1;
    case GST_VIDEO_FORMAT_RGB: case GST_VIDEO_FORMAT_BGR:
      return 3;
    case GST_VIDEO_FORMAT_RGBA: case GST_VIDEO_FORMAT_BGRA: case GST_VIDEO_FORMAT_ARGB: case GST_VIDEO_FORMAT_ABGR:
      return 4;
    default:
      GST_ERROR("Unsupported pixel format");
      return 0;
  }
}

static inline cncvPixelFormat format_cast(GstVideoFormat fmt)
{
  switch (fmt) {
    case GST_VIDEO_FORMAT_NV12:
      return CNCV_PIX_FMT_NV12;
    case GST_VIDEO_FORMAT_NV21:
      return CNCV_PIX_FMT_NV21;
    case GST_VIDEO_FORMAT_I420:
      return CNCV_PIX_FMT_I420;
    case GST_VIDEO_FORMAT_RGB:
      return CNCV_PIX_FMT_RGB;
    case GST_VIDEO_FORMAT_BGR:
      return CNCV_PIX_FMT_BGR;
    case GST_VIDEO_FORMAT_RGBA:
      return CNCV_PIX_FMT_RGBA;
    case GST_VIDEO_FORMAT_BGRA:
      return CNCV_PIX_FMT_BGRA;
    case GST_VIDEO_FORMAT_ARGB:
      return CNCV_PIX_FMT_ARGB;
    case GST_VIDEO_FORMAT_ABGR:
      return CNCV_PIX_FMT_ABGR;
    default:
      GST_ERROR("unsupport pixel format");
      return CNCV_PIX_FMT_INVALID;
  }
}

static inline bool isYUV420sp(GstVideoFormat fmt) {
  if (fmt == GST_VIDEO_FORMAT_NV12 || fmt == GST_VIDEO_FORMAT_NV21)
    return true;

  return false;
}

static inline bool isRGB(GstVideoFormat fmt) {
  if (fmt == GST_VIDEO_FORMAT_RGB || fmt == GST_VIDEO_FORMAT_BGR ||
      fmt == GST_VIDEO_FORMAT_RGBA || fmt == GST_VIDEO_FORMAT_BGRA ||
      fmt == GST_VIDEO_FORMAT_ARGB || fmt == GST_VIDEO_FORMAT_ABGR)
    return true;

  return false;
}

static cncvImageDescriptor video_info_to_desc(const GstVideoInfo& info)
{
  cncvImageDescriptor desc;
  desc.width = info.width;
  desc.height = info.height;
  desc.pixel_fmt = format_cast(info.finfo->format);
  desc.color_space = CNCV_COLOR_SPACE_BT_601;
  desc.depth = CNCV_DEPTH_8U;
  desc.stride[0] = info.stride[0];
  desc.stride[1] = info.stride[1];
  desc.stride[2] = info.stride[2];
  desc.stride[3] = info.stride[3];
  desc.stride[4] = desc.stride[5] = 0;
  return desc;
}

static gboolean
resize_convert(GstCnconvert* self, GstMluFrame_t frame)
{
  GstCnconvertPrivate* priv = gst_cnconvert_get_private(self);

  cncvImageDescriptor src_desc = video_info_to_desc(priv->sink_info);
  cncvImageDescriptor dst_desc = video_info_to_desc(priv->src_info);

  cncvRect src_roi, dst_roi;
  src_roi.x = src_roi.y = dst_roi.x = dst_roi.y = 0;
  src_roi.w = src_desc.width;
  src_roi.h = src_desc.height;
  dst_roi.w = dst_desc.width;
  dst_roi.h = dst_desc.height;

  size_t workspace_size;
  size_t extra_size = 3 * sizeof(void*);
  CNCV_SAFECALL(cncvGetResizeConvertWorkspaceSize(1, &src_desc, &src_roi, &dst_desc, &dst_roi, &workspace_size), FALSE);

  // prepare mlu memory
  if (priv->cncv_workspace && cn_syncedmem_get_size(priv->cncv_workspace) < (workspace_size + extra_size)) {
    cn_syncedmem_free(priv->cncv_workspace);
    priv->cncv_workspace = nullptr;
  }
  if (!priv->cncv_workspace) {
    priv->cncv_workspace = cn_syncedmem_new(workspace_size + extra_size);
  }

  if (!priv->mlu_dst_mem) {
    size_t out_size = 0;
    out_size = (priv->src_info.width * priv->src_info.height) << 2;
    GST_DEBUG_OBJECT(self, "new syncedmem, w: %d, h:%d, out size: %lu", priv->src_info.width, priv->src_info.height,
                     out_size);
    priv->mlu_dst_mem = cn_syncedmem_new(out_size);
  }

  void** buf_host = reinterpret_cast<void**>(cn_syncedmem_get_mutable_host_data(priv->cncv_workspace));
  buf_host[0] = cn_syncedmem_get_mutable_dev_data(frame->data[0]);
  buf_host[1] = cn_syncedmem_get_mutable_dev_data(frame->data[1]);
  buf_host[2] = cn_syncedmem_get_mutable_dev_data(priv->mlu_dst_mem);
  buf_host = nullptr;
  void** buf_dev = reinterpret_cast<void**>(const_cast<void*>(cn_syncedmem_get_dev_data(priv->cncv_workspace)));
  void** src_ptr = buf_dev;
  void** dst_ptr = buf_dev + 2;
  void* workspace = buf_dev + 3;

  CNCV_SAFECALL(cncvResizeConvert_V2(priv->handle, 1,
                                     &src_desc, &src_roi, src_ptr,
                                     &dst_desc, &dst_roi, dst_ptr,
                                     workspace_size, workspace, CNCV_INTER_BILINEAR), FALSE);

  CNRT_SAFECALL(cnrtSyncQueue(priv->queue), FALSE);

  return TRUE;
}

static gboolean
resize_rgb(GstCnconvert* self, GstMluFrame_t frame, GstSyncedMemory_t* _dst)
{
  GstCnconvertPrivate* priv = gst_cnconvert_get_private(self);

  cncvImageDescriptor src_desc = video_info_to_desc(priv->sink_info);
  cncvImageDescriptor dst_desc = video_info_to_desc(priv->src_info);
  dst_desc.pixel_fmt = src_desc.pixel_fmt;
  int ch = get_channel_num_plane0(priv->sink_info.finfo->format);
  dst_desc.stride[0] = dst_desc.width * ch;

  cncvRect src_roi, dst_roi;
  src_roi.x = src_roi.y = dst_roi.x = dst_roi.y = 0;
  src_roi.w = src_desc.width;
  src_roi.h = src_desc.height;
  dst_roi.w = dst_desc.width;
  dst_roi.h = dst_desc.height;

  size_t workspace_size;
  size_t extra_size = 2 * sizeof(void*);
  CNCV_SAFECALL(cncvGetResizeRgbxWorkspaceSize(1, &workspace_size), FALSE);

  // prepare mlu memory
  if (priv->cncv_workspace && cn_syncedmem_get_size(priv->cncv_workspace) < (workspace_size + extra_size)) {
    cn_syncedmem_free(priv->cncv_workspace);
    priv->cncv_workspace = nullptr;
  }
  if (!priv->cncv_workspace) {
    priv->cncv_workspace = cn_syncedmem_new(workspace_size + extra_size);
  }

  if (!*_dst) {
    size_t out_size = 0;
    out_size = (priv->src_info.width * priv->src_info.height) * ch;
    GST_DEBUG_OBJECT(self, "new syncedmem, w: %d, h:%d, out size: %lu", priv->src_info.width, priv->src_info.height,
                     out_size);
    *_dst = cn_syncedmem_new(out_size);
  }
  GstSyncedMemory_t dst = *_dst;

  void** buf_host = reinterpret_cast<void**>(cn_syncedmem_get_mutable_host_data(priv->cncv_workspace));
  buf_host[0] = cn_syncedmem_get_mutable_dev_data(frame->data[0]);
  buf_host[1] = cn_syncedmem_get_mutable_dev_data(dst);
  buf_host = nullptr;
  void** buf_dev = reinterpret_cast<void**>(const_cast<void*>(cn_syncedmem_get_dev_data(priv->cncv_workspace)));
  void** src_ptr = buf_dev;
  void** dst_ptr = buf_dev + 1;
  void* workspace = buf_dev + 2;

  CNCV_SAFECALL(cncvResizeRgbx(priv->handle, 1,
                               src_desc, &src_roi, src_ptr,
                               dst_desc, &dst_roi, dst_ptr,
                               workspace_size, workspace, CNCV_INTER_BILINEAR), FALSE);

  CNRT_SAFECALL(cnrtSyncQueue(priv->queue), FALSE);

  return TRUE;
}

static gboolean
cvt_rgb(GstCnconvert* self, GstSyncedMemory_t src, GstSyncedMemory_t* pdst)
{
  GstCnconvertPrivate* priv = gst_cnconvert_get_private(self);

  cncvImageDescriptor src_desc = video_info_to_desc(priv->src_info);
  cncvImageDescriptor dst_desc = video_info_to_desc(priv->src_info);
  src_desc.pixel_fmt = format_cast(priv->sink_info.finfo->format);
  src_desc.stride[0] = src_desc.width * get_channel_num_plane0(priv->sink_info.finfo->format);

  cncvRect src_roi, dst_roi;
  src_roi.x = src_roi.y = dst_roi.x = dst_roi.y = 0;
  src_roi.w = src_desc.width;
  src_roi.h = src_desc.height;
  dst_roi.w = dst_desc.width;
  dst_roi.h = dst_desc.height;

  size_t extra_size = 2 * sizeof(void*);

  // prepare mlu memory
  if (priv->cncv_workspace && cn_syncedmem_get_size(priv->cncv_workspace) < extra_size) {
    cn_syncedmem_free(priv->cncv_workspace);
    priv->cncv_workspace = nullptr;
  }
  if (!priv->cncv_workspace) {
    priv->cncv_workspace = cn_syncedmem_new(extra_size);
  }

  if (!*pdst) {
    size_t out_size = 0;
    out_size = priv->src_info.stride[0] * priv->src_info.height;
    GST_DEBUG_OBJECT(self, "new syncedmem, w: %d, h:%d, out size: %lu", priv->src_info.width, priv->src_info.height,
                     out_size);
    *pdst = cn_syncedmem_new(out_size);
  }
  GstSyncedMemory_t dst = *pdst;

  void** buf_host = reinterpret_cast<void**>(cn_syncedmem_get_mutable_host_data(priv->cncv_workspace));
  buf_host[0] = cn_syncedmem_get_mutable_dev_data(src);
  buf_host[1] = cn_syncedmem_get_mutable_dev_data(dst);
  buf_host = nullptr;
  void** buf_dev = reinterpret_cast<void**>(const_cast<void*>(cn_syncedmem_get_dev_data(priv->cncv_workspace)));
  void** src_ptr = buf_dev;
  void** dst_ptr = buf_dev + 1;

  CNCV_SAFECALL(cncvRgbxToRgbx(priv->handle, 1,
                               src_desc, src_roi, src_ptr,
                               dst_desc, dst_roi, dst_ptr), FALSE);

  CNRT_SAFECALL(cnrtSyncQueue(priv->queue), FALSE);

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
    int ch = 4;
    if (fmt == GST_VIDEO_FORMAT_RGB || fmt == GST_VIDEO_FORMAT_BGR) ch = 3;
    for (uint32_t i = 0; i < frame->height; i++) {
      memcpy(cp_info.data + i * frame->width * ch, info.data + i * frame->stride[0], frame->width * ch);
    }
  }
  gst_memory_unmap(out_mem, &cp_info);
}

static GstBuffer*
transform_to_cpu(GstCnconvert* self, GstBuffer* buffer, GstMluFrame_t frame, GstVideoFormat fmt)
{
  GstMemory* mem = nullptr;
  thread_local GstMapInfo info;
  using mem_op = edk::MluMemoryOp;

  GST_DEBUG_OBJECT(self, "transform from device(MLU) memory to host memory");

  // prepare memory
  float scale = 1;
  if (fmt == GST_VIDEO_FORMAT_NV12 || fmt == GST_VIDEO_FORMAT_NV21 || fmt == GST_VIDEO_FORMAT_I420) {
    scale = 1.5;
  } else if (fmt == GST_VIDEO_FORMAT_RGB || fmt == GST_VIDEO_FORMAT_BGR) {
    scale = 1;
  }
  int ch = get_channel_num_plane0(fmt);
  mem = gst_allocator_alloc(NULL, frame->stride[0] * frame->height * scale * ch, NULL);
  gst_memory_map(mem, &info, (GstMapFlags)GST_MAP_READWRITE);

  // copy data from device to host
  if (fmt == GST_VIDEO_FORMAT_NV12 || fmt == GST_VIDEO_FORMAT_NV21) {
    mem_op::MemcpyD2H(info.data, cn_syncedmem_get_mutable_dev_data(frame->data[0]), frame->stride[0] * frame->height);
    mem_op::MemcpyD2H(info.data + frame->stride[0] * frame->height, cn_syncedmem_get_mutable_dev_data(frame->data[1]),
                     (frame->stride[1] * frame->height) >> 1);
  } else if (fmt == GST_VIDEO_FORMAT_I420) {
    mem_op::MemcpyD2H(info.data, cn_syncedmem_get_mutable_dev_data(frame->data[0]), frame->stride[0] * frame->height);
    mem_op::MemcpyD2H(info.data + frame->stride[0] * frame->height, cn_syncedmem_get_mutable_dev_data(frame->data[1]),
                     (frame->stride[1] * frame->height) >> 1);
    mem_op::MemcpyD2H(info.data + frame->stride[0] * frame->height + (frame->stride[1] * frame->height >> 1), cn_syncedmem_get_mutable_dev_data(frame->data[2]),
                     (frame->stride[2] * frame->height) >> 1);
  } else {
    mem_op::MemcpyD2H(info.data, cn_syncedmem_get_mutable_dev_data(frame->data[0]), frame->stride[0] * frame->height);
  }

  GST_DEBUG_OBJECT(self, "stride = %d, width = %d\n", frame->stride[0], frame->width);

  // clear alignment
  if (frame->stride[0] != frame->width * ch) {
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

static gboolean
transform_to_mlu(GstCnconvert* self, GstBuffer* buffer, GstMluFrame_t frame, GstVideoFormat fmt) {
  if (fmt == GST_VIDEO_FORMAT_I420) {
    GST_ERROR_OBJECT(self, "unsupported pixel format!");
    return FALSE;
  }

  GstCnconvertPrivate* priv = gst_cnconvert_get_private(self);
  using mem_op = edk::MluMemoryOp;
  GstVideoFrame host_frame;

  g_return_val_if_fail(gst_video_frame_map(&host_frame, &priv->sink_info, buffer, GST_MAP_READ), FALSE);
  auto unmap_func = [&host_frame]() { gst_video_frame_unmap(&host_frame); };
  ScopeGuard<decltype(unmap_func)> map_guard(std::move(unmap_func));
  GST_DEBUG_OBJECT(self, "transform from host memory to device(MLU) memory");
  frame->width = host_frame.info.width;
  frame->height = host_frame.info.height;
  frame->n_planes = 1;
  frame->device_id = priv->device_id;
  frame->stride[0] = host_frame.info.stride[0];

  if (fmt == GST_VIDEO_FORMAT_NV12 || fmt == GST_VIDEO_FORMAT_NV21) {
    frame->n_planes = 2;
    frame->stride[1] = host_frame.info.stride[1];
    size_t y_len = frame->height * frame->stride[0];
    size_t uv_len = frame->height * frame->stride[1] >> 1;
    frame->data[0] = cn_syncedmem_new(y_len);
    frame->data[1] = cn_syncedmem_new(uv_len);
    mem_op::MemcpyH2D(cn_syncedmem_get_mutable_dev_data(frame->data[0]), host_frame.data[0], y_len);
    mem_op::MemcpyH2D(cn_syncedmem_get_mutable_dev_data(frame->data[1]), host_frame.data[1], uv_len);
  } else {
    size_t len = frame->height * frame->stride[0];
    frame->data[0] = cn_syncedmem_new(len);
    mem_op::MemcpyH2D(cn_syncedmem_get_mutable_dev_data(frame->data[0]), host_frame.data[0], len);
  }
  return TRUE;
}

static GstFlowReturn
gst_cnconvert_chain(GstPad* pad, GstObject* parent, GstBuffer* buffer)
{
  GstCnconvert* self = GST_CNCONVERT(parent);
  GstCnconvertPrivate* priv = gst_cnconvert_get_private(self);

  gboolean pass_through = priv->disable_resize && priv->disable_convert && (priv->output_on_mlu == priv->input_on_mlu);
  if (pass_through) {
    GST_DEBUG_OBJECT(self, "pass through");
    gst_pad_push(self->srcpad, buffer);
    return GST_FLOW_OK;
  }

  thread_local bool cnrt_env = false;
  GstMluFrame_t frame = nullptr;
  MluMemoryMeta_t meta = nullptr;
  if (priv->input_on_mlu) {
    meta = gst_buffer_get_mlu_memory_meta(buffer);

    if (!meta || !meta->frame) {
      GST_CNCONVERT_ERROR(self, RESOURCE, READ, ("get meta failed"));
    }

    frame = meta->frame;
    // set mlu environment
    if (!cnrt_env) {
      priv->device_id = frame->device_id;
      g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), priv->device_id), GST_FLOW_ERROR);
      cnrt_env = true;
    }
  } else {
    if (!cnrt_env) {
      priv->device_id = priv->device_id == -1 ? 0 : priv->device_id;
      g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), priv->device_id), GST_FLOW_ERROR);
      cnrt_env = true;
    }
    frame = gst_mlu_frame_new();
    g_return_val_if_fail(transform_to_mlu(self, buffer, frame, priv->sink_info.finfo->format), GST_FLOW_ERROR);
    meta = gst_buffer_add_mlu_memory_meta(buffer, frame, "convert");
  }

  // init cncvHandle and cnrtQueue
  std::call_once(priv->init_flag, [self, priv]() {
    CNRT_SAFECALL(cnrtCreateQueue(&priv->queue), );
    CNCV_SAFECALL(cncvCreate(&priv->handle), );
    CNCV_SAFECALL(cncvSetQueue(priv->handle, priv->queue), );
  });

  // process
  do {
    if (priv->disable_convert && priv->disable_resize) {
      break;
    }
    if (priv->disable_convert) {
      // resize, rgb/bgr supported
      if (!resize_rgb(self, frame, &priv->mlu_dst_mem))
        return GST_FLOW_ERROR;
    } else if (priv->disable_resize) {
      if (!cvt_rgb(self, frame->data[0], &priv->mlu_dst_mem))
        return GST_FLOW_ERROR;
    } else {
      // resize and convert
      if (isYUV420sp(priv->sink_info.finfo->format) && isRGB(priv->src_info.finfo->format)) {
        if (!resize_convert(self, frame))
          return GST_FLOW_ERROR;
      } else if (isRGB(priv->sink_info.finfo->format) && isRGB(priv->src_info.finfo->format)) {
        if (!resize_rgb(self, frame, &priv->tmp_mem) || !cvt_rgb(self, priv->tmp_mem, &priv->mlu_dst_mem))
          return GST_FLOW_ERROR;
      } else {
        GST_CNCONVERT_ERROR(self, LIBRARY, FAILED, ("unsupported resize and color convert mode"));
        return GST_FLOW_ERROR;
      }
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
    frame->stride[0] = priv->src_info.width * get_channel_num_plane0(priv->src_info.finfo->format);
    frame->n_planes = 1;
  } while (0);

  if (!priv->output_on_mlu) {
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

  {
    auto feat_str = gst_caps_features_to_string(gst_caps_get_features(sinkcaps, 0));
    priv->input_on_mlu = g_strcmp0(feat_str, GST_CAPS_FEATURE_MEMORY_MLU) == 0;
    g_free(feat_str);
  }

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
      filter_caps = gst_caps_from_string("video/x-raw(memory:mlu), format={I420};video/x-raw, format={I420};");
      break;
    case GST_VIDEO_FORMAT_RGB: case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGBA: case GST_VIDEO_FORMAT_BGRA: case GST_VIDEO_FORMAT_ARGB: case GST_VIDEO_FORMAT_ABGR:
      filter_caps = gst_caps_from_string("video/x-raw, format={RGB, BGR, ARGB, ABGR, BGRA, RGBA};"
                                         "video/x-raw(memory:mlu), format={RGB, BGR, ARGB, ABGR, BGRA, RGBA};");
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
    priv->output_on_mlu = g_strcmp0(feat_str, GST_CAPS_FEATURE_MEMORY_MLU) == 0;
    g_free(feat_str);

    priv->disable_resize =
      priv->sink_info.width == priv->src_info.width && priv->sink_info.height == priv->src_info.height;
    priv->disable_convert = priv->sink_info.finfo->format == priv->src_info.finfo->format;

    if (priv->disable_resize && !priv->disable_convert &&
        (!isRGB(priv->sink_info.finfo->format) || !isRGB(priv->src_info.finfo->format))) {
      GST_CNCONVERT_ERROR(self, LIBRARY, SETTINGS, ("without resize, only rgb series to rgb series convert is supported"));
      return FALSE;
    }
    if (!priv->disable_resize && priv->disable_convert &&
        (!isRGB(priv->sink_info.finfo->format))) {
      GST_CNCONVERT_ERROR(self, LIBRARY, SETTINGS, ("without color convert, only rgb series resize is supported"));
      return FALSE;
    }

    if (priv->mlu_dst_mem) {
      if (cn_syncedmem_free(priv->mlu_dst_mem)) {
        GST_CNCONVERT_ERROR(self, RESOURCE, CLOSE, ("Free mlu memory failed"));
        return FALSE;
      }
      priv->mlu_dst_mem = nullptr;
    }
    if (priv->tmp_mem) {
      if (cn_syncedmem_free(priv->tmp_mem)) {
        GST_CNCONVERT_ERROR(self, RESOURCE, CLOSE, ("Free mlu memory failed"));
        return FALSE;
      }
      priv->tmp_mem = nullptr;
    }
  }

  gst_caps_unref(src_peer_caps);

  return ret;
}
