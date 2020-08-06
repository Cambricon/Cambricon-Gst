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

#ifdef WITH_DECODE

#include <gst/app/gstappsink.h>
#include <gst/check/gstbufferstraw.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <unistd.h>
#include <atomic>
#include <cstdint>

#include "common/frame_deallocator.h"
#include "common/mlu_memory_meta.h"

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
  GstElement* cnvideodec;
  cnvideodec = gst_check_setup_element("cnvideo_dec");
  g_print("test properties\n");
  fail_unless(cnvideodec != NULL);
  g_object_set(G_OBJECT(cnvideodec), "silent", TRUE, "device-id", 1, "stream-id", 32, "input-buffer-num", 5,
               "output-buffer-num", 5, NULL);
  guint chn_id, mlu_id, i_num, o_num;
  gboolean silent;

  g_object_get(G_OBJECT(cnvideodec), "silent", &silent, "device-id", &mlu_id, "stream-id", &chn_id, "input-buffer-num",
               &i_num, "output-buffer-num", &o_num, NULL);

  fail_unless_equals_int(chn_id, 32);
  fail_unless_equals_int(mlu_id, 1);
  fail_unless_equals_int(silent, 1);
  fail_unless_equals_int(i_num, 5);
  fail_unless_equals_int(o_num, 5);

  gst_check_teardown_element(cnvideodec);
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
  GstElement *pipeline, *source, *parser, *dec, *appsink, *caps;
  gchar current_path[128] = { 0 };
  fail_unless(getcwd(current_path, sizeof(current_path) - 1));
  gchar* video_file = g_strconcat("file://", current_path, "/../samples/data/videos/1080p.h264", NULL);
  g_print("test h264 nv21\n");

  /* construct a pipeline that explicitly uses cnvideodec h264 */
  pipeline = gst_pipeline_new(NULL);
  source = gst_check_setup_element("uridecodebin");
  parser = gst_check_setup_element("h264parse");
  dec = gst_check_setup_element("cnvideo_dec");
  appsink = gst_check_setup_element("appsink");
  caps = gst_check_setup_element("capsfilter");

  GstCaps* h264_caps = gst_caps_new_empty_simple("video/x-h264");
  g_object_set(G_OBJECT(source), "caps", h264_caps, "uri", video_file, NULL);
  gst_caps_unref(h264_caps);
  g_free(video_file);

  GstCaps* nv21_caps = gst_caps_from_string("video/x-raw(memory:mlu), format=(string)NV21");
  g_object_set(G_OBJECT(caps), "caps", nv21_caps, NULL);
  gst_caps_unref(nv21_caps);

  g_signal_connect(source, "pad-added", G_CALLBACK(src_handle_pad_added), parser);

  gst_bin_add_many(GST_BIN(pipeline), source, parser, dec, caps, appsink, NULL);
  fail_unless(gst_element_link_many(parser, dec, caps, appsink, NULL));
  GstPad* sinkpad = gst_element_get_static_pad(appsink, "sink");
  gst_pad_set_event_function(sinkpad, test_sink);
  gst_buffer_straw_start_pipeline(pipeline, sinkpad);

  for (int i = 0; !eos.load(); i++) {
    GstBuffer* buf;
    buf = gst_buffer_straw_get_buffer(pipeline, sinkpad);

    MluMemoryMeta* mlu_memory_meta = gst_buffer_get_mlu_memory_meta(buf);
    fail_unless_equals_string("cnvideo_dec", mlu_memory_meta->meta_src);
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
  GstElement *pipeline, *source, *parser, *dec, *appsink, *caps;
  guint test_frame_nums = 3;
  gchar current_path[128];
  memset(current_path, 0x00, sizeof(current_path));
  fail_unless(getcwd(current_path, sizeof(current_path) - 1));
  gchar* video_file = g_strconcat("file://", current_path, "/../samples/data/videos/1080P.h264", NULL);

  g_print("test h264 nv12\n");
  /* construct a pipeline that explicitly uses cnvideodec h264 */
  pipeline = gst_pipeline_new(NULL);

  source = gst_check_setup_element("uridecodebin");
  parser = gst_check_setup_element("h264parse");
  dec = gst_check_setup_element("cnvideo_dec");
  appsink = gst_check_setup_element("appsink");
  caps = gst_check_setup_element("capsfilter");

  GstCaps* h264_caps = gst_caps_new_empty_simple("video/x-h264");
  g_object_set(G_OBJECT(source), "caps", h264_caps, "uri", video_file, NULL);
  gst_caps_unref(h264_caps);
  g_free(video_file);

  GstCaps* nv12_caps = gst_caps_from_string("video/x-raw(memory:mlu), format=(string)NV12");
  g_object_set(G_OBJECT(caps), "caps", nv12_caps, NULL);
  gst_caps_unref(nv12_caps);

  g_signal_connect(source, "pad-added", G_CALLBACK(src_handle_pad_added), parser);

  gst_bin_add_many(GST_BIN(pipeline), source, parser, dec, caps, appsink, NULL);
  fail_unless(gst_element_link_many(parser, dec, caps, appsink, NULL));
  GstPad* sinkpad = gst_element_get_static_pad(appsink, "sink");
  gst_buffer_straw_start_pipeline(pipeline, sinkpad);

  for (guint i = 0; i < test_frame_nums; i++) {
    GstBuffer* buf;
    buf = gst_buffer_straw_get_buffer(pipeline, sinkpad);

    MluMemoryMeta* mlu_memory_meta = gst_buffer_get_mlu_memory_meta(buf);
    fail_unless_equals_string("cnvideo_dec", mlu_memory_meta->meta_src);
    gst_mlu_frame_unref(mlu_memory_meta->frame);
    mlu_memory_meta->frame = nullptr;

    gst_buffer_unref(buf);
  }

  gst_buffer_straw_stop_pipeline(pipeline, sinkpad);
  gst_object_unref(pipeline);
  gst_object_unref(sinkpad);
}
GST_END_TEST;

GST_START_TEST(test_h264dec_I420_explicit)
{
  GstElement *pipeline, *source, *parser, *dec, *appsink, *caps;
  guint test_frame_nums = 3;
  gchar current_path[128];
  memset(current_path, 0x00, sizeof(current_path));
  fail_unless(getcwd(current_path, sizeof(current_path) - 1));
  gchar* video_file = g_strconcat("file://", current_path, "/../samples/data/videos/1080P.h264", NULL);

  g_print("test h264 I420\n");
  /* construct a pipeline that explicitly uses cnvideodec h264 */
  pipeline = gst_pipeline_new(NULL);

  source = gst_check_setup_element("uridecodebin");
  parser = gst_check_setup_element("h264parse");
  dec = gst_check_setup_element("cnvideo_dec");
  appsink = gst_check_setup_element("appsink");
  caps = gst_check_setup_element("capsfilter");

  GstCaps* h264_caps = gst_caps_new_empty_simple("video/x-h264");
  g_object_set(G_OBJECT(source), "caps", h264_caps, "uri", video_file, NULL);
  gst_caps_unref(h264_caps);
  g_free(video_file);

  GstCaps* i420_caps = gst_caps_from_string("video/x-raw(memory:mlu), format=(string)I420");
  g_object_set(G_OBJECT(caps), "caps", i420_caps, NULL);
  gst_caps_unref(i420_caps);

  g_signal_connect(source, "pad-added", G_CALLBACK(src_handle_pad_added), parser);

  gst_bin_add_many(GST_BIN(pipeline), source, parser, dec, caps, appsink, NULL);
  fail_unless(gst_element_link_many(parser, dec, caps, appsink, NULL));
  GstPad* sinkpad = gst_element_get_static_pad(appsink, "sink");
  gst_buffer_straw_start_pipeline(pipeline, sinkpad);

  for (guint i = 0; i < test_frame_nums; i++) {
    GstBuffer* buf;
    buf = gst_buffer_straw_get_buffer(pipeline, sinkpad);

    MluMemoryMeta* mlu_memory_meta = gst_buffer_get_mlu_memory_meta(buf);
    fail_unless_equals_string("cnvideo_dec", mlu_memory_meta->meta_src);
    gst_mlu_frame_unref(mlu_memory_meta->frame);
    mlu_memory_meta->frame = nullptr;

    gst_buffer_unref(buf);
  }

  gst_buffer_straw_stop_pipeline(pipeline, sinkpad);
  gst_object_unref(pipeline);
  gst_object_unref(sinkpad);
}
GST_END_TEST;

Suite*
cnvideodec_suite(void)
{
  Suite* s = suite_create("cnvideo_dec");
  TCase* tc_chain = tcase_create("general");

  suite_add_tcase(s, tc_chain);
  tcase_add_test(tc_chain, test_properties);
  tcase_add_test(tc_chain, test_h264dec_NV21_explicit);
  tcase_add_test(tc_chain, test_h264dec_NV12_explicit);
  tcase_add_test(tc_chain, test_h264dec_I420_explicit);
  return s;
}

#endif  // WITH_DECODE
