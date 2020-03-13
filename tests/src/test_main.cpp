#include <gst/check/gstcheck.h>

extern Suite*
cnconvert_suite(void);
extern Suite*
cndecode_suite(void);
extern Suite*
cnencode_suite(void);

int
main(int argc, char** argv)
{
  int ret = 0;
  Suite *encode, *convert, *decode;

  gst_check_init(&argc, &argv);

  encode = cnencode_suite();
  convert = cnconvert_suite();
  decode = cndecode_suite();

  ret += gst_check_run_suite(decode, "cndecode", __FILE__);
  ret += gst_check_run_suite(convert, "cnconvert", __FILE__);
  ret += gst_check_run_suite(encode, "cnencode", __FILE__);

  return ret;
}
