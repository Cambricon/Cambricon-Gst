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

#include "mlu_memory_meta.h"

GType
gst_mlu_memory_meta_api_get_type(void)
{
  static volatile GType type;
  static const gchar* tags[] = { NULL };

  if (g_once_init_enter(&type)) {
    GType _type = gst_meta_api_type_register("GstMluMemoryMetaAPI", tags);
    g_once_init_leave(&type, _type);
  }
  return type;
}

static gboolean
gst_mlu_memory_meta_init(GstMeta* meta, gpointer params, GstBuffer* buffer)
{
  MluMemoryMeta_t memory_meta = (MluMemoryMeta_t)meta;
  memory_meta->meta_src = nullptr;
  return TRUE;
}

static void
gst_mlu_memory_meta_free(GstMeta* meta, GstBuffer* buffer)
{
  MluMemoryMeta_t memory_meta = (MluMemoryMeta_t)meta;
  GST_LOG("Free mlu frame meta\n");
  memory_meta->meta_src = nullptr;
  if (memory_meta->frame)
    gst_mlu_frame_unref(memory_meta->frame);
  memory_meta->frame = nullptr;
}

static gboolean
gst_mlu_memory_meta_transform(GstBuffer* transbuf, GstMeta* meta, GstBuffer* buffer, GQuark type, gpointer data)
{
  MluMemoryMeta_t memory_meta = (MluMemoryMeta_t)meta;

  if (GST_META_TRANSFORM_IS_COPY(type)) {
    GstMetaTransformCopy* copy = (GstMetaTransformCopy*)(data);
    if (!copy->region) {
      /* only copy if the complete data is copied as well */
      gst_buffer_add_mlu_memory_meta(transbuf, gst_mlu_frame_ref(memory_meta->frame), memory_meta->meta_src);
    } else {
      return FALSE;
    }
  } else {
    /* transform type not supported */
    return FALSE;
  }
  return TRUE;
}

const GstMetaInfo*
gst_mlu_memory_meta_get_info(void)
{
  static const GstMetaInfo* memory_meta_info = nullptr;

  if (g_once_init_enter(&memory_meta_info)) {
    const GstMetaInfo* meta =
      gst_meta_register(MLU_MEMORY_META_API_TYPE, "MluMemoryMeta", sizeof(struct MluMemoryMeta),
                        gst_mlu_memory_meta_init, gst_mlu_memory_meta_free, gst_mlu_memory_meta_transform);

    g_once_init_leave(&memory_meta_info, meta);
  }
  return memory_meta_info;
}

MluMemoryMeta_t
gst_buffer_add_mlu_memory_meta(GstBuffer* buffer, GstMluFrame_t frame, const gchar* meta_src)
{
  MluMemoryMeta_t meta;

  g_return_val_if_fail(GST_IS_BUFFER(buffer), NULL);

  meta = (MluMemoryMeta_t)(gst_buffer_add_meta(buffer, MLU_MEMORY_META_INFO, NULL));

  meta->meta_src = meta_src;
  meta->frame = frame;

  return meta;
}
