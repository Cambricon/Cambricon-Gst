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

#include <gst/app/gstappsink.h>
#include <gst/check/gstbufferstraw.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <unistd.h>
#include <atomic>
#include <cstdint>

#include "common/data_type.h"
#include "common/frame_deallocator.h"
#include "common/mlu_memory_meta.h"
#include "easycodec/vformat.h"

#define JPEG_CAPS_RESTRICTIVE                                                                                          \
  "image/jpeg, "                                                                                                       \
  "width = (int) [10, 2000], "                                                                                         \
  "framerate = (fraction) 25/1 "

static void
src_handle_pad_added(GstElement* src, GstPad* new_pad, GstElement* sink)
{
  GstPad* sink_pad = gst_element_get_static_pad(sink, "sink");
  fail_if(!sink_pad);

  if (!gst_pad_is_linked(sink_pad)) {
    gst_pad_link(new_pad, sink_pad);
  }
  gst_object_unref(sink_pad);
}

GST_START_TEST(test_properties)
{
  GstElement* cndecode;
  cndecode = gst_check_setup_element("cndecode");
  g_print("test properties\n");
  fail_unless(cndecode != NULL);
  g_object_set(G_OBJECT(cndecode), "silent", TRUE, "device-id", 1, "stream-id", 32, NULL);
  guint chn_id, mlu_id;
  gboolean silent;

  g_object_get(G_OBJECT(cndecode), "silent", &silent, "device-id", &mlu_id, "stream-id", &chn_id, NULL);

  fail_unless_equals_int(chn_id, 32);
  fail_unless_equals_int(mlu_id, 1);
  fail_unless_equals_int(silent, 1);

  gst_check_teardown_element(cndecode);
}
GST_END_TEST;

static std::atomic<bool> eos(false);

static gboolean
test_sink(GstPad* pad, GstObject* parent, GstEvent* event)
{
  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_EOS:
      eos.store(true);
      g_print("eos in unit test\n");
      gst_event_unref(event);
      break;
    default:
      return gst_pad_event_default(pad, parent, event);
      break;
  }
  return FALSE;
}

/* Verify h264 is working when explictly requested by a pipeline. */
GST_START_TEST(test_h264dec_NV21_explicit)
{
  GstElement *pipeline, *source, *parser, *dec, *appsink;
  gchar current_path[128] = { 0 };
  fail_unless(getcwd(current_path, sizeof(current_path) - 1));
  gchar* video_file = g_strconcat("file://", current_path, "/../samples/data/videos/1080p.h264", NULL);
  g_print("test h264 nv21\n");

  /* construct a pipeline that explicitly uses cndecode h264 */
  pipeline = gst_pipeline_new(NULL);
  source = gst_check_setup_element("uridecodebin");
  parser = gst_check_setup_element("h264parse");
  GstCaps* caps = gst_caps_new_empty_simple("video/x-h264");
  g_object_set(G_OBJECT(source), "caps", caps, "uri", video_file, NULL);
  gst_caps_unref(caps);
  g_free(video_file);
  dec = gst_check_setup_element("cndecode");
  appsink = gst_check_setup_element("appsink");

  g_signal_connect(source, "pad-added", G_CALLBACK(src_handle_pad_added), parser);

  gst_bin_add_many(GST_BIN(pipeline), source, parser, dec, appsink, NULL);
  fail_unless(gst_element_link_many(parser, dec, appsink, NULL));
  GstPad* sinkpad = gst_element_get_static_pad(appsink, "sink");
  gst_pad_set_event_function(sinkpad, test_sink);
  gst_buffer_straw_start_pipeline(pipeline, sinkpad);

  for (int i = 0; !eos.load(); i++) {
    GstBuffer* buf;
    buf = gst_buffer_straw_get_buffer(pipeline, sinkpad);

    MluMemoryMeta* mlu_memory_meta = gst_buffer_get_mlu_memory_meta(buf);
    fail_unless_equals_string("cndecode", mlu_memory_meta->meta_src);
    gst_mlu_frame_unref(mlu_memory_meta->frame);
    mlu_memory_meta->frame = nullptr;
    gst_buffer_unref(buf);
    g_usleep(1e5);
  }
  fail_unless(eos == TRUE);

  gst_buffer_straw_stop_pipeline(pipeline, sinkpad);
  gst_object_unref(pipeline);
  gst_object_unref(sinkpad);
}
GST_END_TEST;

GST_START_TEST(test_h264dec_NV12_explicit)
{
  GstElement *pipeline, *source, *parser, *dec, *appsink;
  gchar current_path[128];
  memset(current_path, 0x00, sizeof(current_path));
  fail_unless(getcwd(current_path, sizeof(current_path) - 1));
  gchar* video_file = g_strconcat("file://", current_path, "/../samples/data/videos/1080P.h264", NULL);

  g_print("test h264 nv12\n");
  /* construct a pipeline that explicitly uses cndecode h264 */
  pipeline = gst_pipeline_new(NULL);

  source = gst_check_setup_element("uridecodebin");
  parser = gst_check_setup_element("h264parse");
  GstCaps* caps = gst_caps_new_empty_simple("video/x-h264");
  g_object_set(G_OBJECT(source), "caps", caps, "uri", video_file, NULL);
  gst_caps_unref(caps);
  g_free(video_file);
  dec = gst_check_setup_element("cndecode");
  guint test_frame_nums = 3;

  appsink = gst_check_setup_element("appsink");

  g_signal_connect(source, "pad-added", G_CALLBACK(src_handle_pad_added), parser);

  gst_bin_add_many(GST_BIN(pipeline), source, parser, dec, appsink, NULL);
  fail_unless(gst_element_link_many(parser, dec, appsink, NULL));
  GstPad* sinkpad = gst_element_get_static_pad(appsink, "sink");
  gst_buffer_straw_start_pipeline(pipeline, sinkpad);

  for (guint i = 0; i < test_frame_nums; i++) {
    GstBuffer* buf;
    buf = gst_buffer_straw_get_buffer(pipeline, sinkpad);

    MluMemoryMeta* mlu_memory_meta = gst_buffer_get_mlu_memory_meta(buf);
    fail_unless_equals_string("cndecode", mlu_memory_meta->meta_src);
    gst_mlu_frame_unref(mlu_memory_meta->frame);
    mlu_memory_meta->frame = nullptr;

    gst_buffer_unref(buf);
  }

  gst_buffer_straw_stop_pipeline(pipeline, sinkpad);
  gst_object_unref(pipeline);
  gst_object_unref(sinkpad);
}
GST_END_TEST;

GST_START_TEST(test_jpecdec_explicit)
{
  GstElement *pipeline, *source, *capsfilter_out, *jparse, *dec, *sink;
  g_print("test jpeg\n");

  /* construct a pipeline that explicitly uses cndecode */
  pipeline = gst_pipeline_new(NULL);
  source = gst_check_setup_element("filesrc");

  /* point that pipeline to our test image */
  {
    char* filename = g_build_filename("../tests/data/", "jpeg_1080p.jpg", NULL);
    g_object_set(G_OBJECT(source), "location", filename, NULL);
    g_free(filename);
  }

  capsfilter_out = gst_check_setup_element("capsfilter");
  jparse = gst_check_setup_element("jpegparse");
  fail_unless(capsfilter_out != NULL);
  GstCaps* jpeg_caps = gst_caps_from_string("video/x-raw(memory:mlu),"
                                            "width=1920, height=1080");
  g_object_set(G_OBJECT(capsfilter_out), "caps", jpeg_caps, NULL);
  gst_caps_unref(jpeg_caps);

  dec = gst_check_setup_element("cndecode");
  sink = gst_check_setup_element("appsink");

  gst_bin_add_many(GST_BIN(pipeline), source, jparse, dec, capsfilter_out, sink, NULL);
  fail_unless(gst_element_link_many(source, jparse, dec, sink, NULL));

  GstPad* sinkpad = gst_element_get_static_pad(sink, "sink");
  gst_buffer_straw_start_pipeline(pipeline, sinkpad);

  GstBuffer* buf;
  buf = gst_buffer_straw_get_buffer(pipeline, sinkpad);

  MluMemoryMeta* mlu_memory_meta = gst_buffer_get_mlu_memory_meta(buf);
  fail_unless_equals_string("cndecode", mlu_memory_meta->meta_src);
  gst_mlu_frame_unref(mlu_memory_meta->frame);
  mlu_memory_meta->frame = nullptr;

  gst_buffer_unref(buf);

  gst_buffer_straw_stop_pipeline(pipeline, sinkpad);

  gst_object_unref(sinkpad);
  gst_object_unref(pipeline);
}
GST_END_TEST;

Suite*
cndecode_suite(void)
{
  Suite* s = suite_create("cndecode");
  TCase* tc_chain = tcase_create("general");

  suite_add_tcase(s, tc_chain);
  tcase_add_test(tc_chain, test_properties);
  tcase_add_test(tc_chain, test_h264dec_NV21_explicit);
  tcase_add_test(tc_chain, test_h264dec_NV12_explicit);
  tcase_add_test(tc_chain, test_jpecdec_explicit);
  return s;
}
