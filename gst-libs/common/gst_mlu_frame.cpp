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

#include "gst_mlu_frame.h"

#include <gst/gst.h>
#include <cstring>

#include "frame_deallocator.h"

GST_DEFINE_MINI_OBJECT_TYPE(GstMluFrame, gst_mlu_frame);

static void
gst_mlu_frame_free(GstMluFrame_t frame)
{
  GST_TRACE("Free mlu frame meta\n");
  if (frame->deallocator) {
    frame->deallocator->deallocate();
    delete frame->deallocator;
    frame->deallocator = nullptr;
  }
  for (guint i = 0; i < frame->n_planes; ++i) {
    if (!cn_syncedmem_free(frame->data[i])) {
      GST_ERROR("Free synced memory failed %s", cn_syncedmem_get_last_errmsg(frame->data[i]));
    }
    frame->data[i] = nullptr;
  }

  g_slice_free(GstMluFrame, frame);
}

GstMluFrame_t
gst_mlu_frame_new()
{
  GstMluFrame_t frame;

  frame = g_slice_new0(GstMluFrame);
  gst_mini_object_init(GST_MINI_OBJECT_CAST(frame), 0, GST_TYPE_MLU_FRAME, NULL, NULL,
                       (GstMiniObjectFreeFunction)gst_mlu_frame_free);

  frame->deallocator = nullptr;
  frame->device_id = 0;
  frame->n_planes = 0;
  memset(frame->stride, 0x0, MAXIMUM_PLANE * sizeof(guint));
  memset(frame->data, 0x0, MAXIMUM_PLANE * sizeof(GstSyncedMemory_t));

  return frame;
}
