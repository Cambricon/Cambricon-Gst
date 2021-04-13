[EN](README.md)|CN

# Cambricon<sup>®</sup> Easy Development Kit

EasyDK(Cambricon<sup>®</sup> Neuware Easy Development Kit)提供了一套面向 
MLU(Machine Learning Unit,寒武纪机器学习单元)设备的高级别的接口（C++11标准），
用于面向MLU平台（MLU270，MLU220）快速开发和部署深度学习应用。

EasyDK共包含如下6个模块:

  - Device: 提供MLU设备上下文及内存等相关操作
  - EasyCodec: 提供支持视频与图片的MLU硬件编解码功能
  - EasyInfer: 提供离线模型推理相关功能
  - EasyBang: 提供简易调用Bang算子的接口，目前支持的算子有ResizeConvertCrop和ResizeYuv
  - EasyTrack: 提供目标追踪的功能
  - cxxutil: 其他模块用到的部分cpp实现

![modules](docs/source/images/software_stack.png)

## 快速入门 ##

本节将简单介绍如何从零开始构建EasyDK，并运行示例代码完成简单的深度学习任务。

### 配置要求 ###

寒武纪EasyDK仅支持在寒武纪MLU270和MLU220平台上运行。

#### **构建和运行环境依赖** ####

构建和运行EasyDK有如下依赖：
  - CMake 2.8.7+
  - GCC   4.8.5+
  - GLog  0.3.4
  - Cambricon NEUWARE

测试程序和示例有额外的依赖：
  - OpenCV 2.4.9+
  - GFlags 2.1.2
  - FFmpeg 2.8 3.4 4.2

#### Ubuntu or Debian ####

如果您在使用Ubuntu或Debian，可以运行如下命令安装依赖：

   ```bash
   sudo apt install libgoogle-glog-dev
   # samples dependencies
   sudo apt install libgflags-dev libopencv-dev
   ```

#### CentOS ####

如果您在使用CentOS，可以运行如下命令安装依赖：

   ```bash
   sudo yum install glog
   # samples dependencies
   sudo yum install gflags opencv-devel
   ```

### 编译项目 ###

Easydk仅支持源码编译的方式使用，按如下步骤编译Easydk (`${EASYDK_DIR}` 代表easydk源码目录)：

1. 创建编译文件夹存储编译结果。

   ```bash
   cd ${EASYDK_DIR}
   mkdir build       # Create a directory to save the output.
   ```

2. 运行CMake配置编译选项，并生成编译指令，该命令将会在build目录下生成Makefile文件。

   ```bash
   cd build
   cmake ${EASYDK_DIR}  # Generate native build scripts.
   ```

   Cambricon EasyDK提供了一个CMakeLists.txt描述编译流程，您可以从 http://www.cmake.org/ 免费下载和使用cmake。

   | cmake 选项         | 范围            | 默认值  | 描述                      |
   | ------------------ | --------------- | ------- | ------------------------  |
   | MLU                |                 | MLU270  | 指定编译MLU平台           |
   | RELEASE            | ON / OFF        | ON      | 编译模式release / debug   |
   | BUILD_SAMPLES      | ON / OFF        | OFF     | 编译samples               |
   | BUILD_TESTS        | ON / OFF        | OFF     | 编译tests                 |
   | WITH_CODEC         | ON / OFF        | ON      | 编译EasyCodec             |
   | WITH_INFER         | ON / OFF        | ON      | 编译EasyInfer             |
   | WITH_TRACKER       | ON / OFF        | ON      | 编译EasyTracker           |
   | WITH_BANG          | ON / OFF        | ON      | 编译EasyBang              |
   | WITH_INFER_SERVER  | ON / OFF        | ON      | 编译infer-server          |
   | WITH_CURL          | ON / OFF        | ON      | 依赖libcurl               |
   | WITH_TURBOJPEG     | ON / OFF        | ON      | 编译turbo-jpeg            |
   | ENABLE_KCF         | ON / OFF        | OFF     | Easytrack支持KCF          |
   | SANITIZE_MEMORY    | ON / OFF        | OFF     | 检查内存                  |
   | SANITIZE_ADDRESS   | ON / OFF        | OFF     | 检查地址                  |
   | SANITIZE_THREAD    | ON / OFF        | OFF     | 检查多线程                |
   | SANITIZE_UNDEFINED | ON / OFF        | OFF     | 检查未定义行为            |

   > MLU平台支持： MLU270, MLU220, MLU220EDGE。（MLU220EDGE需要交叉编译）

   示例:

   ```bash
   cd build
   # build with samples and tests
   cmake ${EASYDK_DIR}      \
        -DBUILD_SAMPLES=ON  \
        -DBUILD_TESTS=ON
   ```

3. 运行编译指令。

   ```bash
   make
   ```

   编译后的库文件存放在 `${EASYDK_DIR}/lib` ，头文件在 `${EASYDK_DIR/include}` 

