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

#ifndef MLU_MEMORY_META_H_
#define MLU_MEMORY_META_H_

#include <gst/gst.h>

#include "common/synced_memory.h"
#include "gst_mlu_frame.h"

#define GST_CAPS_FEATURE_MEMORY_MLU "memory:mlu"

G_BEGIN_DECLS

struct FrameDeallocator;

struct MluMemoryMeta
{
  GstMeta meta;
  const gchar* meta_src;

  GstMluFrame_t frame;
};

typedef struct MluMemoryMeta* MluMemoryMeta_t;

GType
gst_mlu_memory_meta_api_get_type(void);

const GstMetaInfo*
gst_mlu_memory_meta_get_info(void);

#define MLU_MEMORY_META_API_TYPE (gst_mlu_memory_meta_api_get_type())

#define gst_buffer_get_mlu_memory_meta(b) ((MluMemoryMeta*)gst_buffer_get_meta((b), MLU_MEMORY_META_API_TYPE))

#define MLU_MEMORY_META_INFO (gst_mlu_memory_meta_get_info())

const GstMetaInfo*
mlu_memory_meta_get_info(void);

MluMemoryMeta_t
gst_buffer_add_mlu_memory_meta(GstBuffer* buffer, GstMluFrame_t frame, const gchar* meta_src);

G_END_DECLS

#endif // MLU_MEMORY_META_H_
