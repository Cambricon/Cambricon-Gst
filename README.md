EN|[CN](README_cn.md)

Cambricon<sup>®</sup> Gstreamer SDK
======================================

Based on Cambricon MLU hardware platform and the open source Gstreamer framework, Cambricon<sup>®</sup> Gstreamer SDK is a media library that allows you to quickly validate and build your AI applications. You can migrate to Cambricon MLU platform from other hardware platforms easily with low costs. 

## Requirements ##

Before you install Cambricon Gstreamer SDK, make sure the hardware, operating system, and software requirements are met.

### Hardware Requirements ###

Cambricon Gstreamer SDK can only be supported on Cambricon MLU270 and MLU220.

### Operating System Requirements ###

The following operating systems are supported:

- Ubuntu 16.04+
- Debian 9.5+
- CentOS 7.2+

### Software Requirements ###

You need to install the following software before building Cambricon Gstreamer SDK.

- Cambricon Neuware SDK. Use the same version as Cambricon Gstreamer SDK.

- Third-Party Software:

  *  Gstreamer suite 1.8.0+
  *  cmake 2.8.7+
  *  make 3.8.2+
  *  gcc 4.9.4+

     > Gstreamer suite includes framework and core plugins that is the basic of this project.

## Build and Installation ##

Perform the following steps to build and install Cambricon Gstreamer SDK:

1. Run the following command to install Cambricon Gstreamer SDK dependencies:

   -  Ubuntu

      ```shell
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

   -  Debian
      
	  ```shell
      sudo apt install gstreamer1.0-plugins-base \
                       gstreamer1.0-plugins-good \
                       gstreamer1.0-plugins-bad \
                       libgstreamer1.0-0 \
                       libgstreamer1.0-dev \
                       libgstreamer-plugins-base1.0-0 \
                       libgstreamer-plugins-base1.0-dev \
                       libgstreamer-plugins-bad1.0-0
      ```

   -  CentOS
   
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

2. Compile Cambricon Gstreamer SDK.

   1. Run the following command to install `cmake make gcc`. Make sure `cmake make gcc` is installed properly.
   
      ```shell
        sudo apt install cmake make gcc
      ```

   2. Run the CMake script, [CMakeLists.txt](CMakeLists.txt), and then run the ``make`` command to compile Cambricon Gstreamer SDK:

      ```shell
        mkdir -p build; cd build
        cmake ${CNSTREAMGST_DIR}
        make
      ```

      You can also call cmake command with command options. See [Building Instructions with cmake Command](#CMake_Command).
	  
   The ``libgstcnstream.so`` binary file will be generated under the ``cnstream-gst/lib`` directory. You can move the location of the `libgstcnstream.so` library to anywhere you want to. 

3. Set the following environment variables. 
   
   - Add the path of the folder that contains ``libgstcnstream.so`` to GST_PLUGIN_PATH.
   - Add the path of the ``/path-to-neuware/lib64`` directory to LD_LIBRARY_PATH.

To check if Cambricon Gstreamer SDK has been installed successfully, run the ``gst-inspect-1.0 cnvideo_dec`` or ``gst-inspect-1.0 cnconvert`` command. If no errors or warnings, such as "no such element or plugin" occurred, the library has been installed successfully.

For more information about how to build and run applications, see [Build and Run Samples](#build_sample).

# <a name="CMake_Command"></a> 
## Building Instructions with cmake Command ##

You can call the following cmake command with command options. 

```     
cmake [dir] -DBUILD_TESTS=ON -DWITH_CONVERT=ON
```
     
The following table shows the meaning of the supported options.

| cmake Option       | Range           | Default | Description                            |
| ------------------ | --------------- | ------- | -------------------------------------- |
| BUILD_SAMPLES      | ON / OFF        | OFF     | Build samples.                         |
| BUILD_TESTS        | ON / OFF        | OFF     | Build tests.                           |
| RELEASE            | ON / OFF        | ON      | Build release and debug version.       |
| WITH_DECODE        | ON / OFF        | ON      | Build cnvideo_dec plugin for decoding. |
| WITH_CONVERT       | ON / OFF        | ON      | Build cnconvert plugin for conversion. |
| WITH_ENCODE        | ON / OFF        | ON      | Build cnvideo_enc plugin for encoding. |

# <a name="plugin"></a> 	  
## Introduction to Plugins ##

Cambricon Gstreamer SDK supports the following plugins to build your AI applications:

* cnvideodec: Video decoding, support h.264, h.265.
* cnconvert: Color space conversion and image scaling.
* cnvideoenc: Video encoding, support h.264, h.265.

For detailed information about the plugins, run the following command. You need to replace *plugin* with the name of the plugin you want to check, such as cnvideo_dec.

```
  gst-inspect-1.0 plugin
```

> The plugins run on MLU are with the prefix 'cn'.

To learn more about how to build applications with plugins, see [Build and Run Samples](#build_sample).

Cambricon continues to support more plugins, such as inference and track in future releases.

## Samples ##

Cambricon provides you two samples to show how to build applications with Cambricon Gstreamer SDK. The samples introduce two methods for building applications. The samples are located in the ``cnstream-gst/launch-scripts`` directory. 

- Transcode demo: Transcodes with the cnvideo_dec, cnconvert, and cnvideo_enc plugins. This demo is located in the ``cnstream-gst/samples`` directory.
- Launch-scripts: Includes two samples that are run via command-line. This is mainly used for validation and testing.
  
  -  run-video-transcode.sh: Transcodes with the cnvideo_dec, cnconvert, and cnvideo_enc plugins. This sample has the same function as the transcode demo.

# <a name="build_sample"></a> 
### Build and Run Samples ###

Samples can be executed in the following ways:

- Transcode demo: Build pipeline with C or C++ codes, and then compile the demo. To compile the demo in samples with plugins library, run the cmake command with the ``-DBUILD_SAMPLES=ON`` option. You can run the demo with the following command:
 
  ```
  ./samples/demo/run.sh
  ```

- Launch-scripts: Describe the pipeline in shell, and run it with the ``gst-launch-1.0`` command. You can use ``samples/launch-script/run-video-transcode.sh`` to launch the transcode samples. 

## Project Migration ##

To migrate to Cambricon MLU platform from other hardware platforms via Gstreamer, you only need to replace the plugins with Cambricon plugins. For detailed information about plugins, see [Introduction to Plugins](#plugin).
