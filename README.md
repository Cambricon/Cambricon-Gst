# Gstreamer-Cambricon-SDK

## Requirements

### Hardware requirements

Cambricon MLU270/MLU220

### Software requirements

Cambricon Neuware SDK: (same version as cnstream-gst)
  * CNRT
  * CNCODEC
  * EDK (Easy Development Kit)

We use CNRT for memory managment, CNCODEC for decode and encode.
EDK is a high-level API encapsulation.

3rdparty:
  * gstreamer suite    1.8.0+

> Gstreamer suite, include framework and core plugins, is basic of
this project.

Installation commands on different platform are listed as follows.

**Platform: Ubuntu 16.04+**
```bash
sudo apt install gstreamer1.0-plugins-base \
                 gstreamer1.0-plugins-good \
                 gstreamer1.0-plugins-bad \
                 gstreamer1.0-plugins-bad-videoparsers \
                 libgstreamer1.0-0 \
                 libgstreamer1.0-dev \
                 libgstreamer-plugins-base1.0-0 \
                 libgstreamer-plugins-base1.0-dev \
                 libgstreamer-plugins-good1.0-0 \
                 libgstreamer-plugins-bad1.0-0
```

**Platform: Debian 9.5+**
```bash
sudo apt install gstreamer1.0-plugins-base \
                 gstreamer1.0-plugins-good \
                 gstreamer1.0-plugins-bad \
                 libgstreamer1.0-0 \
                 libgstreamer1.0-dev \
                 libgstreamer-plugins-base1.0-0 \
                 libgstreamer-plugins-base1.0-dev \
                 libgstreamer-plugins-bad1.0-0
```

**Platform: CentOS 7.2+**
```shell
sudo yum install gstreamer1.x86_64 \
                 gstreamer1-devel.x86_64 \
                 gstreamer1-plugins-bad-free.x86_64 \
                 gstreamer1-plugins-bad-free-devel.x86_64 \
                 gstreamer1-plugins-base.x86_64 \
                 gstreamer1-plugins-base-devel.x86_64 \
                 gstreamer1-plugins-base-tools.x86_64 \
                 gstreamer1-plugins-good.x86_64
```

## Install

Move directory anywhere you like, add dir `lib` to GST_PLUGIN_PATH,
add dir `/path-to-neuware/lib64` to LD_LIBRARY_PATH.

To check if installed properly, use `gst-inspect-1.0 cndecode`, or
`gst-inspect-1.0 cninfer`. Library has been install succeeded,
if neither "no such element or plugin" printed, nor errors or warnings occur.

## Plugins

* cndecode: decode plugin
* cnconvert: video convert plugin
* cnencode: encode plugin

See *Developer Guide* for more detail.

> the plugins run on MLU are prefixed by 'cn'

## Samples

There are samples of two sorts, showing how to use those plugins.
1. build pipeline with C/C++ code, compile and go (transcode demo)
2. describe pipeline in shell, and run it using gst-launch-1.0 (launch-scripts)

### Build & Run

Make sure `cmake make gcc` installed properly.
Simply use compile.sh to build them with cmake and make, and run.sh to run.

