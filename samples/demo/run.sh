#!/bin/bash
CURRENT_DIR=$(readlink -f $(dirname $0))

if [ -z ${NEUWARE_HOME} ]; then
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${NEUWARE_HOME}/lib64
else
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/neuware/lib64
fi

export GST_DEBUG=$1
export GST_PLUGIN_PATH=$GST_PLUGIN_PATH:${CURRENT_DIR}/../../lib
${CURRENT_DIR}/bin/demo \
  --video_path=${CURRENT_DIR}/../data/videos/1080P.h264 \
  --output_path=output.h264

