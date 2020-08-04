CN|[EN](README.md)

寒武纪<sup>®</sup> Gstreamer SDK
======================================

寒武纪<sup>®</sup> Gstreamer SDK是基于寒武纪MLU硬件平台和开源Gstreamer框架，对AI应用进行快速验证和开发的媒体库。实现了方便、低成本地从其他硬件平台迁移到寒武纪MLU平台。

## 配置要求 ##

用户在安装寒武纪Gstreamer SDK前，需满足硬件、操作系统以及软件配置要求。

### 硬件要求 ###

寒武纪Gstreamer SDK仅支持在寒武纪MLU270和MLU220平台上运行。

### 操作系统要求 ###

寒武纪Gstreamer SDK可在如下操作系统中运行：

- Ubuntu 16.04+
- Debian 9.5+
- CentOS 7.2+

### 软件要求 ###

寒武纪Gstreamer SDK对下面软件有依赖：

- 寒武纪Neuware SDK。用户需要使用与寒武纪Gstreamer SDK相同版本的软件进行安装。

- 第三方软件依赖：

  *  Gstreamer suite 1.8.0+
  *  cmake 2.8.7+
  *  make 3.8.2+
  *  gcc 4.9.4+

     > Gstreamer suite包括框架和插件，是该项目的基础依赖。

## 构建与安装 ##

执行下面步骤构建并安装寒武纪Gstreamer SDK：

1. 运行下面的命令安装寒武纪Gstreamer SDK依赖：

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

2. 编译寒武纪Gstreamer SDK。

   1. 运行下面命令安装 `cmake make gcc`。确保 `cmake make gcc` 正确安装。  
   
      ```shell
        sudo apt install cmake make gcc
      ```

   2. 运行CMake脚本，[CMakeLists.txt](CMakeLists.txt)，然后运行 ``make`` 命令编译寒武纪Gstreamer SDK。

      ```shell
        mkdir -p build; cd build
        cmake ${CNSTREAMGST_DIR}
        make
      ```

	  根据需求，用户可以通过设置cmake选项来编译寒武纪Gstreamer SDK。了解详情，请查看[使用cmake命令构建指令](#CMake_Command)。
	  
   编译成功后，生成 ``libgstcnstream.so`` 二进制文件并存放在 ``cnstream-gst/lib`` 目录下。用户可以将该文件移动到任何目录下。 

3. 设置下面环境变量。 
   
   - 设置GST_PLUGIN_PATH为存放 ``libgstcnstream.so`` 文件的路径。
   - 设置LD_LIBRARY_PATH为 ``/path-to-neuware/lib64`` 路径。

如果想要检查寒武纪Gstreamer SDK是否安装成功，用户可以运行 ``gst-inspect-1.0 cnvideo_dec`` 或 ``gst-inspect-1.0 cnconvert`` 命令。如果没有错误或者警告，如“no such element or plugin”出现，则库安装成功。

了解如何运行和构建应用，请参看[示例的构建和运行](#build_sample)。

# <a name="CMake_Command"></a> 
## 使用cmake命令构建指令 ##

用户可以添加cmake选项来构建指令。格式如下：

```
cmake [dir] -DBUILD_TESTS=ON -DWITH_CONVERT=ON
```

下面列表展示了cmake命令支持的选项和含义：

| cmake选项          | 取值            | 默认值  | 描述                          |
| ------------------ | --------------- | ------- | ----------------------------- |
| BUILD_SAMPLES      | ON / OFF        | OFF     | 构建示例。                    |
| BUILD_TESTS        | ON / OFF        | OFF     | 构建测试程序。                |
| RELEASE            | ON / OFF        | ON      | 构建程序是否包含调试符号。    |
| WITH_DECODE        | ON / OFF        | ON      | 编译cnvideo_dec插件用于解码。 |
| WITH_CONVERT       | ON / OFF        | ON      | 编译cnconvert插件用于转码。   |
| WITH_ENCODE        | ON / OFF        | ON      | 编译cnvideo_enc插件用于编码。 |

# <a name="plugin"></a> 
## 插件介绍 ##

寒武纪Gstreamer SDK支持使用下面插件来构建AI应用：

* cnvideo_dec：解码视频，支持H264和H265。
* cnconvert：转换图像数据颜色空间，以及图像放缩。
* cnvideo_enc：编码视频，支持H264和H265。

有关的插件详细说明，可以运行下面的命令查看。用户需要替换命令中 *plugin* 为插件名，例如 cnvideo_dec。

```
  gst-inspect-1.0 plugin
```

> MLU上运行的插件带前缀“cn”。

关于如何使用插件构建应用，用户可以参考寒武纪提供的示例代码。

在将来的版本中，寒武纪会持续支持更多插件，如推理，跟踪等。

## 示例 ##

寒武纪提供两个示例来介绍如何通过寒武纪Gstreamer SDK构建应用。示例介绍了两种构建方法。示例位于 ``cambricon-gst/samples`` 目录下。

- Transcode demo：使用cnvideo_dec、cnconvert以及cnvideo_enc插件完成转码。该示例位于 ``cambricon-gst/samples/demo`` 目录下。
- Launch-scripts：包含两个示例，演示了如何使用命令行方式构建应用。该方法主要用于功能快速验证和测试。
  
  -  run-video-transcode.sh：使用cnvideo_dec、cnconvert以及cnvideo_enc插件完成转码。该示例与transcode demo实现的功能相同。

# <a name="build_sample"></a> 
### 构建并运行示例 ###

用户可以通过下面的方法构建并运行示例。

- Transcode demo：使用C或C++语言构建pipeline，然后编译demo。在编译项目时，执行cmake时附带选项 ``-DBUILD_SAMPLES=ON`` 可使能示例demo的编译。用户可以直接执行下面命令运行示例demo：
 
  ```samples/demo/run.sh```

- Launch-scripts：在shell里描述pipeline，并通过 ``gst-launch-1.0`` 命令运行。用户可以直接使用 ``samples/launch-script/run-video-transcode.sh`` 来运行示例。 

## 项目迁移 ##

如果用户想要从其他硬件平台通过Gstreamer，迁移到寒武纪MLU平台，只需替换插件为寒武纪支持的插件即可。插件详情，请查看[插件介绍](#plugin)。
