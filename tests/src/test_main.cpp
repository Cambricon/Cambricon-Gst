#include <gst/check/gstcheck.h>

#ifdef WITH_DECODE
extern Suite*
cnvideodec_suite(void);
#endif

#ifdef WITH_ENCODE
extern Suite*
cnvideoenc_suite(void);
#endif

#ifdef WITH_CONVERT
extern Suite*
cnconvert_suite(void);
#endif

int
main(int argc, char** argv)
{
  int ret = 0;

  gst_check_init(&argc, &argv);

#ifdef WITH_DECODE
  Suite *video_decode;
  video_decode = cnvideodec_suite();
  ret += gst_check_run_suite(video_decode, "cnvideo_dec", __FILE__);
#endif

#ifdef WITH_ENCODE
  Suite *video_encode;
  video_encode = cnvideoenc_suite();
  ret += gst_check_run_suite(video_encode, "cnvideo_enc", __FILE__);
#endif

#ifdef WITH_CONVERT
  Suite *convert;
  convert = cnconvert_suite();
  ret += gst_check_run_suite(convert, "cnconvert", __FILE__);
#endif

  return ret;
}
