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

#ifdef WITH_ENCODE

#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <unistd.h>

#include <cstdio>

#include "cn_codec_common.h"

static GstStaticPadTemplate srctemplate =
  GST_STATIC_PAD_TEMPLATE("src",
                          GST_PAD_SRC,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{ NV21, NV12, I420, BGRA, RGBA, ABGR, ARGB }")));

static GstStaticPadTemplate sinktemplate =
  GST_STATIC_PAD_TEMPLATE("sink",
                          GST_PAD_SINK,
                          GST_PAD_ALWAYS,
                          GST_STATIC_CAPS("video/x-h264, stream-format=byte-stream, alignment=nal;"
                                          "video/x-h265, stream-format=byte-stream, alignment=nal;"));

static GstPad *mysinkpad, *mysrcpad;
static const char *input_name = NULL, *output_name = NULL;
static FILE *test_stream = NULL, *output_file = NULL;
static guint input_count = 0, output_count = 0, frame_count = 0;
static gboolean got_eos = FALSE;

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

static gboolean
setup_stream(const gchar* input, const gchar* output)
{
  if (test_stream == NULL) {
    test_stream = fopen(input, "rb");
    fail_unless(test_stream != NULL);
  }

  input_name = input;

  if (output_file == NULL) {
    output_file = fopen(output, "wb");
    fail_unless(output_file != NULL);
  }

  output_name = output;

  return TRUE;
}

static void
cleanup_stream(void)
{
  if (test_stream) {
    fflush(test_stream);
    fclose(test_stream);
    test_stream = NULL;
  }

  if (output_file) {
    fflush(output_file);
    fclose(output_file);
    output_file = NULL;
  }
}

static gboolean
feed_stream(GstVideoFormat format, guint width, guint height)
{
  int frame_size = 0;
  GstSegment seg;
  GstBuffer* buffer;

  input_count = output_count = 0;
  frame_count = 0;
  got_eos = FALSE;

  if (format == GST_VIDEO_FORMAT_NV12 || format == GST_VIDEO_FORMAT_NV21) {
    frame_size = width * height * 3 / 2;
  } else if (format == GST_VIDEO_FORMAT_BGRA || format == GST_VIDEO_FORMAT_RGBA || format == GST_VIDEO_FORMAT_ABGR ||
             format == GST_VIDEO_FORMAT_ARGB) {
    frame_size = width * height * 4;
  } else {
    fail("unsupported video format!");
  }

  fail_unless(test_stream != NULL, "test stream is NULL");

  fseek(test_stream, 0, SEEK_END);
  int file_len = ftell(test_stream);
  fail_unless(file_len > 0, "input file size invalid!");
  fseek(test_stream, 0, SEEK_SET);

  gst_segment_init(&seg, GST_FORMAT_TIME);
  // seg.stop = gst_util_uint64_scale(10, GST_SECOND, 25);
  fail_unless(gst_pad_push_event(mysrcpad, gst_event_new_segment(&seg)));

  frame_count = file_len / frame_size;
  if (file_len % frame_size != 0)
    frame_count++;

  int length = file_len;
  while (length > 0) {
    buffer = gst_buffer_new_and_alloc(frame_size);
    gst_buffer_memset(buffer, 0, 0, frame_size);

    GstMapInfo info;
    size_t nr = length < frame_size ? length : frame_size;
    size_t r;
    gst_buffer_map(buffer, &info, GST_MAP_WRITE);
    r = fread(info.data, 1, nr, test_stream);
    fail_unless(r == nr, "read r(%d) != nr(%d)", r, nr);
    gst_buffer_unmap(buffer, &info);

    GST_BUFFER_TIMESTAMP(buffer) = gst_util_uint64_scale(input_count, GST_SECOND, 25);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, 25);

    // g_print("Sending %d frame\n", input_count);
    fail_unless(gst_pad_push(mysrcpad, buffer) == GST_FLOW_OK);

    input_count++;
    length -= nr;
  }

  fail_unless(gst_pad_push_event(mysrcpad, gst_event_new_eos()));

  return TRUE;
}

static void
save_output(GstBuffer* buffer)
{
  fail_unless(output_file != NULL, "output file is NULL");

  GstMapInfo info;
  size_t w;
  gst_buffer_map(buffer, &info, GST_MAP_READ);
  w = fwrite(info.data, 1, info.size, output_file);
  fail_unless(w == info.size, "written size(%d) != info.size(%d)", (unsigned int)w, info.size);

  gst_buffer_unmap(buffer, &info);
}

static GstFlowReturn
mysinkpad_chain(GstPad* pad, GstObject* parent, GstBuffer* buffer)
{
  save_output(buffer);

  gst_buffer_unref(buffer);

  output_count++;

  if (output_count >= frame_count) {
    // g_print("output_count(%d) >= frame_count(%d), EOS for MLU100\n",
    //    output_count, frame_count);
    // got_eos = TRUE;
  }

  return GST_FLOW_OK;
}

/* this function handles sink events */
static gboolean
mysinkpad_event(GstPad* pad, GstObject* parent, GstEvent* event)
{
  gboolean ret = TRUE;

  // g_print("Received %s event: %p\n", GST_EVENT_TYPE_NAME(event), event);

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
      GstCaps* caps;
      gst_event_parse_caps(event, &caps);
      // g_print("mysinkpad_event_set_caps: %s\n", gst_caps_to_string(caps));
      gst_event_unref(event);
      break;
    }
    case GST_EVENT_EOS: {
      got_eos = TRUE;
      // g_print ("Got EOS\n");
      gst_event_unref(event);
      break;
    }
    default:
      ret = gst_pad_event_default(pad, parent, event);
      break;
  }
  return ret;
}

static GstElement*
setup_cnvideoenc(const gchar* src_caps_str, cncodecType codec_type)
{
  GstElement* cnvideoenc;
  GstCaps* srccaps = NULL;

  if (src_caps_str) {
    srccaps = gst_caps_from_string(src_caps_str);
    fail_unless(srccaps != NULL);
  }
  // check factory make element
  cnvideoenc = gst_check_setup_element("cnvideo_enc");
  fail_unless(cnvideoenc != NULL);

  g_object_set(cnvideoenc, "silent", TRUE, "codec", codec_type, NULL);

  // check element's sink pad link
  mysrcpad = gst_check_setup_src_pad(cnvideoenc, &srctemplate);
  // check element's src pad link
  mysinkpad = gst_check_setup_sink_pad(cnvideoenc, &sinktemplate);

  gst_pad_set_chain_function(mysinkpad, GST_DEBUG_FUNCPTR(mysinkpad_chain));
  gst_pad_set_event_function(mysinkpad, GST_DEBUG_FUNCPTR(mysinkpad_event));

  gst_pad_set_active(mysrcpad, TRUE);
  gst_pad_set_active(mysinkpad, TRUE);

  // check START/SEGMENT/CAPS event
  gst_check_setup_events(mysrcpad, cnvideoenc, srccaps, GST_FORMAT_TIME);

  // check element state change
  fail_unless(gst_element_set_state(cnvideoenc, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
              "could not set to playing");

  if (srccaps)
    gst_caps_unref(srccaps);

  buffers = NULL;
  return cnvideoenc;
}

static void
cleanup_cnvideoenc(GstElement* cnvideoenc)
{
  /* Free parsed buffers */
  gst_check_drop_buffers();

  gst_pad_set_active(mysrcpad, FALSE);
  gst_pad_set_active(mysinkpad, FALSE);
  gst_check_teardown_src_pad(cnvideoenc);
  gst_check_teardown_sink_pad(cnvideoenc);
  gst_check_teardown_element(cnvideoenc);
}

// check element factory make, this equals to gst_check_setup_element
GST_START_TEST(test_cnvideoenc_create_destroy)
{
  GstElement* cnvideoenc;

  g_print("test_cnvideoenc_create_destroy()\n");
  g_print("cwd: %s\n", GetExePath());

  cnvideoenc = gst_element_factory_make("cnvideo_enc", NULL);
  gst_object_unref(cnvideoenc);
}
GST_END_TEST;

// check out caps
GST_START_TEST(test_cnvideoenc_outcaps)
{
  GstElement* cnvideoenc;
  GstCaps *outcaps, *srccaps = NULL;

  g_print("test_cnvideoenc_outcaps()\n");

  // setup the element for testing
  cnvideoenc = setup_cnvideoenc("video/x-raw,format=(string)NV21,"
                            "width=(int)1920,height=(int)1080,"
                            "framerate=(fraction)25/1",
                            CNCODEC_H264);

  outcaps = gst_caps_from_string("video/x-h264,stream-format=byte-stream,alignment=nal,"
                                 "width=(int)1920,height=(int)1080,"
                                 "framerate=(fraction)25/1");

  srccaps = gst_pad_peer_query_caps(mysinkpad, NULL);
  fail_unless(srccaps != NULL);
  fail_unless(gst_caps_can_intersect(outcaps, srccaps));

  if (srccaps)
    gst_caps_unref(srccaps);
  gst_caps_unref(outcaps);

  // check EOS event
  // fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  // tear down the element
  cleanup_cnvideoenc(cnvideoenc);
}
GST_END_TEST;

GST_START_TEST(test_cnvideoenc_property)
{
  GstElement* cnvideoenc;

  g_print("test_cnvideoenc_property()\n");

  // setup the element for testing
  cnvideoenc = setup_cnvideoenc("video/x-raw,format=(string)NV21,"
                            "width=(int)720,height=(int)480,"
                            "framerate=(fraction)25/1",
                            CNCODEC_H264);

  gboolean vbr;
  gint id, prof, level;
  guint gop, p, b, gop_type, br, max_br, max_qp, min_qp;
  g_object_set(G_OBJECT(cnvideoenc), "device-id", 0, "vbr", TRUE, "gop-length", 50, "profile", 3, "level", 5,
               "i-frame-interval", 30, "b-frame-num", 10, "gop-type", 2, "bitrate", 512,
               "max-bitrate", 1024, "max-qp", 50, "min-qp", 20, NULL);

  g_object_get(G_OBJECT(cnvideoenc), "device-id", &id, "vbr", &vbr, "gop-length", &gop, "profile", &prof,
               "level", &level, "i-frame-interval", &p, "b-frame-num", &b, "gop-type",
               &gop_type, "bitrate", &br, "max-bitrate", &max_br, "max-qp", &max_qp, "min-qp", &min_qp, NULL);

  fail_unless_equals_int(id, 0);
  fail_unless_equals_int(prof, 3);
  fail_unless_equals_int(level, 5);
  fail_unless_equals_int(gop, 50);
  fail_unless_equals_int(p, 30);
  fail_unless_equals_int(b, 10);
  fail_unless_equals_int(gop_type, 2);
  fail_unless_equals_int(br, 512);
  fail_unless_equals_int(max_br, 1024);
  fail_unless_equals_int(max_qp, 50);
  fail_unless_equals_int(min_qp, 20);

  // tear down the element
  cleanup_cnvideoenc(cnvideoenc);
}
GST_END_TEST;

GST_START_TEST(test_cnvideoenc_NV21_H264)
{
  GstElement* cnvideoenc;

  g_print("test_cnvideoenc_NV21_H264()\n");

  // setup the element for testing
  cnvideoenc = setup_cnvideoenc("video/x-raw,format=(string)NV21,"
                            "width=(int)1280,height=(int)720,"
                            "framerate=(fraction)25/1",
                            CNCODEC_H264);

  setup_stream("../tests/data/cars_nv21.yuv", "cars_nv21.h264");
  feed_stream(GST_VIDEO_FORMAT_NV21, 1280, 720);

  // wait encoder finish encoding work
  while (got_eos == FALSE) {
    usleep(10000);
  }

  // g_print("encoder finished encoding!\n");

  cleanup_stream();

  // tear down the element
  cleanup_cnvideoenc(cnvideoenc);
}
GST_END_TEST;

GST_START_TEST(test_cnvideoenc_NV12_H264)
{
  GstElement* cnvideoenc;

  g_print("test_cnvideoenc_NV12_H264()\n");

  // setup the element for testing
  cnvideoenc = setup_cnvideoenc("video/x-raw,format=(string)NV12,"
                            "width=(int)1280,height=(int)720,"
                            "framerate=(fraction)25/1",
                            CNCODEC_H264);

  setup_stream("../tests/data/cars_nv12.yuv", "cars_nv12.h264");
  feed_stream(GST_VIDEO_FORMAT_NV12, 1280, 720);

  // wait encoder finish encoding work
  while (got_eos == FALSE) {
    usleep(10000);
  }

  // g_print("encoder finished encoding!\n");

  cleanup_stream();

  // tear down the element
  cleanup_cnvideoenc(cnvideoenc);
}
GST_END_TEST;

GST_START_TEST(test_cnvideoenc_NV12_H265)
{
  GstElement* cnvideoenc;

  g_print("test_cnvideoenc_NV12_H265()\n");

  // setup the element for testing
  cnvideoenc = setup_cnvideoenc("video/x-raw,format=(string)NV12,"
                            "width=(int)1280,height=(int)720,"
                            "framerate=(fraction)25/1",
                            CNCODEC_HEVC);

  setup_stream("../tests/data/cars_nv12.yuv", "cars_nv12.h265");
  feed_stream(GST_VIDEO_FORMAT_NV12, 1280, 720);

  // wait encoder finish encoding work
  while (got_eos == FALSE) {
    usleep(10000);
  }

  // g_print("encoder finished encoding!\n");

  cleanup_stream();

  // tear down the element
  cleanup_cnvideoenc(cnvideoenc);
}
GST_END_TEST;

GST_START_TEST(test_cnvideoenc_NV21_H265)
{
  GstElement* cnvideoenc;

  g_print("test_cnvideoenc_NV21_H265()\n");

  // setup the element for testing
  cnvideoenc = setup_cnvideoenc("video/x-raw,format=(string)NV21,"
                            "width=(int)1280,height=(int)720,"
                            "framerate=(fraction)25/1",
                            CNCODEC_HEVC);

  setup_stream("../tests/data/cars_nv21.yuv", "cars_nv21.h265");
  feed_stream(GST_VIDEO_FORMAT_NV21, 1280, 720);

  // wait encoder finish encoding work
  while (got_eos == FALSE) {
    usleep(10000);
  }

  // g_print("encoder finished encoding!\n");

  cleanup_stream();

  // tear down the element
  cleanup_cnvideoenc(cnvideoenc);
}
GST_END_TEST;

Suite*
cnvideoenc_suite(void)
{
  Suite* s = suite_create("cnvideo_enc");
  TCase* tc_chain = tcase_create("general");

  suite_add_tcase(s, tc_chain);

  tcase_add_test(tc_chain, test_cnvideoenc_create_destroy);
  tcase_add_test(tc_chain, test_cnvideoenc_outcaps);
  tcase_add_test(tc_chain, test_cnvideoenc_property);
  tcase_add_test(tc_chain, test_cnvideoenc_NV12_H264);
  tcase_add_test(tc_chain, test_cnvideoenc_NV21_H264);
  tcase_add_test(tc_chain, test_cnvideoenc_NV12_H265);
  tcase_add_test(tc_chain, test_cnvideoenc_NV21_H265);

  return s;
}

#endif  // WITH_ENCODE
