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

#include <gst/gst.h>

#ifdef WITH_DECODE
#include "decode/gstcndecode.h"
#endif
#ifdef WITH_CONVERT
#include "convert/gstcnconvert.h"
#endif
#ifdef WITH_ENCODE
#include "encode/gstcnencode.h"
#endif

#ifndef PACKAGE
#define PACKAGE "cambricon"
#endif

#ifndef VERSION
#define VERSION "1.0"
#endif

GST_DEBUG_CATEGORY(gst_cambricon_debug);

G_BEGIN_DECLS

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
plugin_init(GstPlugin* plugin)
{
  gboolean ret = TRUE;
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT(gst_cambricon_debug,
                          "cambricon",
                          GST_DEBUG_BG_WHITE | GST_DEBUG_FG_BLUE,
                          "Cambricon Neuware Stream Kit debug category");
#ifdef WITH_DECODE
  ret &= gst_element_register(plugin, "cndecode", GST_RANK_NONE, GST_TYPE_CNDECODE);
#endif
#ifdef WITH_CONVERT
  ret &= gst_element_register(plugin, "cnconvert", GST_RANK_NONE, GST_TYPE_CNCONVERT);
#endif
#ifdef WITH_ENCODE
  ret &= gst_element_register(plugin, "cnencode", GST_RANK_NONE, GST_TYPE_CNENCODE);
#endif
  return ret;
}

G_END_DECLS

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  cnstream,
                  "Cambricon Neuware Stream Kit",
                  plugin_init,
                  VERSION,
                  "LGPL",
                  PACKAGE,
                  "http://www.cambricon.com/")
