#!/bin/bash

CURRENT_DIR=$(dirname $(readlink -f $0) )

pushd $CURRENT_DIR
  rm *.h264 *.jpg *.h265 >/dev/null 2>&1
popd

