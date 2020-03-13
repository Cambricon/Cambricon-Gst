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

#ifndef GST_CNDECODE_H_
#define GST_CNDECODE_H_

#include <gst/gst.h>

#define GST_TYPE_CNDECODE (gst_cndecode_get_type())
#define GST_CNDECODE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CNDECODE, GstCndecode))
#define GST_CNDECODE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CNDECODE, GstCndecodeClass))
#define GST_IS_CNDECODE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CNDECODE))
#define GST_IS_CNDECODE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CNDECODE))
#define GST_CNDECODE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_CNDECODE, GstCndecodeClass))

G_BEGIN_DECLS

typedef struct _GstCndecode GstCndecode;
typedef struct _GstCndecodeClass GstCndecodeClass;

struct _GstCndecode
{
  GstElement element;
  GstPad *sinkpad, *srcpad;

  gboolean silent;

  gint device_id;
  guint stream_id;
};

struct _GstCndecodeClass
{
  GstElementClass parent_class;
  gboolean (*start)(GstCndecode* element);
  gboolean (*init_decoder)(GstCndecode* element);
  gboolean (*stop)(GstCndecode* element);
  gboolean (*destroy_decoder)(GstCndecode* element);
};

GType
gst_cndecode_get_type(void);

G_END_DECLS

#endif // GST_CNDECODE_H_
