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

#ifndef GST_CNVIDEO_ENC_H_
#define GST_CNVIDEO_ENC_H_

#include <gst/gst.h>
#include <gst/video/video.h>

#include "cn_codec_common.h"
#include "cn_video_enc.h"
#include "encode_type.h"

#define GST_TYPE_CNVIDEOENC (gst_cnvideoenc_get_type())
#define GST_CNVIDEOENC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CNVIDEOENC, GstCnvideoenc))
#define GST_CNVIDEOENC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CNVIDEOENC, GstCnvideoencClass))
#define GST_IS_CNVIDEOENC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CNVIDEOENC))
#define GST_IS_CNVIDEOENC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CNVIDEOENC))
#define GST_CNVIDEOENC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_CNVIDEOENC, GstCnvideoencClass))

typedef struct _GstCnvideoenc GstCnvideoenc;
typedef struct _GstCnvideoencClass GstCnvideoencClass;

struct _GstCnvideoenc
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean silent;
  gint device_id;
  cncodecType codec_type;
  GstVideoProfile video_profile;
  GstVideoLevel video_level;
  guint i_frame_interval;
  guint b_frame_num;
  cnvideoEncGopType gop_type;
  guint input_buffer_num;
  guint output_buffer_num;

  GstVideoInfo video_info;
};

struct _GstCnvideoencClass
{
  GstElementClass parent_class;
  gboolean (*init_encoder)(GstCnvideoenc* element);
  gboolean (*destroy_encoder)(GstCnvideoenc* element);
};

GType
gst_cnvideoenc_get_type(void);

#endif // GST_CNVIDEO_ENC_H_
