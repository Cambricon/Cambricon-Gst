cmake_minimum_required(VERSION 2.8.7)
if(POLICY CMP0046)
  cmake_policy(SET CMP0046 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

# ---[ GStreamer-Cambricon-SDK project
message("---------- Build Cambricon-Gst ----------")
project(Cambricon-Gst C CXX)

option(RELEASE "Build type" ON)
option(CODE_COVERAGE_TEST "Build code coverage test" OFF)

option(BUILD_TEST "build unit test" ON)
option(BUILD_SAMPLES "build samples" ON)

option(WITH_DECODE "build cndecode" ON)
option(WITH_CONVERT "build cnconvert" ON)
option(WITH_ENCODE "build cnencode" ON)

if (NOT (WITH_DECODE OR WITH_ENCODE OR WITH_CONVERT))
  message(FATAL_ERROR "All the modules are set to not build!")
endif()

#######################################################################
# -- Build Flags

if (RELEASE)
  set(CMAKE_BUILD_TYPE RELEASE)
else()
  set(CMAKE_BUILD_TYPE DEBUG)
endif()

set(CMAKE_CXX_FLAGS "-fPIC -Wall -Werror -std=c++11")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O3")

if(USE_libstdcpp)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libstdc++")
  message("-- Warning: forcing libstdc++ (controlled by USE_libstdcpp option in cmake)")
endif()

if(CODE_COVERAGE_TEST)
  message("build with code coverage test")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
endif()

######################### 3rdparty ########################################

# ---[ gstreamer
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER-VIDEO REQUIRED gstreamer-video-1.0)

########################## neuware ######################################

# neuware path
if(NOT DEFINED ENV{NEUWARE_HOME})
  set(ENV{NEUWARE_HOME} /usr/local/neuware)
endif()

# ---[ Includes
include_directories(
)

# ---[ cnrt
set(CNRT_LIB      "$ENV{NEUWARE_HOME}/lib64/libcnrt.so")

# ---[ cncodec
set(CNCODEC_LIB   "$ENV{NEUWARE_HOME}/lib64/libcncodec.so")

# ---[ cncv
set(CNCV_LIB      "$ENV{NEUWARE_HOME}/lib64/libcncv.so")

# ---[ edk
add_subdirectory(easydk)
set(EDK_LIB       "easydk")

set(LINK_LIBRARIES
  ${GLIB_LIBRARIES}
  ${GLIB_GOBJECT_LIBRARIES}
  ${GSTREAMER_LIBRARIES}
  ${GSTREAMER-VIDEO_LIBRARIES}
  ${CNRT_LIB}
  ${CNCODEC_LIB}
  ${CNCV_LIB}
  ${EDK_LIB}
)

# ---[ collect source file
aux_source_directory(${PROJECT_SOURCE_DIR}/gst-libs/common common_src)

if (WITH_DECODE)
  message(STATUS "Build with cndecode")
  aux_source_directory(${PROJECT_SOURCE_DIR}/gst/decode decode_src)
  add_definitions(-DWITH_DECODE)
endif()

if (WITH_CONVERT)
  message(STATUS "Build with cnconvert")
  aux_source_directory(${PROJECT_SOURCE_DIR}/gst/convert cvt_src)
  add_definitions(-DWITH_CONVERT)
endif()

if (WITH_ENCODE)
  message(STATUS "Build with cnencode")
  aux_source_directory(${PROJECT_SOURCE_DIR}/gst/encode encode_src)
  add_definitions(-DWITH_ENCODE)
endif()

# ---[ build plugin library
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
set(name gstcnstream)
message("target :  ${name}")
add_library(${name} SHARED
            ${common_src}
            ${PROJECT_SOURCE_DIR}/gst/plugin_register.cpp
            ${decode_src}
            ${cvt_src}
            ${encode_src})
target_include_directories(${name} PRIVATE
                           ${PROJECT_SOURCE_DIR}/gst
                           ${PROJECT_SOURCE_DIR}/gst-libs
                           ${PROJECT_SOURCE_DIR}/easydk/include
                           ${GSTREAMER_INCLUDE_DIRS}
                           ${GSTREAMER_VIDEO_INCLUDE_DIRS}
                           $ENV{NEUWARE_HOME}/include)
target_link_libraries(${name} ${LINK_LIBRARIES})

if(BUILD_TEST)
  message("Build tests")
  enable_testing()
  add_subdirectory(tests)
endif()

if(BUILD_SAMPLES)
  message("Build samples")
  add_subdirectory(samples/demo)
endif()
