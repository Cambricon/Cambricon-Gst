CURRENT_DIR=$(dirname $(readlink -f $0) )

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$CURRENT_DIR/../mlu/MLU270/libs/x86_64
export GST_PLUGIN_PATH=$CURRENT_DIR/../lib
export CK_FORK=no
export GST_DEBUG=$1
pushd $CURRENT_DIR
  ./bin/gstcnstream-test
  ret=$?
  if [ $ret -ne 0 ]; then
    echo -e "\033[31m\033[7m" \
            "$ret testcases failed" \
            "\033[31m\033[0m"
  else
    echo -e "\033[32m\033[7m" \
            "gstcnstream test succeeded" \
            "\033[32m\033[0m"
  fi
popd

exit $ret
