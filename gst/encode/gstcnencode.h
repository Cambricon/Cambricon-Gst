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

#ifndef GST_CNENCODE_H_
#define GST_CNENCODE_H_

#include <gst/gst.h>
#include <gst/video/video.h>

#include "common/data_type.h"

#define GST_TYPE_CNENCODE (gst_cnencode_get_type())
#define GST_CNENCODE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CNENCODE, GstCnencode))
#define GST_CNENCODE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CNENCODE, GstCnencodeClass))
#define GST_IS_CNENCODE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CNENCODE))
#define GST_IS_CNENCODE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CNENCODE))
#define GST_CNENCODE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_CNENCODE, GstCnencodeClass))

typedef struct _GstCnencode GstCnencode;
typedef struct _GstCnencodeClass GstCnencodeClass;

struct _GstCnencode
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean silent;
  gint device_id;
  GstVideoProfile video_profile;
  GstVideoLevel video_level;
  guint jpeg_qulity;
  guint p_frame_num;
  guint b_frame_num;
  guint cabac_init_idc;
  guint gop_type;

  GstVideoInfo video_info;
};

struct _GstCnencodeClass
{
  GstElementClass parent_class;
  gboolean (*create)(GstCnencode* element);
  gboolean (*destroy)(GstCnencode* element);
  gboolean (*start)(GstCnencode* element);
  gboolean (*stop)(GstCnencode* element);
};

GType
gst_cnencode_get_type(void);

#endif // GST_CNENCODE_H_
