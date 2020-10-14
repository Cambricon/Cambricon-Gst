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

#include "gstcnvideo_enc.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <thread>

#include "cn_video_enc.h"
#include "cnrt.h"
#include "common/mlu_memory_meta.h"
#include "common/mlu_utils.h"
#include "device/mlu_context.h"
#include "easyinfer/mlu_memory_op.h"
#include "encode_type.h"

GST_DEBUG_CATEGORY_EXTERN(gst_cambricon_debug);
#define GST_CAT_DEFAULT gst_cambricon_debug

#define GST_CNVIDEOENC_ERROR(el, domain, code, msg) GST_ELEMENT_ERROR(el, domain, code, msg, ("None"))

// cncodec add version macro since v1.6.0
#ifndef CNCODEC_VERSION
#define CNCODEC_VERSION 0
#endif

// default params
static constexpr gint DEFAULT_DEVICE_ID = 0;
static constexpr guint DEFAULT_INPUT_BUFFER_NUM = 4;
static constexpr guint DEFAULT_OUTPUT_BUFFER_NUM = 4;
static constexpr guint DEFAULT_GOP_LENGTH = 30;
static constexpr guint DEFAULT_I_QP = 0;
static constexpr guint DEFAULT_P_QP = 0;
static constexpr guint DEFAULT_B_QP = 0;
static constexpr guint DEFAULT_RC_BIT_RATE = 0x100000;
static constexpr guint DEFAULT_RC_MAX_BIT_RATE = 0x100000;
static constexpr guint DEFAULT_MAX_QP = 51;
static constexpr guint DEFAULT_MIN_QP = 0;
static constexpr guint DEFAULT_I_FRAME_INTERVAL = 0;
static constexpr guint DEFAULT_B_FRAME_NUM = 0;
static constexpr gboolean DEFAULT_SILENT = FALSE;
static constexpr gboolean DEFAULT_RC_VBR = FALSE;
static constexpr GstVideoProfile DEFAULT_VIDEO_PROFILE = GST_VP_H264_MAIN;
static constexpr GstVideoLevel DEFAULT_VIDEO_LEVEL = GST_VL_H264_41;
static constexpr cnvideoEncGopType DEFAULT_GOP_TYPE = CNVIDEOENC_GOP_TYPE_BIDIRECTIONAL;
static constexpr cncodecType DEFAULT_CODEC_TYPE = CNCODEC_H264;

// suggest input buffer size
static constexpr uint32_t ENCODE_BUFFER_SIZE = 0x200000;
// colorimetry space
static constexpr cncodecColorSpace COLOR_SPACE = CNCODEC_COLOR_SPACE_BT_709;

/* self args */

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_DEVICE_ID,

  PROP_CODEC_TYPE,
  PROP_INPUT_BUFFER_NUM,
  PROP_OUTPUT_BUFFER_NUM,

  PROP_VIDEO_PROFILE,
  PROP_VIDEO_LEVEL,

  PROP_I_FRAME_INTERVAL,
  PROP_B_FRAME_NUM,
  PROP_GOP_TYPE,

  // rate control
  PROP_RC_VBR,
  PROP_GOP_LENGTH,
  /* for CBR only */
  PROP_RC_BIT_RATE,
  PROP_I_QP,
  PROP_P_QP,
  PROP_B_QP,
  /* for VBR only */
  PROP_RC_MAX_BIT_RATE,
  PROP_MAX_QP,
  PROP_MIN_QP,
};

struct VideoProfileInfo
{
  cnvideoEncProfile prof;
  std::string str;
};
#define PROFILE_INFO_PAIR(prof, cncodec_prof, str)                                                                     \
  {                                                                                                                    \
    prof, { cncodec_prof, str }                                                                                        \
  }
static const std::map<GstVideoProfile, VideoProfileInfo> g_profile_table = {
  PROFILE_INFO_PAIR(GST_VP_H264_BASELINE, CNVIDEOENC_PROFILE_H264_BASELINE, "baseline"),
  PROFILE_INFO_PAIR(GST_VP_H264_MAIN, CNVIDEOENC_PROFILE_H264_MAIN, "main"),
  PROFILE_INFO_PAIR(GST_VP_H264_HIGH, CNVIDEOENC_PROFILE_H264_HIGH, "high"),
  PROFILE_INFO_PAIR(GST_VP_H264_HIGH_10, CNVIDEOENC_PROFILE_H264_HIGH_10, "high-10"),
  PROFILE_INFO_PAIR(GST_VP_H265_MAIN, CNVIDEOENC_PROFILE_H265_MAIN, "main"),
  PROFILE_INFO_PAIR(GST_VP_H265_MAIN_STILL, CNVIDEOENC_PROFILE_H265_MAIN_STILL, "main-still-picture"),
  PROFILE_INFO_PAIR(GST_VP_H265_MAIN_INTRA, CNVIDEOENC_PROFILE_H265_MAIN_INTRA, "main-intra"),
  PROFILE_INFO_PAIR(GST_VP_H265_MAIN_10, CNVIDEOENC_PROFILE_H265_MAIN_10, "main-10"),
};

struct VideoLevelInfo
{
  cnvideoEncLevel level;
  std::string level_str;
  std::string tier_str;
};
#define LEVEL_INFO_PAIR(level, cncodec_level, level_str, tier_str)                                                     \
  {                                                                                                                    \
    level, { cncodec_level, level_str, tier_str }                                                                      \
  }
static const std::map<GstVideoLevel, VideoLevelInfo> g_level_table = {
  LEVEL_INFO_PAIR(GST_VL_H264_1, CNVIDEOENC_LEVEL_H264_1, "1", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_1B, CNVIDEOENC_LEVEL_H264_1B, "1b", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_11, CNVIDEOENC_LEVEL_H264_11, "1.1", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_12, CNVIDEOENC_LEVEL_H264_12, "1.2", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_13, CNVIDEOENC_LEVEL_H264_13, "1.3", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_2, CNVIDEOENC_LEVEL_H264_2, "2", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_21, CNVIDEOENC_LEVEL_H264_21, "2.1", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_22, CNVIDEOENC_LEVEL_H264_22, "2.2", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_3, CNVIDEOENC_LEVEL_H264_3, "3", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_31, CNVIDEOENC_LEVEL_H264_31, "3.1", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_32, CNVIDEOENC_LEVEL_H264_32, "3.2", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_4, CNVIDEOENC_LEVEL_H264_4, "4", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_41, CNVIDEOENC_LEVEL_H264_41, "4.1", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_42, CNVIDEOENC_LEVEL_H264_42, "4.2", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_5, CNVIDEOENC_LEVEL_H264_5, "5", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_51, CNVIDEOENC_LEVEL_H264_51, "5.1", ""),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_1, CNVIDEOENC_LEVEL_H265_MAIN_1, "1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_1, CNVIDEOENC_LEVEL_H265_HIGH_1, "1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_2, CNVIDEOENC_LEVEL_H265_MAIN_2, "2", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_2, CNVIDEOENC_LEVEL_H265_HIGH_2, "2", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_21, CNVIDEOENC_LEVEL_H265_MAIN_21, "2.1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_21, CNVIDEOENC_LEVEL_H265_HIGH_21, "2.1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_3, CNVIDEOENC_LEVEL_H265_MAIN_3, "3", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_3, CNVIDEOENC_LEVEL_H265_HIGH_3, "3", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_31, CNVIDEOENC_LEVEL_H265_MAIN_31, "3.1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_31, CNVIDEOENC_LEVEL_H265_HIGH_31, "3.1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_4, CNVIDEOENC_LEVEL_H265_MAIN_4, "4", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_4, CNVIDEOENC_LEVEL_H265_HIGH_4, "4", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_41, CNVIDEOENC_LEVEL_H265_MAIN_41, "4.1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_41, CNVIDEOENC_LEVEL_H265_HIGH_41, "4.1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_5, CNVIDEOENC_LEVEL_H265_MAIN_5, "5", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_5, CNVIDEOENC_LEVEL_H265_HIGH_5, "5", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_51, CNVIDEOENC_LEVEL_H265_MAIN_51, "5.1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_51, CNVIDEOENC_LEVEL_H265_HIGH_51, "5.1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_52, CNVIDEOENC_LEVEL_H265_MAIN_52, "5.2", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_52, CNVIDEOENC_LEVEL_H265_HIGH_52, "5.2", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_6, CNVIDEOENC_LEVEL_H265_MAIN_6, "6", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_6, CNVIDEOENC_LEVEL_H265_HIGH_6, "6", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_61, CNVIDEOENC_LEVEL_H265_MAIN_61, "6.1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_61, CNVIDEOENC_LEVEL_H265_HIGH_61, "6.1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_62, CNVIDEOENC_LEVEL_H265_MAIN_62, "6.2", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_62, CNVIDEOENC_LEVEL_H265_HIGH_62, "6.2", "high"),
};

#define GST_CNVIDEOENC_VIDEO_PROFILE (gst_cnvideoenc_video_profile_get_type())
static GType
gst_cnvideoenc_video_profile_get_type(void)
{
  static const GEnumValue values[] = { { GST_VP_H264_BASELINE, "h264 Video Profile Baseline", "h264 Baseline" },
                                       { GST_VP_H264_MAIN, "h264 Video Profile Main", "h264 Main" },
                                       { GST_VP_H264_HIGH, "h264 Video Profile High", "h264 High" },
                                       { GST_VP_H264_HIGH_10, "h264 Video Profile High 10", "h264 High 10" },
                                       { GST_VP_H265_MAIN, "h265 Video Profile Main", "h265 Main" },
                                       { GST_VP_H265_MAIN_STILL, "h265 Video Profile Main Still", "h265 Main Still" },
                                       { GST_VP_H265_MAIN_INTRA, "h265 Video Profile Main Intra", "h265 Main Intra" },
                                       { GST_VP_H265_MAIN_10, "h265 Video Profile Main 10", "h265 Main 10" },
                                       { 0, NULL, NULL } };
  static volatile GType id = 0;
  if (g_once_init_enter((gsize*)&id)) {
    GType _id;
    _id = g_enum_register_static("GstCnvideoencVideoProfile", values);
    g_once_init_leave((gsize*)&id, _id);
  }
  return id;
}

#define GST_CNVIDEOENC_VIDEO_LEVEL (gst_cnvideoenc_video_level_get_type())
static GType
gst_cnvideoenc_video_level_get_type(void)
{
  static const GEnumValue values[] = { { GST_VL_H264_1, "H264 Video Level 1", "H264 1" },
                                       { GST_VL_H264_1B, "H264 Video Level 1B", "H264 1B" },
                                       { GST_VL_H264_11, "H264 Video Level 11", "H264 11" },
                                       { GST_VL_H264_12, "H264 Video Level 12", "H264 12" },
                                       { GST_VL_H264_13, "H264 Video Level 13", "H264 13" },
                                       { GST_VL_H264_2, "H264 Video Level 2", "H264 2" },
                                       { GST_VL_H264_21, "H264 Video Level 21", "H264 21" },
                                       { GST_VL_H264_22, "H264 Video Level 22", "H264 22" },
                                       { GST_VL_H264_3, "H264 Video Level 3", "H264 3" },
                                       { GST_VL_H264_31, "H264 Video Level 31", "H264 31" },
                                       { GST_VL_H264_32, "H264 Video Level 32", "H264 32" },
                                       { GST_VL_H264_4, "H264 Video Level 4", "H264 4" },
                                       { GST_VL_H264_41, "H264 Video Level 41", "H264 41" },
                                       { GST_VL_H264_42, "H264 Video Level 42", "H264 42" },
                                       { GST_VL_H264_5, "H264 Video Level 5", "H264 5" },
                                       { GST_VL_H264_51, "H264 Video Level 51", "H264 51" },
                                       { GST_VL_H265_MAIN_1, "H265 Video Level Main 1", "H265 MAIN 1" },
                                       { GST_VL_H265_HIGH_1, "H265 Video Level High 1", "H265 HIGH 1" },
                                       { GST_VL_H265_MAIN_2, "H265 Video Level Main 2", "H265 MAIN 2" },
                                       { GST_VL_H265_HIGH_2, "H265 Video Level High 2", "H265 HIGH 2" },
                                       { GST_VL_H265_MAIN_21, "H265 Video Level Main 21", "H265 MAIN 21" },
                                       { GST_VL_H265_HIGH_21, "H265 Video Level High 21", "H265 HIGH 21" },
                                       { GST_VL_H265_MAIN_3, "H265 Video Level Main 3", "H265 MAIN 3" },
                                       { GST_VL_H265_HIGH_3, "H265 Video Level High 3", "H265 HIGH 3" },
                                       { GST_VL_H265_MAIN_31, "H265 Video Level Main 31", "H265 MAIN 31" },
                                       { GST_VL_H265_HIGH_31, "H265 Video Level High 31", "H265 HIGH 31" },
                                       { GST_VL_H265_MAIN_4, "H265 Video Level Main 4", "H265 MAIN 4" },
                                       { GST_VL_H265_HIGH_4, "H265 Video Level High 4", "H265 HIGH 4" },
                                       { GST_VL_H265_MAIN_41, "H265 Video Level Main 41", "H265 MAIN 41" },
                                       { GST_VL_H265_HIGH_41, "H265 Video Level High 41", "H265 HIGH 41" },
                                       { GST_VL_H265_MAIN_5, "H265 Video Level Main 5", "H265 MAIN 5" },
                                       { GST_VL_H265_HIGH_5, "H265 Video Level High 5", "H265 HIGH 5" },
                                       { GST_VL_H265_MAIN_51, "H265 Video Level Main 51", "H265 MAIN 51" },
                                       { GST_VL_H265_HIGH_51, "H265 Video Level High 51", "H265 HIGH 51" },
                                       { GST_VL_H265_MAIN_52, "H265 Video Level Main 52", "H265 MAIN 52" },
                                       { GST_VL_H265_HIGH_52, "H265 Video Level High 52", "H265 HIGH 52" },
                                       { GST_VL_H265_MAIN_6, "H265 Video Level Main 6", "H265 MAIN 6" },
                                       { GST_VL_H265_HIGH_6, "H265 Video Level High 6", "H265 HIGH 6" },
                                       { GST_VL_H265_MAIN_61, "H265 Video Level Main 61", "H265 MAIN 61" },
                                       { GST_VL_H265_HIGH_61, "H265 Video Level High 61", "H265 HIGH 61" },
                                       { GST_VL_H265_MAIN_62, "H265 Video Level Main 62", "H265 MAIN 62" },
                                       { GST_VL_H265_HIGH_62, "H265 Video Level High 62", "H265 HIGH 62" },
                                       { 0, NULL, NULL } };
  static volatile GType id = 0;
  if (g_once_init_enter((gsize*)&id)) {
    GType _id;
    _id = g_enum_register_static("GstCnvideoencVideLevel", values);
    g_once_init_leave((gsize*)&id, _id);
  }
  return id;
}

#define GST_CNVIDEOENC_GOP_TYPE (gst_cnvideoenc_gop_type_get_type())
static GType
gst_cnvideoenc_gop_type_get_type(void)
{
  static const GEnumValue values[] = {
    { CNVIDEOENC_GOP_TYPE_BIDIRECTIONAL, "bidirectional GOP type", "Video encode GOP type bidirectional" },
    { CNVIDEOENC_GOP_TYPE_LOW_DELAY, "low delay GOP type", "Video encode GOP type low delay" },
    { CNVIDEOENC_GOP_TYPE_PYRAMID, "pyramid GOP type", "Video encode GOP type pyramid" },
    { 0, NULL, NULL }
  };
  static volatile GType id = 0;
  if (g_once_init_enter((gsize*)&id)) {
    GType _id;
    _id = g_enum_register_static("GstCnvideoencGopType", values);
    g_once_init_leave((gsize*)&id, _id);
  }
  return id;
}

#define GST_CNCODEC_TYPE (gst_cncodec_type_get_type())
static GType
gst_cncodec_type_get_type(void)
{
  static const GEnumValue values[] = { { CNCODEC_H264, "video codec type h264", "H.264" },
                                       { CNCODEC_HEVC, "video codec type hevc", "H.265" },
                                       { 0, NULL, NULL } };
  static volatile GType id = 0;
  if (g_once_init_enter((gsize*)&id)) {
    GType _id;
    _id = g_enum_register_static("GstCncodecType", values);
    g_once_init_leave((gsize*)&id, _id);
  }
  return id;
}

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory =
  GST_STATIC_PAD_TEMPLATE("sink",
                          GST_PAD_SINK,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{ NV12, NV21, I420, BGRA, RGBA, ABGR, ARGB }")));

static GstStaticPadTemplate src_factory =
  GST_STATIC_PAD_TEMPLATE("src",
                          GST_PAD_SRC,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS("video/x-h264, stream-format=byte-stream, alignment=nal;"
                                          "video/x-h265, stream-format=byte-stream, alignment=nal;"));

struct GstCnvideoencPrivateCpp
{
  std::mutex eos_mtx;
  std::condition_variable eos_cond;

  std::thread event_loop;
  std::queue<cncodecCbEventType> event_queue;
  std::mutex event_mtx;
  std::condition_variable event_cond;
};

struct GstCnvideoencPrivate
{
  cnvideoEncoder encode;
  cncodecType codec_type;
  cnvideoEncRateCtrl rate_control;
  cncodecPixelFormat pixel_format;
  gboolean send_eos;
  gboolean got_eos;
  gboolean first_frame;
  guint64 frame_id;

  GstCnvideoencPrivateCpp* cpp;
};

// define GstCnvideoenc type and shortcut function to get private
G_DEFINE_TYPE_WITH_PRIVATE(GstCnvideoenc, gst_cnvideoenc, GST_TYPE_ELEMENT);
#define PARENT_CLASS gst_cnvideoenc_parent_class

static inline GstCnvideoencPrivate*
gst_cnvideoenc_get_private(GstCnvideoenc* object)
{
  return reinterpret_cast<GstCnvideoencPrivate*>(gst_cnvideoenc_get_instance_private(object));
}

// method declarations
static void
gst_cnvideoenc_finalize(GObject* gobject);
static void
gst_cnvideoenc_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec);
static void
gst_cnvideoenc_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec);

static gboolean
gst_cnvideoenc_sink_event(GstPad* pad, GstObject* parent, GstEvent* event);
static GstFlowReturn
gst_cnvideoenc_chain(GstPad* pad, GstObject* parent, GstBuffer* buf);
static gboolean
gst_cnvideoenc_set_caps(GstCnvideoenc* self, GstCaps* caps);
static GstStateChangeReturn
gst_cnvideoenc_change_state(GstElement* element, GstStateChange transition);

static gboolean
gst_cnvideoenc_init_encoder(GstCnvideoenc* self);
static gboolean
gst_cnvideoenc_destroy_encoder(GstCnvideoenc* self);

static gboolean
feed_eos(GstCnvideoenc* self);
static gboolean
encode_frame(GstCnvideoenc* self, GstBuffer* buf);
static void
handle_output(GstCnvideoenc* self, cnvideoEncOutput* packet);
static void
handle_eos(GstCnvideoenc* self);
static void
handle_event(GstCnvideoenc* self, cncodecCbEventType type);
static i32_t
event_handler(cncodecCbEventType type, void* user_data, void* package);
static void
event_task_runner(GstCnvideoenc* self);

/* 1. GObject vmethod implementations */

static void
gst_cnvideoenc_finalize(GObject* object)
{
  GstCnvideoencPrivate* priv = gst_cnvideoenc_get_private(GST_CNVIDEOENC(object));
  delete priv->cpp;

  G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

// initialize the cnvideoenc's class
static void
gst_cnvideoenc_class_init(GstCnvideoencClass* klass)
{
  GObjectClass* gobject_class;
  GstElementClass* gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  gobject_class->finalize = gst_cnvideoenc_finalize;
  gobject_class->set_property = gst_cnvideoenc_set_property;
  gobject_class->get_property = gst_cnvideoenc_get_property;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_cnvideoenc_change_state);

  klass->init_encoder = GST_DEBUG_FUNCPTR(gst_cnvideoenc_init_encoder);
  klass->destroy_encoder = GST_DEBUG_FUNCPTR(gst_cnvideoenc_destroy_encoder);

  g_object_class_install_property(gobject_class, PROP_SILENT,
                                  g_param_spec_boolean("silent", "Silent", "Produce verbose output ?", DEFAULT_SILENT,
                                                       (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class, PROP_DEVICE_ID,
                                  g_param_spec_int("device-id", "Device id", "Mlu device id", -1, 20, DEFAULT_DEVICE_ID,
                                                   (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class, PROP_CODEC_TYPE,
                                  g_param_spec_enum("codec", "codec type", "video codec type", GST_CNCODEC_TYPE,
                                                    DEFAULT_CODEC_TYPE,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_INPUT_BUFFER_NUM,
                                  g_param_spec_uint("input-buffer-num", "input buffer num", "input buffer number", 0,
                                                    20, DEFAULT_INPUT_BUFFER_NUM,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_OUTPUT_BUFFER_NUM,
                                  g_param_spec_uint("output-buffer-num", "output buffer num", "output buffer number", 0,
                                                    20, DEFAULT_OUTPUT_BUFFER_NUM,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class, PROP_RC_VBR,
                                  g_param_spec_boolean("vbr", "Rate control VBR enable", "Rate control VBR enable",
                                                       DEFAULT_RC_VBR,
                                                       (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_GOP_LENGTH,
                                  g_param_spec_uint("gop-length", "GOP", "Rate control interval of ISLICE", 1, 65536,
                                                    DEFAULT_GOP_LENGTH,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(
    gobject_class, PROP_RC_BIT_RATE,
    g_param_spec_uint("bitrate", "Rate control bitrate(kbps)", "Rate control average bitrate for CBR", 2, UINT_MAX,
                      DEFAULT_RC_BIT_RATE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(
    gobject_class, PROP_RC_MAX_BIT_RATE,
    g_param_spec_uint("max-bitrate", "Rate control max bitrate(kbps)", "Rate control max bitrate for VBR", 2, UINT_MAX,
                      DEFAULT_RC_MAX_BIT_RATE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_MAX_QP,
                                  g_param_spec_uint("max-qp", "Rate control max qp", "Rate control max qp for VBR", 0,
                                                    51, DEFAULT_MAX_QP,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_MIN_QP,
                                  g_param_spec_uint("min-qp", "Rate control min qp", "Rate control min qp for VBR", 0,
                                                    51, DEFAULT_MIN_QP,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class, PROP_I_QP,
                                  g_param_spec_uint("i-qp", "Rate control I QP", "Rate control I QP for CBR", 0, 51,
                                                    DEFAULT_I_QP,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_P_QP,
                                  g_param_spec_uint("p-qp", "Rate control P QP", "Rate control P QP for CBR", 0, 51,
                                                    DEFAULT_P_QP,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_B_QP,
                                  g_param_spec_uint("b-qp", "Rate control B QP", "Rate control B QP for CBR", 0, 51,
                                                    DEFAULT_B_QP,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class, PROP_VIDEO_PROFILE,
                                  g_param_spec_enum("profile", "video encode profile", "Profile for video encoder.",
                                                    GST_CNVIDEOENC_VIDEO_PROFILE, DEFAULT_VIDEO_PROFILE,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_VIDEO_LEVEL,
                                  g_param_spec_enum("level", "video encode level", "Level for video encoder.",
                                                    GST_CNVIDEOENC_VIDEO_LEVEL, DEFAULT_VIDEO_LEVEL,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(
    gobject_class, PROP_I_FRAME_INTERVAL,
    g_param_spec_uint("i-frame-interval", "Intra frame's interval", "P frame number between two I frame", 0, 4095,
                      DEFAULT_I_FRAME_INTERVAL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_B_FRAME_NUM,
                                  g_param_spec_uint("b-frame-num", "P frame interval",
                                                    "B frame number between two P frame", 0, 4095, DEFAULT_B_FRAME_NUM,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_GOP_TYPE,
                                  g_param_spec_enum("gop-type", "sequence GOP type", "frame order in GOP",
                                                    GST_CNVIDEOENC_GOP_TYPE, DEFAULT_GOP_TYPE,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple(gstelement_class, "cnvideo_enc", "Generic/Encoder", "Cambricon video encoder",
                                       "Cambricon Solution SDK");

  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&src_factory));
  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_cnvideoenc_init(GstCnvideoenc* self)
{
  self->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  gst_pad_set_event_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_cnvideoenc_sink_event));
  gst_pad_set_chain_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_cnvideoenc_chain));
  GST_PAD_SET_ACCEPT_INTERSECT(self->sinkpad);
  gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  GST_PAD_SET_ACCEPT_INTERSECT(self->srcpad);
  gst_element_add_pad(GST_ELEMENT(self), self->srcpad);

  auto priv = gst_cnvideoenc_get_private(self);

  self->silent = DEFAULT_SILENT;
  self->device_id = DEFAULT_DEVICE_ID;
  self->input_buffer_num = DEFAULT_INPUT_BUFFER_NUM;
  self->output_buffer_num = DEFAULT_OUTPUT_BUFFER_NUM;
  self->video_profile = DEFAULT_VIDEO_PROFILE;
  self->video_level = DEFAULT_VIDEO_LEVEL;
  self->i_frame_interval = DEFAULT_I_FRAME_INTERVAL;
  self->b_frame_num = DEFAULT_B_FRAME_NUM;
  self->gop_type = DEFAULT_GOP_TYPE;
  self->codec_type = DEFAULT_CODEC_TYPE;

  priv->encode = nullptr;
  priv->cpp = new GstCnvideoencPrivateCpp;
  priv->send_eos = FALSE;
  priv->got_eos = FALSE;
  priv->frame_id = 0;

  priv->rate_control.rcMode = DEFAULT_RC_VBR ? CNVIDEOENC_RATE_CTRL_VBR : CNVIDEOENC_RATE_CTRL_CBR;
  priv->rate_control.gopLength = DEFAULT_GOP_LENGTH;
  priv->rate_control.targetBitrate = DEFAULT_RC_BIT_RATE;
  priv->rate_control.peakBitrate = DEFAULT_RC_MAX_BIT_RATE;
  priv->rate_control.maxIQP = DEFAULT_MAX_QP;
  priv->rate_control.maxPQP = DEFAULT_MAX_QP;
  priv->rate_control.maxBQP = DEFAULT_MAX_QP;
  priv->rate_control.minIQP = DEFAULT_MIN_QP;
  priv->rate_control.minPQP = DEFAULT_MIN_QP;
  priv->rate_control.minBQP = DEFAULT_MIN_QP;
  priv->rate_control.constIQP = DEFAULT_I_QP;
  priv->rate_control.constPQP = DEFAULT_P_QP;
  priv->rate_control.constBQP = DEFAULT_B_QP;
}

static void
gst_cnvideoenc_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
  GstCnvideoenc* self = GST_CNVIDEOENC(object);
  auto priv = gst_cnvideoenc_get_private(self);

  switch (prop_id) {
    case PROP_SILENT:
      self->silent = g_value_get_boolean(value);
      break;
    case PROP_DEVICE_ID:
      self->device_id = g_value_get_int(value);
      break;
    case PROP_INPUT_BUFFER_NUM:
      self->input_buffer_num = g_value_get_uint(value);
      break;
    case PROP_OUTPUT_BUFFER_NUM:
      self->output_buffer_num = g_value_get_uint(value);
      break;
    case PROP_RC_VBR:
      priv->rate_control.rcMode = g_value_get_boolean(value) ? CNVIDEOENC_RATE_CTRL_VBR : CNVIDEOENC_RATE_CTRL_CBR;
      break;
    case PROP_GOP_LENGTH:
      priv->rate_control.gopLength = g_value_get_uint(value);
      break;
    case PROP_RC_BIT_RATE:
      priv->rate_control.targetBitrate = g_value_get_uint(value);
      break;
    case PROP_RC_MAX_BIT_RATE:
      priv->rate_control.peakBitrate = g_value_get_uint(value);
      break;
    case PROP_I_QP:
      priv->rate_control.constIQP = g_value_get_uint(value);
      break;
    case PROP_P_QP:
      priv->rate_control.constPQP = g_value_get_uint(value);
      break;
    case PROP_B_QP:
      priv->rate_control.constBQP = g_value_get_uint(value);
      break;
    case PROP_MAX_QP:
      priv->rate_control.maxIQP = g_value_get_uint(value);
      priv->rate_control.maxPQP = priv->rate_control.maxIQP;
      priv->rate_control.maxBQP = priv->rate_control.maxIQP;
      break;
    case PROP_MIN_QP:
      priv->rate_control.minIQP = g_value_get_uint(value);
      priv->rate_control.minPQP = priv->rate_control.minIQP;
      priv->rate_control.minBQP = priv->rate_control.minIQP;
      break;

    case PROP_VIDEO_PROFILE:
      self->video_profile = static_cast<GstVideoProfile>(g_value_get_enum(value));
      break;
    case PROP_VIDEO_LEVEL:
      self->video_level = static_cast<GstVideoLevel>(g_value_get_enum(value));
      break;
    case PROP_I_FRAME_INTERVAL:
      self->i_frame_interval = g_value_get_uint(value);
      break;
    case PROP_B_FRAME_NUM:
      self->b_frame_num = g_value_get_uint(value);
      break;
    case PROP_GOP_TYPE:
      self->gop_type = static_cast<cnvideoEncGopType>(g_value_get_enum(value));
      break;
    case PROP_CODEC_TYPE:
      self->codec_type = static_cast<cncodecType>(g_value_get_enum(value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void
gst_cnvideoenc_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
  GstCnvideoenc* self = GST_CNVIDEOENC(object);
  auto priv = gst_cnvideoenc_get_private(self);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean(value, self->silent);
      break;
    case PROP_DEVICE_ID:
      g_value_set_int(value, self->device_id);
      break;
    case PROP_INPUT_BUFFER_NUM:
      g_value_set_uint(value, self->input_buffer_num);
      break;
    case PROP_OUTPUT_BUFFER_NUM:
      g_value_set_uint(value, self->output_buffer_num);
      break;

    case PROP_RC_VBR:
      g_value_set_boolean(value, priv->rate_control.rcMode == CNVIDEOENC_RATE_CTRL_VBR ? TRUE : FALSE);
      break;
    case PROP_GOP_LENGTH:
      g_value_set_uint(value, priv->rate_control.gopLength);
      break;
    case PROP_RC_BIT_RATE:
      g_value_set_uint(value, priv->rate_control.targetBitrate);
      break;
    case PROP_RC_MAX_BIT_RATE:
      g_value_set_uint(value, priv->rate_control.peakBitrate);
      break;
    case PROP_I_QP:
      g_value_set_uint(value, priv->rate_control.constIQP);
      break;
    case PROP_P_QP:
      g_value_set_uint(value, priv->rate_control.constPQP);
      break;
    case PROP_B_QP:
      g_value_set_uint(value, priv->rate_control.constBQP);
      break;
    case PROP_MAX_QP:
      g_value_set_uint(value, priv->rate_control.maxIQP);
      break;
    case PROP_MIN_QP:
      g_value_set_uint(value, priv->rate_control.minIQP);
      break;

    case PROP_VIDEO_PROFILE:
      g_value_set_enum(value, self->video_profile);
      break;
    case PROP_VIDEO_LEVEL:
      g_value_set_enum(value, self->video_level);
      break;
    case PROP_I_FRAME_INTERVAL:
      g_value_set_uint(value, self->i_frame_interval);
      break;
    case PROP_B_FRAME_NUM:
      g_value_set_uint(value, self->b_frame_num);
      break;
    case PROP_GOP_TYPE:
      g_value_set_enum(value, self->gop_type);
      break;
    case PROP_CODEC_TYPE:
      g_value_set_enum(value, self->codec_type);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

/* 2. GstElement vmethod implementations */

static GstStateChangeReturn
gst_cnvideoenc_change_state(GstElement* element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstCnvideoencClass* klass = GST_CNVIDEOENC_GET_CLASS(element);
  GstCnvideoenc* self = GST_CNVIDEOENC(element);

  ret = GST_ELEMENT_CLASS(PARENT_CLASS)->change_state(element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      klass->destroy_encoder(self);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }

  return ret;
}

static inline cncodecPixelFormat
pixel_format_cast(GstCnvideoenc* self, GstVideoFormat f)
{
  switch (f) {
    case GST_VIDEO_FORMAT_NV21:
      return CNCODEC_PIX_FMT_NV21;
    case GST_VIDEO_FORMAT_NV12:
      return CNCODEC_PIX_FMT_NV12;
    case GST_VIDEO_FORMAT_I420:
      return CNCODEC_PIX_FMT_I420;
    case GST_VIDEO_FORMAT_BGRA:
      return CNCODEC_PIX_FMT_BGRA;
    case GST_VIDEO_FORMAT_RGBA:
      return CNCODEC_PIX_FMT_RGBA;
    case GST_VIDEO_FORMAT_ARGB:
      return CNCODEC_PIX_FMT_ARGB;
    case GST_VIDEO_FORMAT_ABGR:
      return CNCODEC_PIX_FMT_ABGR;
    default:
      GST_ERROR_OBJECT(self, "unsupported input video pixel format(%d)", self->video_info.finfo->format);
      return CNCODEC_PIX_FMT_RAW;
  }
}

static gboolean
gst_cnvideoenc_set_caps(GstCnvideoenc* self, GstCaps* caps)
{
  GstCaps* src_caps = NULL;
  auto priv = gst_cnvideoenc_get_private(self);

  GST_INFO_OBJECT(self, "cnvideoenc set caps");

  if (gst_video_info_from_caps(&self->video_info, caps) != TRUE) {
    GST_ERROR_OBJECT(self, "invalid caps");
    return FALSE;
  }

  if (self->video_info.width * self->video_info.height == 0) {
    GST_ERROR_OBJECT(self, "invalid caps width and height");
    return FALSE;
  }

  priv->pixel_format = pixel_format_cast(self, self->video_info.finfo->format);
  if (priv->pixel_format == CNCODEC_PIX_FMT_RAW)
    return FALSE;

  // invalid fps
  if (self->video_info.fps_n == 0 || self->video_info.fps_d == 0) {
    GST_INFO_OBJECT(self, "use default framerate 30000/1001");
    self->video_info.fps_n = 30000;
    self->video_info.fps_d = 1001;
  } else {
    GST_INFO_OBJECT(self, "use framerate %u/%u", self->video_info.fps_n, self->video_info.fps_d);
  }

  // config src caps
  GstCnvideoencClass* klass = GST_CNVIDEOENC_GET_CLASS(self);
  src_caps = gst_caps_from_string("video/x-h264; video/x-h265;");

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
  peer_caps = gst_caps_truncate(gst_caps_normalize(peer_caps));

  /* Set src caps */
  try {
    if (self->codec_type == CNCODEC_H264) {
      gst_caps_set_simple(peer_caps, "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "nal",
                          NULL);
      gst_caps_set_simple(peer_caps, "width", G_TYPE_INT, self->video_info.width, "height", G_TYPE_INT,
                          self->video_info.height, "framerate", GST_TYPE_FRACTION, self->video_info.fps_n,
                          self->video_info.fps_d, NULL);
      gst_caps_set_simple(peer_caps, "profile", G_TYPE_STRING, g_profile_table.at(self->video_profile).str.c_str(),
                          "level", G_TYPE_STRING, g_level_table.at(self->video_level).level_str.c_str(), NULL);
    } else if (self->codec_type == CNCODEC_HEVC) {
      gst_caps_set_simple(peer_caps, "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "nal",
                          NULL);
      gst_caps_set_simple(peer_caps, "width", G_TYPE_INT, self->video_info.width, "height", G_TYPE_INT,
                          self->video_info.height, "framerate", GST_TYPE_FRACTION, self->video_info.fps_n,
                          self->video_info.fps_d, NULL);
      gst_caps_set_simple(peer_caps, "profile", G_TYPE_STRING, g_profile_table.at(self->video_profile).str.c_str(),
                          "level", G_TYPE_STRING, g_level_table.at(self->video_level).level_str.c_str(), "tier",
                          G_TYPE_STRING, g_level_table.at(self->video_level).tier_str.c_str(), NULL);
    } else {
      GST_ERROR_OBJECT(self, "Unsupported codec type %d", static_cast<int>(self->codec_type));
      return FALSE;
    }
  } catch (std::out_of_range& e) {
    GST_ERROR_OBJECT(self, "%s, wrong profile or level", e.what());
    return FALSE;
  }

  GST_INFO_OBJECT(self, "cnvideoenc setcaps %" GST_PTR_FORMAT, peer_caps);
  gst_pad_use_fixed_caps(self->srcpad);
  gboolean ret = gst_pad_set_caps(self->srcpad, peer_caps);
  gst_caps_unref(peer_caps);

  if (!ret) {
    GST_ERROR_OBJECT(self, "Set pad failed");
    return FALSE;
  }

  if (priv->encode) {
    if (!klass->destroy_encoder(self)) {
      GST_ERROR_OBJECT(self, "gst_cnvideoenc_destroy_encoder() failed");
      return FALSE;
    }
  }

  if (TRUE != klass->init_encoder(self)) {
    GST_ERROR_OBJECT(self, "gst_cnvideoenc_init_encoder() failed");
    return FALSE;
  }

  return TRUE;
}

static gboolean
feed_eos(GstCnvideoenc* self)
{
  auto priv = gst_cnvideoenc_get_private(self);
  if (!priv->encode)
    return TRUE;

  cnvideoEncInput input;
  memset(&input, 0, sizeof(cnvideoEncInput));
  int ecode = cnvideoEncWaitAvailInputBuf(priv->encode, &input.frame, 10000);
  if (CNCODEC_SUCCESS != ecode) {
    GST_CNVIDEOENC_ERROR(self, RESOURCE, FAILED, ("cnvideoEncWaitAvailInputBuf failed. Error code: %d", ecode));
    return FALSE;
  }
  input.flags |= CNVIDEOENC_FLAG_EOS;
  ecode = cnvideoEncFeedFrame(priv->encode, &input, 10000);
  if (CNCODEC_SUCCESS != ecode) {
    GST_CNVIDEOENC_ERROR(self, STREAM, ENCODE, ("cnvideoEncFeedFrame failed. Error code: %d", ecode));
    return FALSE;
  }

  priv->send_eos = TRUE;
  return TRUE;
}

/* this function handles sink events */
static gboolean
gst_cnvideoenc_sink_event(GstPad* pad, GstObject* parent, GstEvent* event)
{
  GstCnvideoenc* self;
  gboolean ret;

  self = GST_CNVIDEOENC(parent);

  GST_LOG_OBJECT(self, "Received %s event: %" GST_PTR_FORMAT, GST_EVENT_TYPE_NAME(event), event);

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
      GstCaps* caps;
      gst_event_parse_caps(event, &caps);
      ret = gst_cnvideoenc_set_caps(self, caps);
      if (!ret) {
        GST_ERROR_OBJECT(self, "set caps failed");
      }
      gst_event_unref(event);
      break;
    }
    case GST_EVENT_EOS: {
      ret = feed_eos(self);
      gst_event_unref(event);
      break;
    }
    default:
      ret = gst_pad_event_default(pad, parent, event);
      break;
  }
  return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_cnvideoenc_chain(GstPad* pad, GstObject* parent, GstBuffer* buf)
{
  GstCnvideoenc* self;
  self = GST_CNVIDEOENC(parent);

  if (!encode_frame(self, buf))
    GST_CNVIDEOENC_ERROR(self, STREAM, ENCODE, ("encode failed"));
  gst_buffer_unref(buf);
  return GST_FLOW_OK;
}

static void
print_create_attr(cnvideoEncCreateInfo* p_attr)
{
  printf("%-32s%s\n", "param", "value");
  printf("-------------------------------------\n");
  printf("%-32s%u\n", "Codectype", p_attr->codec);
  printf("%-32s%u\n", "PixelFormat", p_attr->pixelFmt);
  printf("%-32s%u\n", "Instance", p_attr->instance);
  printf("%-32s%u\n", "DeviceID", p_attr->deviceId);
  printf("%-32s%u\n", "MemoryAllocType", p_attr->allocType);
  printf("%-32s%u\n", "Width", p_attr->width);
  printf("%-32s%u\n", "Height", p_attr->height);
  printf("%-32s%u\n", "FrameRateNum", p_attr->fpsNumerator);
  printf("%-32s%u\n", "FrameRateDen", p_attr->fpsDenominator);
  printf("%-32s%u\n", "ColorSpaceStandard", p_attr->colorSpace);
  printf("%-32s%u\n", "RateCtrlMode", p_attr->rateCtrl.rcMode);
  printf("%-32s%u\n", "InputBufferNumber", p_attr->inputBufNum);
  printf("%-32s%u\n", "OutputBufferNumber", p_attr->outputBufNum);
}

/* 3. GstCnvideoenc method implementations */
static gboolean
gst_cnvideoenc_init_encoder(GstCnvideoenc* self)
{
  GST_INFO_OBJECT(self, "Create cncodec encoder instance");
  auto priv = gst_cnvideoenc_get_private(self);

  g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id), FALSE);

  // start event loop
  priv->cpp->event_loop = std::thread(&event_task_runner, self);
  priv->first_frame = true;

  cnvideoEncCreateInfo params;
  memset(&params, 0, sizeof(params));

  params.deviceId = self->device_id;
  params.width = self->video_info.width;
  params.height = self->video_info.height;
  params.pixelFmt = priv->pixel_format;
  params.colorSpace = COLOR_SPACE;
  params.codec = self->codec_type;
  params.instance = CNVIDEOENC_INSTANCE_AUTO;
  params.userContext = reinterpret_cast<void*>(self);
  params.inputBuf = nullptr;
  params.outputBuf = nullptr;
  params.inputBufNum = self->input_buffer_num;
  params.outputBufNum = self->output_buffer_num;
  params.allocType = CNCODEC_BUF_ALLOC_LIB;
  params.suggestedLibAllocBitStrmBufSize = ENCODE_BUFFER_SIZE;
  params.fpsNumerator = self->video_info.fps_n;
  params.fpsDenominator = self->video_info.fps_d;
  params.rateCtrl = priv->rate_control;

  cnvideoEncProfile profile;
  cnvideoEncLevel level;
  try {
    profile = g_profile_table.at(self->video_profile).prof;
    level = g_level_table.at(self->video_level).level;
  } catch (std::out_of_range& e) {
    GST_ERROR_OBJECT(self, "%s, wrong profile or level", e.what());
    return FALSE;
  }

  if (params.codec == CNCODEC_H264) {
    memset(&params.uCfg.h264, 0x0, sizeof(params.uCfg.h264));
    if (static_cast<int>(profile) > static_cast<int>(CNVIDEOENC_PROFILE_H264_HIGH_10)) {
      GST_WARNING_OBJECT(self, "Invalid H264 profile, using H264_MAIN as default");
      params.uCfg.h264.profile = CNVIDEOENC_PROFILE_H264_HIGH;
    } else {
      params.uCfg.h264.profile = profile;
    }
    if (static_cast<int>(level) > static_cast<int>(CNVIDEOENC_LEVEL_H264_51)) {
      GST_WARNING_OBJECT(self, "Invalid H264 level, using H264_41 as default");
      params.uCfg.h264.level = CNVIDEOENC_LEVEL_H264_41;
    } else {
      params.uCfg.h264.level = level;
    }
    params.uCfg.h264.IframeInterval = self->i_frame_interval;
    params.uCfg.h264.BFramesNum = self->b_frame_num;
    params.uCfg.h264.insertSpsPpsWhenIDR = 1;
    params.uCfg.h264.gopType = self->gop_type;
    // use default entropy mode
    params.uCfg.h264.entropyMode = CNVIDEOENC_ENTROPY_MODE_CAVLC;
  } else if (params.codec == CNCODEC_HEVC) {
    memset(&params.uCfg.h265, 0x0, sizeof(params.uCfg.h265));
    if (static_cast<int>(profile) < static_cast<int>(CNVIDEOENC_PROFILE_H265_MAIN)) {
      GST_WARNING_OBJECT(self, "Invalid H265 profile, using H265_MAIN as default");
      params.uCfg.h265.profile = CNVIDEOENC_PROFILE_H265_MAIN;
    } else {
      params.uCfg.h265.profile = profile;
    }
    if (static_cast<int>(level) < static_cast<int>(CNVIDEOENC_LEVEL_H265_MAIN_1)) {
      GST_WARNING_OBJECT(self, "Invalid H265 level, using H265_MAIN_41 as default");
      params.uCfg.h265.level = CNVIDEOENC_LEVEL_H265_HIGH_41;
    } else {
      params.uCfg.h265.level = level;
    }
    params.uCfg.h265.IframeInterval = self->i_frame_interval;
    params.uCfg.h265.BFramesNum = self->b_frame_num;
    params.uCfg.h265.insertSpsPpsWhenIDR = 1;
    params.uCfg.h265.gopType = self->gop_type;
  } else {
    GST_ERROR_OBJECT(self, "Encoder only support format H264/H265");
    return FALSE;
  }

  if (!self->silent) {
    print_create_attr(&params);
  }

  int ecode = cnvideoEncCreate(&priv->encode, event_handler, &params);
  if (CNCODEC_SUCCESS != ecode) {
    priv->encode = nullptr;
    GST_CNVIDEOENC_ERROR(self, LIBRARY, INIT, ("Create video encoder failed. Error code: %d", ecode));
    return FALSE;
  }
  GST_INFO_OBJECT(self, "Init video encoder succeeded");

  return TRUE;
}

static gboolean
gst_cnvideoenc_destroy_encoder(GstCnvideoenc* self)
{
  auto priv = gst_cnvideoenc_get_private(self);

  /**
   * Release resources.
   */
  std::unique_lock<std::mutex> eos_lk(priv->cpp->eos_mtx);
  if (!priv->got_eos) {
    if (!priv->send_eos && priv->encode) {
      eos_lk.unlock();
      GST_INFO_OBJECT(self, "Send EOS in destruct");
      feed_eos(self);
    } else {
      if (!priv->encode)
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

  if (priv->encode) {
    // destroy vpu encoder
    GST_INFO_OBJECT(self, "Destroy video encoder channel");
    auto ecode = cnvideoEncDestroy(priv->encode);
    if (CNCODEC_SUCCESS != ecode) {
      GST_CNVIDEOENC_ERROR(self, LIBRARY, SHUTDOWN, ("Encoder destroy failed Error Code: %d", ecode));
    }
    priv->encode = nullptr;
  }

  return TRUE;
}

static gboolean
copy_frame(GstCnvideoenc* self, cncodecFrame* dst, const GstVideoFrame& src)
{
  edk::MluMemoryOp mem_op;
  auto priv = gst_cnvideoenc_get_private(self);
  auto frame_size = src.info.width * src.info.height;
  // cnrtRet_t cnrt_ecode = CNRT_RET_SUCCESS;
  switch (priv->pixel_format) {
    case CNCODEC_PIX_FMT_NV12:
    case CNCODEC_PIX_FMT_NV21: {
      GST_DEBUG_OBJECT(self, "Copy frame luminance");
      mem_op.MemcpyH2D(reinterpret_cast<void*>(dst->plane[0].addr), src.data[0], frame_size, 1);
      GST_DEBUG_OBJECT(self, "Copy frame chroma");
      mem_op.MemcpyH2D(reinterpret_cast<void*>(dst->plane[1].addr), src.data[1], frame_size >> 1, 1);
      break;
    }
    case CNCODEC_PIX_FMT_I420: {
      GST_DEBUG_OBJECT(self, "Copy frame luminance");
      mem_op.MemcpyH2D(reinterpret_cast<void*>(dst->plane[0].addr), src.data[0], frame_size, 1);
      GST_DEBUG_OBJECT(self, "Copy frame chroma 0");
      mem_op.MemcpyH2D(reinterpret_cast<void*>(dst->plane[1].addr), src.data[1], frame_size >> 2, 1);
      GST_DEBUG_OBJECT(self, "Copy frame chroma 1");
      mem_op.MemcpyH2D(reinterpret_cast<void*>(dst->plane[2].addr), src.data[2], frame_size >> 2, 1);
      break;
    }
    case CNCODEC_PIX_FMT_ARGB:
    case CNCODEC_PIX_FMT_ABGR:
    case CNCODEC_PIX_FMT_RGBA:
    case CNCODEC_PIX_FMT_BGRA:
      GST_DEBUG_OBJECT(self, "Copy frame RGB family");
      mem_op.MemcpyH2D(reinterpret_cast<void*>(dst->plane[0].addr), src.data[0], frame_size << 2, 1);
      break;
    default:
      GST_CNVIDEOENC_ERROR(self, STREAM, FORMAT, ("Unsupported pixel format"));
      return FALSE;
  }
  return TRUE;
}

gboolean
encode_frame(GstCnvideoenc* self, GstBuffer* buf)
{
  gboolean ret = TRUE;
  GstVideoFrame gst_frame;
  cnvideoEncInput input;
  memset(&input, 0, sizeof(cnvideoEncInput));
  auto priv = gst_cnvideoenc_get_private(self);

  // prepare CNRT environment
  thread_local bool cnrt_env = false;
  if (!cnrt_env) {
    g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id), FALSE);
    cnrt_env = true;
  }

  if (!gst_video_frame_map(&gst_frame, &self->video_info, buf, GST_MAP_READ)) {
    GST_WARNING_OBJECT(self, "buffer map failed %" GST_PTR_FORMAT, buf);
    return FALSE;
  }

  int ecode = cnvideoEncWaitAvailInputBuf(priv->encode, &input.frame, 10000);
  if (CNCODEC_SUCCESS != ecode) {
    GST_CNVIDEOENC_ERROR(self, RESOURCE, FAILED, ("cnvideoEncWaitAvailInputBuf failed. Error code: %d", ecode));
    return FALSE;
  }

  if (!copy_frame(self, &input.frame, gst_frame)) {
    gst_video_frame_unmap(&gst_frame);
    return FALSE;
  }
  input.frame.pixelFmt = priv->pixel_format;
  input.frame.colorSpace = COLOR_SPACE;
  input.frame.width = gst_frame.info.width;
  input.frame.height = gst_frame.info.height;
  input.pts = GST_BUFFER_PTS(buf);
  for (uint32_t i = 0; i < GST_VIDEO_INFO_N_PLANES(&(gst_frame.info)); ++i) {
    input.frame.stride[i] = gst_frame.info.stride[i];
  }

  GST_DEBUG_OBJECT(self, "Feed video frame info) data: %p, length: %lu, ptr: %lu", gst_frame.data[0],
                   gst_frame.info.size, input.pts);
  // send data to codec
  if (priv->encode) {
    ecode = cnvideoEncFeedFrame(priv->encode, &input, 10000);
    if (CNCODEC_SUCCESS != ecode) {
      GST_CNVIDEOENC_ERROR(self, STREAM, ENCODE, ("cnvideoEncFeedFrame failed. Error code: %d", ecode));
      ret = FALSE;
    }
  } else {
    ret = FALSE;
  }

  gst_video_frame_unmap(&gst_frame);

  return ret;
}

static void
abort_encoder(GstCnvideoenc* self)
{
  GST_WARNING_OBJECT(self, "Abort encoder");
  GstCnvideoencPrivate* priv = gst_cnvideoenc_get_private(self);
  if (priv->encode) {
    cnvideoEncAbort(priv->encode);
    priv->encode = nullptr;
    handle_eos(self);

    std::unique_lock<std::mutex> eos_lk(priv->cpp->eos_mtx);
    priv->got_eos = TRUE;
    priv->cpp->eos_cond.notify_one();
  } else {
    GST_CNVIDEOENC_ERROR(self, LIBRARY, SHUTDOWN, ("Won't do abort, since cncodec handler has not been initialized"));
  }
}

static void
event_task_runner(GstCnvideoenc* self)
{
  GstCnvideoencPrivate* priv = gst_cnvideoenc_get_private(self);
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
        GST_CNVIDEOENC_ERROR(self, LIBRARY, FAILED, ("Encode firmware crash event"));
        abort_encoder(self);
        break;
      case CNCODEC_CB_EVENT_OUT_OF_MEMORY:
        GST_CNVIDEOENC_ERROR(self, LIBRARY, FAILED, ("Out of memory error thrown from cncodec"));
        abort_encoder(self);
        break;
      case CNCODEC_CB_EVENT_ABORT_ERROR:
        GST_CNVIDEOENC_ERROR(self, LIBRARY, FAILED, ("Abort error thrown from cncodec"));
        abort_encoder(self);
        break;
#if CNCODEC_VERSION >= 10600
      case CNCODEC_CB_EVENT_STREAM_CORRUPT:
        GST_WARNING_OBJECT(self, "Stream corrupt, discard frame");
        break;
#endif
      default:
        GST_CNVIDEOENC_ERROR(self, LIBRARY, FAILED, ("Unknown event type"));
        abort_encoder(self);
        break;
    }

    lock.lock();
  }
}

static i32_t
event_handler(cncodecCbEventType type, void* user_data, void* package)
{
  auto handler = reinterpret_cast<GstCnvideoenc*>(user_data);
  // [ACQUIRED BY CNCODEC]
  // NEW_FRAME event must handled in callback thread,
  // The other events must handled in a different thread.
  if (handler != nullptr) {
    switch (type) {
      case CNCODEC_CB_EVENT_NEW_FRAME:
        handle_output(handler, reinterpret_cast<cnvideoEncOutput*>(package));
        break;
      default:
        handle_event(handler, type);
        break;
    }
  }
  return 0;
}

static void
handle_event(GstCnvideoenc* self, cncodecCbEventType type)
{
  GstCnvideoencPrivate* priv = gst_cnvideoenc_get_private(self);
  std::lock_guard<std::mutex> lock(priv->cpp->event_mtx);
  priv->cpp->event_queue.push(type);
  priv->cpp->event_cond.notify_one();
}

static void
handle_eos(GstCnvideoenc* self)
{
  GST_INFO_OBJECT(self, "receive EOS from cncodec");
  GstCnvideoencPrivate* priv = gst_cnvideoenc_get_private(self);
  std::unique_lock<std::mutex> eos_lk(priv->cpp->eos_mtx);
  priv->got_eos = TRUE;
  priv->cpp->eos_cond.notify_all();

  if (GST_STATE(GST_ELEMENT_CAST(self)) <= GST_STATE_READY)
    return;

  gst_pad_push_event(self->srcpad, gst_event_new_eos());
}

static void
handle_output(GstCnvideoenc* self, cnvideoEncOutput* packet)
{
  GST_TRACE_OBJECT(self, "handle_output(%p,%u,%lu,%d)",
                   reinterpret_cast<void*>(packet->streamBuffer.addr + packet->dataOffset), packet->streamLength,
                   packet->pts, static_cast<int>(self->codec_type));

  // prepare CNRT environment
  thread_local bool cnrt_env = false;
  if (!cnrt_env) {
    g_return_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id));
    cnrt_env = true;
  }

  GstMapInfo info;
  GstBuffer* buffer = gst_buffer_new_allocate(NULL, packet->streamLength, NULL);

  gst_buffer_map(buffer, &info, GST_MAP_WRITE);
  auto ret = cnrtMemcpy(info.data, reinterpret_cast<void*>(packet->streamBuffer.addr + packet->dataOffset),
                        packet->streamLength, CNRT_MEM_TRANS_DIR_DEV2HOST);
  if (ret != CNRT_RET_SUCCESS) {
    GST_CNVIDEOENC_ERROR(self, RESOURCE, FAILED, ("Copy bitstream failed, DEV2HOST"));
    gst_buffer_unmap(buffer, &info);
    abort_encoder(self);
    return;
  }
  gst_buffer_unmap(buffer, &info);

  GstCnvideoencPrivate* priv = gst_cnvideoenc_get_private(self);
  if (packet->pts == 0 && !priv->first_frame) {
    GST_BUFFER_PTS(buffer) = (priv->frame_id++) * 1e9 / self->video_info.fps_n * self->video_info.fps_d;
  } else {
    GST_BUFFER_PTS(buffer) = packet->pts;
  }
  if (priv->first_frame)
    priv->first_frame = false;

  gst_pad_push(self->srcpad, buffer);
}
