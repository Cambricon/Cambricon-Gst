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

#ifndef GST_CNVIDEO_DEC_H_
#define GST_CNVIDEO_DEC_H_

#include <gst/gst.h>

#define GST_TYPE_CNVIDEODEC (gst_cnvideodec_get_type())
#define GST_CNVIDEODEC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CNVIDEODEC, GstCnvideodec))
#define GST_CNVIDEODEC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CNVIDEODEC, GstCnvideodecClass))
#define GST_IS_CNVIDEODEC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CNVIDEODEC))
#define GST_IS_CNVIDEODEC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CNVIDEODEC))
#define GST_CNVIDEODEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_CNVIDEODEC, GstCnvideodecClass))

G_BEGIN_DECLS

typedef struct _GstCnvideodec GstCnvideodec;
typedef struct _GstCnvideodecClass GstCnvideodecClass;

struct _GstCnvideodec
{
  GstElement element;
  GstPad *sinkpad, *srcpad;

  gboolean silent;

  gint device_id;
  guint stream_id;
  guint input_buffer_num;
  guint output_buffer_num;
};

struct _GstCnvideodecClass
{
  GstElementClass parent_class;
  gboolean (*init_decoder)(GstCnvideodec* element);
  gboolean (*destroy_decoder)(GstCnvideodec* element);
};

GType
gst_cnvideodec_get_type(void);

G_END_DECLS

#endif // GST_CNVIDEO_DEC_H_
