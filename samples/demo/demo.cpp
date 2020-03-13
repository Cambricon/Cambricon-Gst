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
#include <iostream>
#include <string>

#define MAX_NAME_LENGTH 20

#define CHECK(ret, output)           \
  if (!ret) {                        \
    GST_ERROR("ERROR: %s", output);  \
    return -1;                       \
  }

gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = reinterpret_cast < GMainLoop * >(data);
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      GST_INFO ("End of stream\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:
      gchar * debug;
      GError *error;
      gst_message_parse_error (msg, &error, &debug);
      GST_ERROR ("ERROR from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), error->message);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }
  return true;
}


static void
src_handle_pad_added (GstElement * src, GstPad * new_pad, GstElement * sink)
{
  GstPad *sink_pad = gst_element_get_static_pad (sink, "sink");
  if (!sink_pad) {
    GST_INFO ("have not get the pad\n");
  }
  GstPadLinkReturn ret;

  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_structure = NULL;
  const gchar *new_pad_type = NULL;
  new_pad_caps = gst_pad_get_current_caps (new_pad);
  new_pad_structure = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_structure);
  GST_INFO ("%s\n", new_pad_type);

  if (gst_pad_is_linked (sink_pad)) {
    GST_INFO ("already linked, Ignoring.\n");
    gst_object_unref (sink_pad);
    return;
  }

  ret = gst_pad_link (new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED (ret)) {
    GST_INFO ("link failed\n");
  } else {
    GST_INFO ("link succeed\n");
  }
  gst_object_unref (sink_pad);
}

int main (int argc, char **argv) {
  gchar *video_path = NULL, *output_path = NULL;
  GOptionContext *ctx;
  GError *err = NULL;
  GOptionEntry entries[] = {
    { "video_path", 'v', 0, G_OPTION_ARG_STRING, &video_path,
      "The path of the video file.", "FILE" },
    { "output_path", 'o', 0, G_OPTION_ARG_STRING, &output_path,
      "The path to save output transcode video file", "FILE" },
    { NULL }
  };

  ctx = g_option_context_new ("demo");
  g_option_context_add_main_entries (ctx, entries, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Failed to initialize: %s\n", err->message);
    g_error_free (err);
    return 1;
  }

  gst_init (&argc, &argv);

  std::cout << "video path: " << video_path << std::endl;
  std::cout << "output path: " << output_path << std::endl;
  std::string vpath = "file://";
  vpath += video_path;
  std::string opath = output_path;

  g_free(video_path);
  g_free(output_path);
  g_option_context_free(ctx);

  GstElement *pipeline = NULL, *src = NULL, *parse = NULL, *dec = NULL, *convert = NULL,
             *encode = NULL, *sink = NULL;
  GstCaps *caps = NULL;
  GST_INFO ("create pipeline\n");
  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("uridecodebin", "src");
  parse = gst_element_factory_make ("h264parse", "parse");
  dec = gst_element_factory_make ("cndecode", "dec");
  convert = gst_element_factory_make ("cnconvert", "convert");
  encode = gst_element_factory_make ("cnencode", "enc");
  sink = gst_element_factory_make("filesink", "sink");

  CHECK ((pipeline && convert && sink &&
          src && parse && dec && encode),
      "create element failed\n");
  gst_bin_add_many (reinterpret_cast < GstBin * >(pipeline),
      src, parse, dec, convert, encode, sink, NULL);

  caps = gst_caps_new_empty_simple ("video/x-h264");

  g_object_set (G_OBJECT (src), "caps", caps, "uri", vpath.c_str (), NULL);
  g_object_set (G_OBJECT (dec), "silent", FALSE, "stream-id", 0, "device-id", 0, NULL);
  g_object_set (G_OBJECT (sink), "location", opath.c_str(), NULL);

  // common
  g_signal_connect (src, "pad-added", G_CALLBACK (src_handle_pad_added), parse);

  gst_caps_unref (caps);

  auto ret = gst_element_link_many (parse, dec, convert, encode, sink, NULL);
  if (!ret) {
    GST_ERROR ("link failed\n");
    return -1;
  }

  auto loop = g_main_loop_new (NULL, false);
  auto bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  guint bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  GST_INFO ("Pipeline playing...\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  GST_INFO ("Running...\n");
  g_main_loop_run (loop);
  GST_INFO ("Stop playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  // release resources
  /* Release the request pads from the Tiler, and unref them */
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);
  return 0;
}
