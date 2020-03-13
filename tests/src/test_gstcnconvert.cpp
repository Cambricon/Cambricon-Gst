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

#include <gst/check/gstbufferstraw.h>
#include <gst/check/gstcheck.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "common/mlu_memory_meta.h"
#include "convert/gstcnconvert.h"

static GstStaticPadTemplate sink_template =
  GST_STATIC_PAD_TEMPLATE("sink",
                          GST_PAD_SINK,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS("video/x-raw(memory:mlu), format={NV12, NV21};"));

static GstStaticPadTemplate src_template =
  GST_STATIC_PAD_TEMPLATE("src",
                          GST_PAD_SRC,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS("video/x-raw(memory:mlu), format={NV12, NV21, RGBA, ARGB, BGRA, ABGR};"
                                          "video/x-raw, format={NV12, NV21, RGBA, ARGB, BGRA, ABGR};"));

const char* in_caps_str = "video/x-raw(memory:mlu), format=NV12, width=1280, height=720;";

const char* out_caps_str = "video/x-raw(memory:mlu), format={NV12, RGBA, ARGB, BGRA, ABGR};"
                           "video/x-raw, format={NV12, RGBA, ARGB, BGRA, ABGR};";

GST_START_TEST(test_create_and_destroy)
{
  GstElement* convert;

  convert = gst_check_setup_element("cnconvert");
  fail_if(!convert);

  gst_check_teardown_element(convert);
}
GST_END_TEST;

GST_START_TEST(test_outcaps)
{
  GstElement* convert;
  GstPad *srcpad, *sinkpad;
  GstCaps *srccaps, *sinkcaps, *outcaps;

  // test cnconvert caps
  sinkcaps = gst_caps_from_string(in_caps_str);
  outcaps = gst_caps_from_string(out_caps_str);
  fail_if(sinkcaps == NULL || outcaps == NULL);

  convert = gst_check_setup_element("cnconvert");
  fail_if(convert == NULL);
  srcpad = gst_check_setup_src_pad(convert, &src_template);
  sinkpad = gst_check_setup_sink_pad(convert, &sink_template);
  fail_if(srcpad == NULL || sinkpad == NULL);

  gst_pad_set_active(srcpad, TRUE);
  gst_pad_set_active(sinkpad, TRUE);

  ASSERT_SET_STATE(convert, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);
  gst_check_setup_events(srcpad, convert, sinkcaps, GST_FORMAT_TIME);
  srccaps = gst_pad_peer_query_caps(sinkpad, NULL);
  fail_if(srccaps == NULL);

  fail_unless(gst_caps_can_intersect(srccaps, outcaps));

  gst_caps_unref(srccaps);
  gst_caps_unref(sinkcaps);
  gst_caps_unref(outcaps);
  gst_pad_set_active(srcpad, FALSE);
  gst_pad_set_active(sinkpad, FALSE);
  gst_check_teardown_sink_pad(convert);
  gst_check_teardown_src_pad(convert);
  gst_check_teardown_element(convert);
}
GST_END_TEST;

static gboolean event_received = FALSE;

static gboolean
test_event(GstPad* pad, GstObject* parent, GstEvent* event)
{
  event_received = TRUE;
  gst_event_unref(event);
  return TRUE;
}

GST_START_TEST(test_event_func)
{
  GstElement* convert;
  GstPad *sinkpad, *srcpad, *mysrc, *mysink;

  // test convert
  convert = gst_check_setup_element("cnconvert");
  fail_if(convert == NULL);

  sinkpad = gst_element_get_static_pad(convert, "sink");
  srcpad = gst_element_get_static_pad(convert, "src");
  fail_if(sinkpad == NULL || srcpad == NULL);
  gst_pad_set_active(sinkpad, TRUE);
  gst_pad_set_active(srcpad, TRUE);

  mysrc = gst_pad_new("mysrc", GST_PAD_SRC);
  mysink = gst_pad_new("mysink", GST_PAD_SINK);
  fail_if(mysrc == NULL || mysink == NULL);
  gst_pad_set_event_function(mysink, test_event);

  gst_pad_set_active(mysrc, TRUE);
  gst_pad_set_active(mysink, TRUE);

  fail_unless(gst_pad_link(mysrc, sinkpad) == GST_PAD_LINK_OK);
  fail_unless(gst_pad_link(srcpad, mysink) == GST_PAD_LINK_OK);

  fail_unless(gst_pad_push_event(mysrc, gst_event_new_eos()));
  fail_unless(event_received);

  gst_pad_set_active(mysrc, FALSE);
  gst_pad_set_active(mysink, FALSE);
  fail_unless(gst_pad_unlink(mysrc, sinkpad));
  fail_unless(gst_pad_unlink(srcpad, mysink));

  gst_pad_set_active(sinkpad, FALSE);
  gst_pad_set_active(srcpad, FALSE);
  gst_object_unref(srcpad);
  gst_object_unref(sinkpad);
  gst_check_teardown_element(convert);
  event_received = FALSE;

  gst_object_unref(mysrc);
  gst_object_unref(mysink);
}
GST_END_TEST;

static std::atomic<bool> eos(false);

static gboolean
test_convert_sink(GstPad* pad, GstObject* parent, GstEvent* event)
{
  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_EOS:
      eos.store(true);
      gst_event_unref(event);
      break;
    default:
      return gst_pad_event_default(pad, parent, event);
      break;
  }
  return FALSE;
}

static void
src_handle_pad_added(GstElement* src, GstPad* new_pad, GstElement* sink)
{
  GstPad* sink_pad = gst_element_get_static_pad(sink, "sink");
  if (!gst_pad_is_linked(sink_pad)) {
    gst_pad_link(new_pad, sink_pad);
  }
  g_object_unref(sink_pad);
}

unsigned char* g_data = NULL;

static char*
GetExePath(void)
{
  static char exe_path[1024];
  static bool start_flag = true;

  if (start_flag) {
    memset(exe_path, 0, sizeof(exe_path));
    unsigned int cnt = readlink("/proc/self/exe", exe_path, sizeof(exe_path));
    if (cnt < 0 || cnt >= sizeof(exe_path)) {
      printf("%s.error, readlink size:%d\n", __FUNCTION__, cnt);
      memset(exe_path, 0, sizeof(exe_path));
    } else {
      for (int i = cnt; i >= 0; --i) {
        if ('/' == exe_path[i]) {
          exe_path[i + 1] = '\0';
          break;
        }
      }
    }
    start_flag = false;
  }
  return exe_path;
}

GST_START_TEST(test_chain_func)
{
  gchar* current_path = GetExePath();

  // test convert
  gchar* video_file = g_strconcat("file://", current_path, "../../samples/data/videos/1080P.h264", NULL);
  GstElement* pipeline = NULL;
  GstPad* sinkpad = NULL;
  GstBuffer* buffer = NULL;
  GstElement *source = NULL, *parser = NULL, *decode = NULL, *convert = NULL, *appsink = NULL;
  pipeline = gst_pipeline_new("pipeline");
  source = gst_check_setup_element("uridecodebin");
  parser = gst_check_setup_element("h264parse");
  decode = gst_check_setup_element("cndecode");
  convert = gst_check_setup_element("cnconvert");
  appsink = gst_check_setup_element("appsink");
  GstCaps* caps = gst_caps_new_empty_simple("video/x-h264");
  g_object_set(G_OBJECT(source), "caps", caps, "uri", video_file, NULL);
  gst_caps_unref(caps);
  g_free(video_file);

  fail_unless(pipeline && source && parser && decode && convert && appsink);
  gst_bin_add_many(GST_BIN(pipeline), source, parser, decode, convert, appsink, NULL);
  fail_unless(gst_element_link_many(parser, decode, convert, appsink, NULL));
  g_signal_connect(source, "pad-added", G_CALLBACK(src_handle_pad_added), parser);
  sinkpad = gst_element_get_static_pad(appsink, "sink");
  gst_pad_set_event_function(sinkpad, test_convert_sink);
  gst_buffer_straw_start_pipeline(pipeline, sinkpad);

  // test 10 frames.
  for (int i = 0; !eos.load(); i++) {
    buffer = gst_buffer_straw_get_buffer(pipeline, sinkpad);
    fail_if(!buffer);
    gst_buffer_unref(buffer);
    g_usleep(1e5);
  }

  gst_buffer_straw_stop_pipeline(pipeline, sinkpad);
  g_object_unref(pipeline);
  g_object_unref(sinkpad);
}
GST_END_TEST;

Suite*
cnconvert_suite(void)
{
  Suite* s = suite_create("cnconvert");
  TCase* tc_chain = tcase_create("general");

  suite_add_tcase(s, tc_chain);
  tcase_add_test(tc_chain, test_create_and_destroy);
  tcase_add_test(tc_chain, test_outcaps);
  tcase_add_test(tc_chain, test_event_func);
  tcase_add_test(tc_chain, test_chain_func);
  return s;
}
