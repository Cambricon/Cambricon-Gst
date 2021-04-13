EN|[CN](README_cn.md)

# Cambricon<sup>®</sup> Easy Development Kit

Cambricon<sup>®</sup> Easy Development Kit is a toolkit, which aim at helping with developing software on Cambricon MLU270/MLU220 platform.

Toolkit provides following modules:
- Device: MLU device context operation
- EasyCodec: easy decode and encode on MLU
- EasyInfer: easy inference accelerator on MLU
- EasyTrack: easy track, including feature match track and kcf track
- EasyBang: easy Bang operator

![modules](docs/source/images/software_stack.png)

## **Cambricon Dependencies** ##

You can find the cambricon dependencies, including headers and libraries, in the neuware home (installed in `/usr/local/neuware` by default).

### Quick Start ###

This section introduces how to quickly build instructions on EasyDK and how to develop your own applications based on easydk.

#### **Required environments** ####

Before building instructions, you need to install the following software:

- cmake  2.8.7+
- GCC    4.8.5+
- GLog   0.3.4

samples & tests dependencies:

- OpenCV 2.4.9+
- GFlags 2.1.2
- FFmpeg 2.8 3.4 4.2

#### Ubuntu or Debian ####

If you are using Ubuntu or Debian, run the following commands:

   ```bash
   sudo apt install libgoogle-glog-dev
   # samples dependencies
   sudo apt install libgflags-dev libopencv-dev
   ```

#### Centos ####

If you are using Centos, run the following commands:

   ```bash
   sudo yum install glog
   # samples dependencies
   sudo yum install gflags opencv-devel
   ```

## Build Instructions Using CMake ##

After finished prerequiste, you can build instructions with the following steps:

1. Run the following command to create a directory for saving the output.

   ```bash
   mkdir build       # Create a directory to save the output.
   ```

   A Makefile will be generated in the build folder.

2. Run the following command to generate a script for building instructions.

   ```bash
   cd build
   cmake ${EASYDK_DIR}  # Generate native build scripts.
   ```

   Cambricon easydk provides a CMake script ([CMakeLists.txt](CMakeLists.txt)) to build instructions. You can download CMake for free from <http://www.cmake.org/>.

   `${EASYDK_DIR}` specifies the directory where easydk saves for.

   | cmake option       | range           | default | description              |
   | ------------------ | --------------- | ------- | ------------------------ |
   | MLU                |                 | MLU270  | specify the MLU platform |
   | BUILD_SAMPLES      | ON / OFF        | OFF     | build with samples       |
   | BUILD_TESTS        | ON / OFF        | OFF     | build with tests         |
   | RELEASE            | ON / OFF        | ON      | release / debug          |
   | WITH_CODEC         | ON / OFF        | ON      | build codec              |
   | WITH_INFER         | ON / OFF        | ON      | build infer              |
   | WITH_TRACKER       | ON / OFF        | ON      | build tracker            |
   | WITH_BANG          | ON / OFF        | ON      | build bang               |
   | WITH_INFER_SERVER  | ON / OFF        | ON      | build infer-server       |
   | WITH_CURL          | ON / OFF        | ON      | build with libcurl       |
   | WITH_TURBOJPEG     | ON / OFF        | ON      | build with turbo-jpeg    |
   | ENABLE_KCF         | ON / OFF        | OFF     | build with KCF track     |
   | SANITIZE_MEMORY    | ON / OFF        | OFF     | check memory             |
   | SANITIZE_ADDRESS   | ON / OFF        | OFF     | check address            |
   | SANITIZE_THREAD    | ON / OFF        | OFF     | check thread             |
   | SANITIZE_UNDEFINED | ON / OFF        | OFF     | check undefined behavior |

   > Supported MLU platform: MLU270, MLU220, MLU220EDGE. (MLU220EDGE need cross-compile)

   Example:

   ```bash
   cd build
   # build without samples and tests
   cmake ${EASYDK_DIR}      \
        -DBUILD_SAMPLES=ON  \
        -DBUILD_TESTS=ON
   ```

3. Run the following command to build instructions:

   ```bash
   make
   ```

