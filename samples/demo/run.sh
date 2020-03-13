#!/bin/bash
CURRENT_DIR=$(readlink -f $(dirname $0))
if [ -z ${NEUWARE_HOME} ]; then
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${NEUWARE_HOME}/lib64
fi
gst_path="${CURRENT_DIR}/../../lib"
export GST_PLUGIN_PATH=${gst_path}
${CURRENT_DIR}/bin/demo \
  --video_path=${CURRENT_DIR}/../data/videos/1080P.h264 \
  --output_path=output.h264

