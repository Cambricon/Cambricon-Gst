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

#include "gstcnencode.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <cstring>
#include <functional>
#include <map>

#include "common/data_type.h"
#include "common/mlu_memory_meta.h"
#include "common/mlu_utils.h"
#include "easycodec/easy_encode.h"
#include "easycodec/vformat.h"
#include "easyinfer/mlu_context.h"

GST_DEBUG_CATEGORY_EXTERN(gst_cambricon_debug);
#define GST_CAT_DEFAULT gst_cambricon_debug

#define DEFAULT_DEVICE_ID 0
#define DEFAULT_SILENT FALSE

#define DEFAULT_RC_VBR FALSE
#define DEFAULT_RC_GOP 30
#define DEFAULT_RC_BIT_RATE 1024
#define DEFAULT_RC_MAX_BIT_RATE 2048
#define DEFAULT_RC_MAX_QP 50
#define DEFAULT_RC_MIN_QP 25

#define DEFAULT_CROP_ENABLE FALSE
#define DEFAULT_CROP_X 0
#define DEFAULT_CROP_Y 0
#define DEFAULT_CROP_W 0
#define DEFAULT_CROP_H 0

#define DEFAULT_JPEG_QUALITY 80
#define DEFAULT_VIDEO_PROFILE GST_VP_H264_MAIN
#define DEFAULT_VIDEO_LEVEL GST_VL_H264_41
#define DEFAULT_P_FRAME_NUM 0
#define DEFAULT_B_FRAME_NUM 0
#define DEFAULT_CABAC_INIT_IDC 0
#define DEFAULT_GOP_TYPE 0

/* self args */

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_DEVICE_ID,

  PROP_JPEG_QUALITY,
  PROP_VIDEO_PROFILE,
  PROP_VIDEO_LEVEL,

  PROP_P_FRAME_NUM,
  PROP_B_FRAME_NUM,
  PROP_CABAC_INIT_IDC,
  PROP_GOP_TYPE,

  // rate control
  PROP_RC_VBR,
  PROP_RC_GOP,
  /* for CBR only */
  PROP_RC_BIT_RATE,
  /* for VBR only */
  PROP_RC_MAX_BIT_RATE,
  PROP_RC_MAX_QP,
  PROP_RC_MIN_QP,

  // crop config
  PROP_CROP_ENABLE,
  PROP_CROP_X,
  PROP_CROP_Y,
  PROP_CROP_W,
  PROP_CROP_H
};

using edk::VideoLevel;
using edk::VideoProfile;

struct VideoProfileInfo
{
  edk::VideoProfile prof;
  std::string str;
};
#define PROFILE_INFO_PAIR(prof, edk_prof, str)                                                                         \
  {                                                                                                                    \
    prof, { edk_prof, str }                                                                                            \
  }
static const std::map<GstVideoProfile, VideoProfileInfo> g_profile_table = {
  PROFILE_INFO_PAIR(GST_VP_0, VideoProfile::PROFILE_MAX, "wrong profile"),
  PROFILE_INFO_PAIR(GST_VP_H264_BASELINE, VideoProfile::H264_BASELINE, "baseline"),
  PROFILE_INFO_PAIR(GST_VP_H264_MAIN, VideoProfile::H264_MAIN, "main"),
  PROFILE_INFO_PAIR(GST_VP_H264_HIGH, VideoProfile::H264_HIGH, "high"),
  PROFILE_INFO_PAIR(GST_VP_H264_HIGH_10, VideoProfile::H264_HIGH_10, "high-10"),
  PROFILE_INFO_PAIR(GST_VP_H265_MAIN, VideoProfile::H265_MAIN, "main"),
  PROFILE_INFO_PAIR(GST_VP_H265_MAIN_STILL, VideoProfile::H265_MAIN_STILL, "main-still-picture"),
  PROFILE_INFO_PAIR(GST_VP_H265_MAIN_INTRA, VideoProfile::H265_MAIN_INTRA, "main-intra"),
  PROFILE_INFO_PAIR(GST_VP_H265_MAIN_10, VideoProfile::H265_MAIN_10, "main-10"),
  PROFILE_INFO_PAIR(GST_VP_MAX, VideoProfile::PROFILE_MAX, "wrong profile")
};

struct VideoLevelInfo
{
  edk::VideoLevel level;
  std::string level_str;
  std::string tier_str;
};
#define LEVEL_INFO_PAIR(level, edk_level, level_str, tier_str)                                                         \
  {                                                                                                                    \
    level, { edk_level, level_str, tier_str }                                                                          \
  }
static const std::map<GstVideoLevel, VideoLevelInfo> g_level_table = {
  LEVEL_INFO_PAIR(GST_VL_0, VideoLevel::LEVEL_MAX, "wrong level", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_1, VideoLevel::H264_1, "1", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_1B, VideoLevel::H264_1B, "1b", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_11, VideoLevel::H264_11, "1.1", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_12, VideoLevel::H264_12, "1.2", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_13, VideoLevel::H264_13, "1.3", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_2, VideoLevel::H264_2, "2", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_21, VideoLevel::H264_21, "2.1", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_22, VideoLevel::H264_22, "2.2", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_3, VideoLevel::H264_3, "3", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_31, VideoLevel::H264_31, "3.1", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_32, VideoLevel::H264_32, "3.2", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_4, VideoLevel::H264_4, "4", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_41, VideoLevel::H264_41, "4.1", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_42, VideoLevel::H264_42, "4.2", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_5, VideoLevel::H264_5, "5", ""),
  LEVEL_INFO_PAIR(GST_VL_H264_51, VideoLevel::H264_51, "5.1", ""),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_1, VideoLevel::H265_MAIN_1, "1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_1, VideoLevel::H265_HIGH_1, "1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_2, VideoLevel::H265_MAIN_2, "2", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_2, VideoLevel::H265_HIGH_2, "2", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_21, VideoLevel::H265_MAIN_21, "2.1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_21, VideoLevel::H265_HIGH_21, "2.1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_3, VideoLevel::H265_MAIN_3, "3", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_3, VideoLevel::H265_HIGH_3, "3", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_31, VideoLevel::H265_MAIN_31, "3.1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_31, VideoLevel::H265_HIGH_31, "3.1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_4, VideoLevel::H265_MAIN_4, "4", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_4, VideoLevel::H265_HIGH_4, "4", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_41, VideoLevel::H265_MAIN_41, "4.1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_41, VideoLevel::H265_HIGH_41, "4.1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_5, VideoLevel::H265_MAIN_5, "5", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_5, VideoLevel::H265_HIGH_5, "5", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_51, VideoLevel::H265_MAIN_51, "5.1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_51, VideoLevel::H265_HIGH_51, "5.1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_52, VideoLevel::H265_MAIN_52, "5.2", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_52, VideoLevel::H265_HIGH_52, "5.2", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_6, VideoLevel::H265_MAIN_6, "6", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_6, VideoLevel::H265_HIGH_6, "6", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_61, VideoLevel::H265_MAIN_61, "6.1", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_61, VideoLevel::H265_HIGH_61, "6.1", "high"),
  LEVEL_INFO_PAIR(GST_VL_H265_MAIN_62, VideoLevel::H265_MAIN_62, "6.2", "main"),
  LEVEL_INFO_PAIR(GST_VL_H265_HIGH_62, VideoLevel::H265_HIGH_62, "6.2", "high"),
  LEVEL_INFO_PAIR(GST_VL_MAX, VideoLevel::LEVEL_MAX, "wrong level", "")
};

#define GST_CNENCODE_VIDEO_PROFILE (gst_cnencode_video_profile_get_type())
static GType
gst_cnencode_video_profile_get_type(void)
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
    _id = g_enum_register_static("GstCnencodeVideoProfile", values);
    g_once_init_leave((gsize*)&id, _id);
  }
  return id;
}

#define GST_CNENCODE_VIDEO_LEVEL (gst_cnencode_video_level_get_type())
static GType
gst_cnencode_video_level_get_type(void)
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
                                       { GST_VL_H265_MAIN_1, "H265 Video Level Main 1", "H264 MAIN 1" },
                                       { GST_VL_H265_HIGH_1, "H265 Video Level High 1", "H264 HIGH 1" },
                                       { GST_VL_H265_MAIN_2, "H265 Video Level Main 2", "H264 MAIN 2" },
                                       { GST_VL_H265_HIGH_2, "H265 Video Level High 2", "H264 HIGH 2" },
                                       { GST_VL_H265_MAIN_21, "H265 Video Level Main 21", "H264 MAIN 21" },
                                       { GST_VL_H265_HIGH_21, "H265 Video Level High 21", "H264 HIGH 21" },
                                       { GST_VL_H265_MAIN_3, "H265 Video Level Main 3", "H264 MAIN 3" },
                                       { GST_VL_H265_HIGH_3, "H265 Video Level High 3", "H264 HIGH 3" },
                                       { GST_VL_H265_MAIN_31, "H265 Video Level Main 31", "H264 MAIN 31" },
                                       { GST_VL_H265_HIGH_31, "H265 Video Level High 31", "H264 HIGH 31" },
                                       { GST_VL_H265_MAIN_4, "H265 Video Level Main 4", "H264 MAIN 4" },
                                       { GST_VL_H265_HIGH_4, "H265 Video Level High 4", "H264 HIGH 4" },
                                       { GST_VL_H265_MAIN_41, "H265 Video Level Main 41", "H264 MAIN 41" },
                                       { GST_VL_H265_HIGH_41, "H265 Video Level High 41", "H264 HIGH 41" },
                                       { GST_VL_H265_MAIN_5, "H265 Video Level Main 5", "H264 MAIN 5" },
                                       { GST_VL_H265_HIGH_5, "H265 Video Level High 5", "H264 HIGH 5" },
                                       { GST_VL_H265_MAIN_51, "H265 Video Level Main 51", "H264 MAIN 51" },
                                       { GST_VL_H265_HIGH_51, "H265 Video Level High 51", "H264 HIGH 51" },
                                       { GST_VL_H265_MAIN_52, "H265 Video Level Main 52", "H264 MAIN 52" },
                                       { GST_VL_H265_HIGH_52, "H265 Video Level High 52", "H264 HIGH 52" },
                                       { GST_VL_H265_MAIN_6, "H265 Video Level Main 6", "H264 MAIN 6" },
                                       { GST_VL_H265_HIGH_6, "H265 Video Level High 6", "H264 HIGH 6" },
                                       { GST_VL_H265_MAIN_61, "H265 Video Level Main 61", "H264 MAIN 61" },
                                       { GST_VL_H265_HIGH_61, "H265 Video Level High 61", "H264 HIGH 61" },
                                       { GST_VL_H265_MAIN_62, "H265 Video Level Main 62", "H264 MAIN 62" },
                                       { GST_VL_H265_HIGH_62, "H265 Video Level High 62", "H264 HIGH 62" },
                                       { 0, NULL, NULL } };
  static volatile GType id = 0;
  if (g_once_init_enter((gsize*)&id)) {
    GType _id;
    _id = g_enum_register_static("GstCnencodeVideLevel", values);
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
                          GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{ NV21, NV12, BGRA, RGBA, ABGR, ARGB }")));

static GstStaticPadTemplate src_factory =
  GST_STATIC_PAD_TEMPLATE("src",
                          GST_PAD_SRC,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS("video/x-h264, stream-format=byte-stream, alignment=nal;"
                                          "video/x-h265, stream-format=byte-stream, alignment=nal;"
                                          "image/jpeg"));

struct GstCnencodePrivate
{
  edk::CodecType codec_type;
  edk::RateControl rate_control;
  edk::CropConfig crop_config;
  edk::PixelFmt pixel_format;

  edk::EasyEncode* encode;
};

// define GstCnencode type and shortcut function to get private
G_DEFINE_TYPE_WITH_PRIVATE(GstCnencode, gst_cnencode, GST_TYPE_ELEMENT);
#define PARENT_CLASS gst_cnencode_parent_class

static inline GstCnencodePrivate*
gst_cnencode_get_private(GstCnencode* object)
{
  return reinterpret_cast<GstCnencodePrivate*>(gst_cnencode_get_instance_private(object));
}

// method declarations
static void
gst_cnencode_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec);
static void
gst_cnencode_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec);

static gboolean
gst_cnencode_sink_event(GstPad* pad, GstObject* parent, GstEvent* event);
static GstFlowReturn
gst_cnencode_chain(GstPad* pad, GstObject* parent, GstBuffer* buf);
static gboolean
gst_cnencode_set_caps(GstCnencode* self, GstCaps* caps);
static GstStateChangeReturn
gst_cnencode_change_state(GstElement* element, GstStateChange transition);

static gboolean
gst_cnencode_create(GstCnencode* self);
static gboolean
gst_cnencode_destroy(GstCnencode* self);
static gboolean
gst_cnencode_start(GstCnencode* self);
static gboolean
gst_cnencode_stop(GstCnencode* self);

static gboolean
encode_frame(GstCnencode* self, GstBuffer* buf);
static void
handle_encode_output(GstCnencode* self, const edk::CnPacket& packet);
static void
handle_encode_eos(GstCnencode* self);

/* 1. GObject vmethod implementations */

// initialize the cnencode's class
static void
gst_cnencode_class_init(GstCnencodeClass* klass)
{
  GObjectClass* gobject_class;
  GstElementClass* gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  gobject_class->set_property = gst_cnencode_set_property;
  gobject_class->get_property = gst_cnencode_get_property;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_cnencode_change_state);

  klass->create = GST_DEBUG_FUNCPTR(gst_cnencode_create);
  klass->destroy = GST_DEBUG_FUNCPTR(gst_cnencode_destroy);
  klass->start = GST_DEBUG_FUNCPTR(gst_cnencode_start);
  klass->stop = GST_DEBUG_FUNCPTR(gst_cnencode_stop);

  g_object_class_install_property(gobject_class, PROP_SILENT,
                                  g_param_spec_boolean("silent", "Silent", "Produce verbose output ?", DEFAULT_SILENT,
                                                       (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class, PROP_DEVICE_ID,
                                  g_param_spec_int("device-id", "Device id", "Mlu device id", -1, 20, DEFAULT_DEVICE_ID,
                                                   (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class, PROP_RC_VBR,
                                  g_param_spec_boolean("rc-vbr", "Rate control VBR enable", "Rate control VBR enable",
                                                       DEFAULT_RC_VBR,
                                                       (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_RC_GOP,
                                  g_param_spec_uint("rc-gop", "GOP", "Rate control interval of ISLICE", 1, 65536,
                                                    DEFAULT_RC_GOP,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(
    gobject_class, PROP_RC_BIT_RATE,
    g_param_spec_uint("rc-bitrate", "Rate control bitrate(kbps)", "Rate control average bitrate for CBR", 2, 102400,
                      DEFAULT_RC_BIT_RATE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(
    gobject_class, PROP_RC_MAX_BIT_RATE,
    g_param_spec_uint("rc-max-bitrate", "Rate control max bitrate(kbps)", "Rate control max bitrate for VBR", 2, 102400,
                      DEFAULT_RC_MAX_BIT_RATE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_RC_MAX_QP,
                                  g_param_spec_uint("rc-max-qp", "Rate control max qp", "Rate control max qp for VBR",
                                                    0, 51, DEFAULT_RC_MAX_QP,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_RC_MIN_QP,
                                  g_param_spec_uint("rc-min-qp", "Rate control min qp", "Rate control min qp for VBR",
                                                    0, 51, DEFAULT_RC_MIN_QP,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
    gobject_class, PROP_VIDEO_PROFILE,
    g_param_spec_enum("video-profile", "video encode profile", "Profile for video encoder.", GST_CNENCODE_VIDEO_PROFILE,
                      DEFAULT_VIDEO_PROFILE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_VIDEO_LEVEL,
                                  g_param_spec_enum("video-level", "video encode level", "Level for video encoder.",
                                                    GST_CNENCODE_VIDEO_LEVEL, DEFAULT_VIDEO_LEVEL,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_P_FRAME_NUM,
                                  g_param_spec_uint("p-frame-num", "I frame interval",
                                                    "P frame number between two I frame", 0, 4095, DEFAULT_P_FRAME_NUM,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_B_FRAME_NUM,
                                  g_param_spec_uint("b-frame-num", "P frame interval",
                                                    "B frame number between two P frame", 0, 4095, DEFAULT_B_FRAME_NUM,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_CABAC_INIT_IDC,
                                  g_param_spec_uint("cabac-init-idc", "init table for CABAC",
                                                    "Init table for CABAC, 0,1,2 for H264 and 0,1 for HEVC, default 0",
                                                    0, 2, DEFAULT_CABAC_INIT_IDC,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_GOP_TYPE,
                                  g_param_spec_uint("gop-type", "sequence GOP type",
                                                    "frame order in GOP, "
                                                    "0 for bidirectional, 1 for low delay, 2 for pyramid",
                                                    0, 2, DEFAULT_GOP_TYPE,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class, PROP_CROP_ENABLE,
                                  g_param_spec_boolean("crop-enable", "Crop video", "Enable crop video",
                                                       DEFAULT_CROP_ENABLE,
                                                       (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_CROP_X,
                                  g_param_spec_uint("crop-x", "Crop left", "Crop X position", 0, 4095, DEFAULT_CROP_X,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_CROP_Y,
                                  g_param_spec_uint("crop-y", "Crop top", "Crop Y position", 0, 2159, DEFAULT_CROP_Y,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_CROP_W,
                                  g_param_spec_uint("crop-w", "Crop width", "Crop Width", 0, 4096, DEFAULT_CROP_W,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property(gobject_class, PROP_CROP_H,
                                  g_param_spec_uint("crop-h", "Crop H height", "Crop H height", 0, 2160, DEFAULT_CROP_H,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(gobject_class, PROP_JPEG_QUALITY,
                                  g_param_spec_uint("jpeg-quality", "JPEG Quality", "JPEG Encoder Quality Factor", 1,
                                                    100, DEFAULT_JPEG_QUALITY,
                                                    (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple(gstelement_class, "Cnencode", "Generic/Encoder", "Cambricon encoder",
                                       "Cambricon Video");

  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&src_factory));
  gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_cnencode_init(GstCnencode* self)
{
  self->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  gst_pad_set_event_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_cnencode_sink_event));
  gst_pad_set_chain_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_cnencode_chain));
  GST_PAD_SET_ACCEPT_INTERSECT(self->sinkpad);
  gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  GST_PAD_SET_ACCEPT_INTERSECT(self->srcpad);
  gst_element_add_pad(GST_ELEMENT(self), self->srcpad);

  auto priv = gst_cnencode_get_private(self);

  self->silent = DEFAULT_SILENT;
  self->device_id = DEFAULT_DEVICE_ID;
  priv->encode = nullptr;
  priv->rate_control.vbr = DEFAULT_RC_VBR;
  priv->rate_control.gop = DEFAULT_RC_GOP;
  priv->rate_control.frame_rate_num = 30000;
  priv->rate_control.frame_rate_den = 1001;
  priv->rate_control.bit_rate = DEFAULT_RC_BIT_RATE;
  priv->rate_control.max_bit_rate = DEFAULT_RC_MAX_BIT_RATE;
  priv->rate_control.max_qp = DEFAULT_RC_MAX_QP;
  priv->rate_control.min_qp = DEFAULT_RC_MIN_QP;
  memset(&priv->crop_config, 0, sizeof(edk::CropConfig));
  priv->crop_config.enable = DEFAULT_CROP_ENABLE;
  self->jpeg_qulity = DEFAULT_JPEG_QUALITY;
  self->video_profile = DEFAULT_VIDEO_PROFILE;
  self->video_level = DEFAULT_VIDEO_LEVEL;
  self->p_frame_num = DEFAULT_P_FRAME_NUM;
  self->b_frame_num = DEFAULT_B_FRAME_NUM;
  self->cabac_init_idc = DEFAULT_CABAC_INIT_IDC;
  self->gop_type = DEFAULT_GOP_TYPE;
}

static void
gst_cnencode_set_property(GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec)
{
  GstCnencode* self = GST_CNENCODE(object);
  auto priv = gst_cnencode_get_private(self);

  switch (prop_id) {
    case PROP_SILENT:
      self->silent = g_value_get_boolean(value);
      break;
    case PROP_DEVICE_ID:
      self->device_id = g_value_get_int(value);
      break;
    case PROP_RC_VBR:
      priv->rate_control.vbr = g_value_get_boolean(value) ? true : false;
      break;
    case PROP_RC_GOP:
      priv->rate_control.gop = g_value_get_uint(value);
      break;
    case PROP_RC_BIT_RATE:
      priv->rate_control.bit_rate = g_value_get_uint(value);
      break;
    case PROP_RC_MAX_BIT_RATE:
      priv->rate_control.max_bit_rate = g_value_get_uint(value);
      break;
    case PROP_RC_MAX_QP:
      priv->rate_control.max_qp = g_value_get_uint(value);
      break;
    case PROP_RC_MIN_QP:
      priv->rate_control.min_qp = g_value_get_uint(value);
      break;

    case PROP_CROP_ENABLE:
      priv->crop_config.enable = g_value_get_boolean(value) ? true : false;
      break;
    case PROP_CROP_X:
      priv->crop_config.x = g_value_get_uint(value);
      break;
    case PROP_CROP_Y:
      priv->crop_config.y = g_value_get_uint(value);
      break;
    case PROP_CROP_W:
      priv->crop_config.w = g_value_get_uint(value);
      break;
    case PROP_CROP_H:
      priv->crop_config.h = g_value_get_uint(value);
      break;

    case PROP_JPEG_QUALITY:
      self->jpeg_qulity = g_value_get_uint(value);
      break;
    case PROP_VIDEO_PROFILE:
      self->video_profile = static_cast<GstVideoProfile>(g_value_get_enum(value));
      break;
    case PROP_VIDEO_LEVEL:
      self->video_level = static_cast<GstVideoLevel>(g_value_get_enum(value));
      break;
    case PROP_P_FRAME_NUM:
      self->p_frame_num = g_value_get_uint(value);
      break;
    case PROP_B_FRAME_NUM:
      self->b_frame_num = g_value_get_uint(value);
      break;
    case PROP_CABAC_INIT_IDC:
      self->cabac_init_idc = g_value_get_uint(value);
      break;
    case PROP_GOP_TYPE:
      self->gop_type = g_value_get_uint(value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void
gst_cnencode_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
  GstCnencode* self = GST_CNENCODE(object);
  auto priv = gst_cnencode_get_private(self);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean(value, self->silent);
      break;
    case PROP_DEVICE_ID:
      g_value_set_int(value, self->device_id);
      break;

    case PROP_RC_VBR:
      g_value_set_boolean(value, priv->rate_control.vbr ? TRUE : FALSE);
      break;
    case PROP_RC_GOP:
      g_value_set_uint(value, priv->rate_control.gop);
      break;
    case PROP_RC_BIT_RATE:
      g_value_set_uint(value, priv->rate_control.bit_rate);
      break;
    case PROP_RC_MAX_BIT_RATE:
      g_value_set_uint(value, priv->rate_control.max_bit_rate);
      break;
    case PROP_RC_MAX_QP:
      g_value_set_uint(value, priv->rate_control.max_qp);
      break;
    case PROP_RC_MIN_QP:
      g_value_set_uint(value, priv->rate_control.min_qp);
      break;

    case PROP_CROP_ENABLE:
      g_value_set_boolean(value, priv->crop_config.enable ? TRUE : FALSE);
      break;
    case PROP_CROP_X:
      g_value_set_uint(value, priv->crop_config.x);
      break;
    case PROP_CROP_Y:
      g_value_set_uint(value, priv->crop_config.y);
      break;
    case PROP_CROP_W:
      g_value_set_uint(value, priv->crop_config.w);
      break;
    case PROP_CROP_H:
      g_value_set_uint(value, priv->crop_config.h);
      break;

    case PROP_JPEG_QUALITY:
      g_value_set_uint(value, self->jpeg_qulity);
      break;
    case PROP_VIDEO_PROFILE:
      g_value_set_enum(value, self->video_profile);
      break;
    case PROP_VIDEO_LEVEL:
      g_value_set_enum(value, self->video_level);
      break;
    case PROP_P_FRAME_NUM:
      g_value_set_uint(value, self->p_frame_num);
      break;
    case PROP_B_FRAME_NUM:
      g_value_set_uint(value, self->b_frame_num);
      break;
    case PROP_CABAC_INIT_IDC:
      g_value_set_uint(value, self->cabac_init_idc);
      break;
    case PROP_GOP_TYPE:
      g_value_set_uint(value, self->gop_type);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

/* 2. GstElement vmethod implementations */

static GstStateChangeReturn
gst_cnencode_change_state(GstElement* element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstCnencodeClass* klass = GST_CNENCODE_GET_CLASS(element);
  GstCnencode* self = GST_CNENCODE(element);

  ret = GST_ELEMENT_CLASS(PARENT_CLASS)->change_state(element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      klass->stop(self);
      klass->destroy(self);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }

  return ret;
}

static gboolean
gst_cnencode_set_caps(GstCnencode* self, GstCaps* caps)
{
  GstCaps* src_caps = NULL;
  auto priv = gst_cnencode_get_private(self);

  GST_INFO_OBJECT(self, "cnencode set caps");

  if (gst_video_info_from_caps(&self->video_info, caps) != TRUE) {
    GST_ERROR_OBJECT(self, "invalid caps");
    return FALSE;
  }

  if (self->video_info.width * self->video_info.height == 0) {
    GST_ERROR_OBJECT(self, "invalid caps width and height");
    return FALSE;
  }

  switch (self->video_info.finfo->format) {
    case GST_VIDEO_FORMAT_NV21:
      priv->pixel_format = edk::PixelFmt::NV21;
      break;
    case GST_VIDEO_FORMAT_NV12:
      priv->pixel_format = edk::PixelFmt::NV12;
      break;
    case GST_VIDEO_FORMAT_I420:
      priv->pixel_format = edk::PixelFmt::I420;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      priv->pixel_format = edk::PixelFmt::BGRA;
      break;
    case GST_VIDEO_FORMAT_RGBA:
      priv->pixel_format = edk::PixelFmt::RGBA;
      break;
    case GST_VIDEO_FORMAT_ARGB:
      priv->pixel_format = edk::PixelFmt::ARGB;
      break;
    case GST_VIDEO_FORMAT_ABGR:
      priv->pixel_format = edk::PixelFmt::ABGR;
      break;
    default:
      GST_ERROR_OBJECT(self, "unsupported input video pixel format(%d)", self->video_info.finfo->format);
      return FALSE;
  }

  if (self->video_info.fps_n == 0)
    self->video_info.fps_n = 30000;
  if (self->video_info.fps_d == 0)
    self->video_info.fps_d = 1001;

  priv->rate_control.frame_rate_num = self->video_info.fps_n;
  priv->rate_control.frame_rate_den = self->video_info.fps_d;

  // config src caps
  GstCnencodeClass* klass = GST_CNENCODE_GET_CLASS(self);
  src_caps = gst_caps_from_string("video/x-h264; video/x-h265; image/jpeg");

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
  // get information from sink caps
  auto structure = gst_caps_get_structure(peer_caps, 0);
  auto name = gst_structure_get_name(structure);
  if (g_strcmp0(name, "video/x-h264") == 0) {
    priv->codec_type = edk::CodecType::H264;
  } else if (g_strcmp0(name, "image/jpeg") == 0) {
    priv->codec_type = edk::CodecType::JPEG;
  } else if (g_strcmp0(name, "video/x-h265")) {
    priv->codec_type = edk::CodecType::H265;
  } else {
    GST_ERROR_OBJECT(self, "Unsupport Codec Type");
    gst_caps_unref(peer_caps);
    return FALSE;
  }

  /* Set src caps */
  if (priv->codec_type == edk::CodecType::H264) {
    gst_caps_set_simple(peer_caps, "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "nal",
                        NULL);
    gst_caps_set_simple(peer_caps, "width", G_TYPE_INT, self->video_info.width, "height", G_TYPE_INT,
                        self->video_info.height, "framerate", GST_TYPE_FRACTION, priv->rate_control.frame_rate_num,
                        priv->rate_control.frame_rate_den, "bitrate", G_TYPE_INT, priv->rate_control.bit_rate, NULL);
    gst_caps_set_simple(peer_caps, "profile", G_TYPE_STRING, g_profile_table.at(self->video_profile).str.c_str(),
                        "level", G_TYPE_STRING, g_level_table.at(self->video_level).level_str.c_str(), NULL);
  } else if (priv->codec_type == edk::CodecType::H265) {
    gst_caps_set_simple(peer_caps, "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "nal",
                        NULL);
    gst_caps_set_simple(peer_caps, "width", G_TYPE_INT, self->video_info.width, "height", G_TYPE_INT,
                        self->video_info.height, "framerate", GST_TYPE_FRACTION, priv->rate_control.frame_rate_num,
                        priv->rate_control.frame_rate_den, "bitrate", G_TYPE_INT, priv->rate_control.bit_rate, NULL);
    gst_caps_set_simple(peer_caps, "profile", G_TYPE_STRING, g_profile_table.at(self->video_profile).str.c_str(),
                        "level", G_TYPE_STRING, g_level_table.at(self->video_level).level_str.c_str(), "tier",
                        G_TYPE_STRING, g_level_table.at(self->video_level).tier_str.c_str(), NULL);
  } else if (priv->codec_type == edk::CodecType::JPEG) {
    gst_caps_set_simple(peer_caps, "width", G_TYPE_INT, self->video_info.width, "height", G_TYPE_INT,
                        self->video_info.height, "framerate", GST_TYPE_FRACTION, priv->rate_control.frame_rate_num,
                        priv->rate_control.frame_rate_den, NULL);
  } else {
    GST_ERROR_OBJECT(self, "Unsupported codec type %d", static_cast<int>(priv->codec_type));
    return FALSE;
  }

  GST_INFO_OBJECT(self, "cnencode setcaps %" GST_PTR_FORMAT, peer_caps);
  gst_pad_use_fixed_caps(self->srcpad);
  gboolean ret = gst_pad_set_caps(self->srcpad, peer_caps);
  gst_caps_unref(peer_caps);

  if (!ret) {
    GST_ERROR_OBJECT(self, "Set pad failed");
    return FALSE;
  }

  if (priv->encode) {
    if (!klass->stop(self)) {
      GST_ERROR_OBJECT(self, "gst_cnencode_stop() failed");
      return FALSE;
    }
    if (!klass->destroy(self)) {
      GST_ERROR_OBJECT(self, "gst_cnencode_destroy() failed");
      return FALSE;
    }
  }

  if (TRUE != klass->create(self)) {
    GST_ERROR_OBJECT(self, "gst_cnencode_create() failed");
    return FALSE;
  }
  if (TRUE != klass->start(self)) {
    GST_ERROR_OBJECT(self, "gst_cnencode_start() failed");
    return FALSE;
  }

  return TRUE;
}

/* this function handles sink events */
static gboolean
gst_cnencode_sink_event(GstPad* pad, GstObject* parent, GstEvent* event)
{
  GstCnencode* self;
  gboolean ret;

  self = GST_CNENCODE(parent);

  GST_LOG_OBJECT(self, "Received %s event: %" GST_PTR_FORMAT, GST_EVENT_TYPE_NAME(event), event);

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
      GstCaps* caps;
      gst_event_parse_caps(event, &caps);
      ret = gst_cnencode_set_caps(self, caps);
      if (!ret) {
        GST_ERROR_OBJECT(self, "set caps failed");
      }
      gst_event_unref(event);
      break;
    }
    case GST_EVENT_EOS: {
      edk::CnFrame frame;
      auto priv = gst_cnencode_get_private(self);
      memset(&frame, 0, sizeof(edk::CnFrame));
      if (priv->encode)
        ret = priv->encode->SendDataCPU(frame, true);
      else
        ret = TRUE;
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
gst_cnencode_chain(GstPad* pad, GstObject* parent, GstBuffer* buf)
{
  GstCnencode* self;
  self = GST_CNENCODE(parent);

  if (!encode_frame(self, buf))
    GST_ERROR_OBJECT(self, "encode failed\n");
  gst_buffer_unref(buf);
  return GST_FLOW_OK;
}

/* 3. GstCnencode method implementations */
static gboolean
gst_cnencode_create(GstCnencode* self)
{
  GST_INFO_OBJECT(self, "Create EasyEncode instance");
  auto priv = gst_cnencode_get_private(self);

  g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id), FALSE);

  edk::EasyEncode::Attr attr;
  memset(&attr, 0, sizeof(attr));

  attr.silent = self->silent;
  attr.dev_id = self->device_id;

  attr.frame_geometry.w = self->video_info.width;
  attr.frame_geometry.h = self->video_info.height;
  attr.pixel_format = priv->pixel_format;
  attr.codec_type = priv->codec_type;

  attr.rate_control = priv->rate_control;
  attr.crop_config = priv->crop_config;
  try {
    attr.profile = g_profile_table.at(self->video_profile).prof;
    attr.level = g_level_table.at(self->video_level).level;
  } catch (std::out_of_range& e) {
    GST_ERROR_OBJECT(self, "%s, wrong profile or level", e.what());
    return FALSE;
  }
  attr.jpeg_qfactor = self->jpeg_qulity;
  attr.input_buffer_num = 4;
  attr.output_buffer_num = 4;
  attr.b_frame_num = self->b_frame_num;
  attr.p_frame_num = self->p_frame_num;
  attr.gop_type = static_cast<edk::GopType>(self->gop_type);
  attr.cabac_init_idc = self->cabac_init_idc;

  attr.packet_callback = std::bind(&handle_encode_output, self, std::placeholders::_1);
  attr.eos_callback = std::bind(&handle_encode_eos, self);

  try {
    priv->encode = edk::EasyEncode::Create(attr);
  } catch (edk::Exception& e) {
    GST_ERROR_OBJECT(self, "%s", e.what());
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cnencode_destroy(GstCnencode* self)
{
  auto priv = gst_cnencode_get_private(self);

  if (priv->encode) {
    delete priv->encode;
    priv->encode = nullptr;
  }

  return TRUE;
}

static gboolean
gst_cnencode_start(GstCnencode* self)
{
  auto priv = gst_cnencode_get_private(self);
  if (priv->encode == NULL) {
    GST_ERROR_OBJECT(self, "self->encode == NULL");
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_cnencode_stop(GstCnencode* self)
{
  auto priv = gst_cnencode_get_private(self);
  if (priv->encode == NULL) {
    GST_ERROR_OBJECT(self, "self->encode == NULL");
    return FALSE;
  }
  return TRUE;
}

gboolean
encode_frame(GstCnencode* self, GstBuffer* buf)
{
  bool ret = true;
  GstVideoFrame gst_frame;
  auto priv = gst_cnencode_get_private(self);

  thread_local bool cnrt_env = false;
  if (!cnrt_env) {
    g_return_val_if_fail(set_cnrt_env(GST_ELEMENT(self), self->device_id), FALSE);
    cnrt_env = true;
  }

  if (!gst_video_frame_map(&gst_frame, &self->video_info, buf, GST_MAP_READ)) {
    GST_WARNING_OBJECT(self, "buffer map failed %" GST_PTR_FORMAT, buf);
    return FALSE;
  }

  edk::CnFrame frame;
  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES(&self->video_info); ++i) {
    frame.ptrs[i] = reinterpret_cast<void*>(gst_frame.data[i]);
  }
  frame.n_planes = GST_VIDEO_INFO_N_PLANES(&self->video_info);
  frame.frame_size = self->video_info.size;
  frame.pts = GST_BUFFER_PTS(buf);
  frame.width = self->video_info.width;
  frame.height = self->video_info.height;
  frame.pformat = priv->pixel_format;
  frame.device_id = self->device_id;

  if (priv->encode)
    ret = priv->encode->SendDataCPU(frame);
  else
    ret = false;

  gst_video_frame_unmap(&gst_frame);

  return ret ? TRUE : FALSE;
}

static void
handle_encode_eos(GstCnencode* self)
{
  GST_INFO_OBJECT(self, "Got HW EOS");

  GstEvent* event = gst_event_new_eos();
  gst_pad_push_event(self->srcpad, event);
}

static void
handle_encode_output(GstCnencode* self, const edk::CnPacket& packet)
{
  GST_TRACE_OBJECT(self, "handle_encode_output(%p,%lu,%lu,%lu,%d)", packet.data, packet.length, packet.buf_id,
                   packet.pts, static_cast<int>(packet.codec_type));

  auto priv = gst_cnencode_get_private(self);

  GstMapInfo info;
  GstBuffer* buffer = gst_buffer_new_allocate(NULL, packet.length, NULL);

  gst_buffer_map(buffer, &info, GST_MAP_WRITE);
  memcpy(info.data, packet.data, packet.length);
  priv->encode->ReleaseBuffer(packet.buf_id);
  gst_buffer_unmap(buffer, &info);

  gst_pad_push(self->srcpad, buffer);
}
