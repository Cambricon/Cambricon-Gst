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

#ifndef GST_MLU_FRAME_H_
#define GST_MLU_FRAME_H_

#include <gst/gst.h>

#include "synced_memory.h"

G_BEGIN_DECLS

GType
gst_mlu_frame_get_type(void);
#define GST_TYPE_MLU_FRAME (gst_mlu_frame_get_type())
#define GST_MLU_FRAME_CAST(obj) ((GstMluFrame_t)(obj))

#define MAXIMUM_PLANE 6

struct FrameDeallocator;

struct GstMluFrame
{
  GstMiniObject mini_object;

  gint device_id;
  guint channel_id;

  guint stride[MAXIMUM_PLANE];
  guint n_planes;
  guint width;
  guint height;

  // data
  GstSyncedMemory_t data[MAXIMUM_PLANE];

  struct FrameDeallocator* deallocator;
};

typedef struct GstMluFrame* GstMluFrame_t;

GstMluFrame_t
gst_mlu_frame_new();

inline GstMluFrame_t
gst_mlu_frame_ref(GstMluFrame_t frame)
{
  return GST_MLU_FRAME_CAST(gst_mini_object_ref(GST_MINI_OBJECT_CAST(frame)));
}

inline void
gst_mlu_frame_unref(GstMluFrame_t frame)
{
  gst_mini_object_unref(GST_MINI_OBJECT_CAST(frame));
}

G_END_DECLS

#endif // GST_MLU_FRAME_H_
