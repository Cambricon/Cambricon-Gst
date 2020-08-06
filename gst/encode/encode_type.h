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

#ifndef GST_CAMBRICON_NEUWARE_DATATYPE_
#define GST_CAMBRICON_NEUWARE_DATATYPE_

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  GST_VP_0 = 0,
  GST_VP_H264_BASELINE,
  GST_VP_H264_MAIN,
  GST_VP_H264_HIGH,
  GST_VP_H264_HIGH_10,

  GST_VP_H265_MAIN,
  GST_VP_H265_MAIN_STILL,
  GST_VP_H265_MAIN_INTRA,
  GST_VP_H265_MAIN_10,
  GST_VP_MAX,
} GstVideoProfile;

typedef enum
{
  GST_VL_0 = 0,
  GST_VL_H264_1,
  GST_VL_H264_1B,
  GST_VL_H264_11,
  GST_VL_H264_12,
  GST_VL_H264_13,
  GST_VL_H264_2,
  GST_VL_H264_21,
  GST_VL_H264_22,
  GST_VL_H264_3,
  GST_VL_H264_31,
  GST_VL_H264_32,
  GST_VL_H264_4,
  GST_VL_H264_41,
  GST_VL_H264_42,
  GST_VL_H264_5,
  GST_VL_H264_51,

  GST_VL_H265_MAIN_1,
  GST_VL_H265_HIGH_1,
  GST_VL_H265_MAIN_2,
  GST_VL_H265_HIGH_2,
  GST_VL_H265_MAIN_21,
  GST_VL_H265_HIGH_21,
  GST_VL_H265_MAIN_3,
  GST_VL_H265_HIGH_3,
  GST_VL_H265_MAIN_31,
  GST_VL_H265_HIGH_31,
  GST_VL_H265_MAIN_4,
  GST_VL_H265_HIGH_4,
  GST_VL_H265_MAIN_41,
  GST_VL_H265_HIGH_41,
  GST_VL_H265_MAIN_5,
  GST_VL_H265_HIGH_5,
  GST_VL_H265_MAIN_51,
  GST_VL_H265_HIGH_51,
  GST_VL_H265_MAIN_52,
  GST_VL_H265_HIGH_52,
  GST_VL_H265_MAIN_6,
  GST_VL_H265_HIGH_6,
  GST_VL_H265_MAIN_61,
  GST_VL_H265_HIGH_61,
  GST_VL_H265_MAIN_62,
  GST_VL_H265_HIGH_62,
  GST_VL_MAX
} GstVideoLevel;

G_END_DECLS

#endif // GST_CAMBRICON_NEUWARE_DATATYPE_
