cmake_minimum_required(VERSION 2.8.12)
if(POLICY CMP0046)
  cmake_policy(SET CMP0046 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

# ---[ easydk project
project(easydk C CXX)

set(EDK_VERSION_MAJOR 2)
set(EDK_VERSION_MINOR 6)
set(EDK_VERSION_PATCH 0)
set(EDK_VERSION "${EDK_VERSION_MAJOR}.${EDK_VERSION_MINOR}.${EDK_VERSION_PATCH}")

#######################################################################
# -- Build Flags

option(WITH_CODEC "Build easy codec" ON)
option(WITH_INFER "Build easy infer" ON)
option(WITH_TRACKER "Build easy tracker" ON)
option(WITH_BANG "Build easy Bang" ON)
option(ENABLE_KCF "Build with KCF track" OFF)
option(WITH_BACKWARD "Build with Backward" ON)
option(WITH_TURBOJPEG "Build Turbo Jpeg" OFF)

set(CMAKE_CXX_FLAGS "-fPIC -Wall -Werror -std=c++11 -D_REENTRANT")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3")
set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--no-as-needed")

set(CMAKE_INSTALL_DEFAULT_PREFIX "${PROJECT_SOURCE_DIR}/dist")
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_DEFAULT_PREFIX}" CACHE PATH
    "Directory into which to install ${CMAKE_PROJECT_NAME} (default: ${CMAKE_INSTALL_DEFAULT_PREFIX})"
    FORCE)
endif()
message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "!!!!! BUILD ${CMAKE_BUILD_TYPE} !!!!!")

#######################################################################

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# disable KCF
if (WITH_TRACKER AND ENABLE_KCF)
  message(WARNING "disable KCF since it's not supported on MLU220EDGE for now")
  set(ENABLE_KCF OFF)
endif()

find_package(MLU)

# set runtime path
set(CMAKE_BUILD_RPATH "$ORIGIN;${NEUWARE_HOME}/lib64;")
set(CMAKE_INSTALL_RPATH "$ORIGIN;${CMAKE_INSTALL_PREFIX}/lib;${NEUWARE_HOME}/lib64;")
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# ------------------- Sources -----------------------

configure_file(${PROJECT_SOURCE_DIR}/edk_config.h.in
               ${PROJECT_SOURCE_DIR}/include/edk_config.h)

file(GLOB cxxutil_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/cxxutil/*.cpp)
file(GLOB device_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/device/*.cpp)
install(DIRECTORY include/cxxutil DESTINATION include)
install(DIRECTORY include/device DESTINATION include)

if(WITH_INFER)
  message(STATUS "Build with EasyInfer")
  file(GLOB infer_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easyinfer/*.cpp)
  install(DIRECTORY include/easyinfer DESTINATION include)
endif()

if(WITH_CODEC)
  message(STATUS "Build with EasyCodec")
  file(GLOB codec_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easycodec/*.cpp)
  install(DIRECTORY include/easycodec DESTINATION include)
endif()

if(WITH_TRACKER)
  message(STATUS "Build with EasyTrack")
  file(GLOB_RECURSE track_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easytrack/*.cpp)
  if(ENABLE_KCF)
    set(KCF_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/src/easytrack/kcf/libkcf_mlu270.a")
    list(APPEND EDK_DEFINITIONS "ENABLE_KCF")
  endif()
  install(DIRECTORY include/easytrack DESTINATION include)
endif()

if(WITH_BANG)
  message(STATUS "Build with EasyBang")
  if ((CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64) OR (CMAKE_SYSTEM_PROCESSOR STREQUAL aarch64))
    file(GLOB_RECURSE resize_and_convert_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easybang/resize_and_convert/kernel_${CMAKE_SYSTEM_PROCESSOR}.o ${CMAKE_CURRENT_SOURCE_DIR}/src/easybang/resize_and_convert/*.cpp)
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
      file(GLOB_RECURSE resize_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easybang/resize/kernel_x86_64_mlu270.o ${CMAKE_CURRENT_SOURCE_DIR}/src/easybang/resize/*.cpp)
    endif()
    install(DIRECTORY include/easybang DESTINATION include)
  else()
    message(FATAL_ERROR "error system processor !!!")
  endif()
endif()

message(STATUS "@@@@@@@@@@@ Target : easydk")
add_library(easydk SHARED
            ${cxxutil_srcs}
            ${device_srcs}
            ${infer_srcs}
            ${codec_srcs}
            ${resize_srcs}
            ${resize_and_convert_srcs}
            ${track_srcs})

target_include_directories(easydk PRIVATE
                           ${NEUWARE_INCLUDE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(easydk PUBLIC
                      ${CNRT_LIBS}
                      ${CNDRV_LIBS}
                      ${KCF_LIBS})

if(WITH_CODEC)
  target_link_libraries(easydk PUBLIC ${CNCODEC_LIBS})

  # turbojpeg and libyuv
  if(WITH_TURBOJPEG)
    message("------------------- Turbo-JPEG and libyuv -----------------------")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -fvisibility=hidden")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
    add_subdirectory(3rdparty/libjpeg-turbo)
    add_subdirectory(3rdparty/libyuv)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=default")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")
    message("------------------- Turbo-JPEG and libyuv END -----------------------")
  
    list(APPEND EDK_DEFINITIONS "ENABLE_TURBOJPEG")
    target_include_directories(easydk PRIVATE "${PROJECT_SOURCE_DIR}/3rdparty/libyuv/include"
                                              "${PROJECT_SOURCE_DIR}/3rdparty/libjpeg-turbo")
    target_link_libraries(easydk PRIVATE turbojpeg-static yuv)
  endif()
endif()

# backward
if (WITH_BACKWARD)
  include(${PROJECT_SOURCE_DIR}/3rdparty/backward/BackwardConfig.cmake)
  target_include_directories(easydk PRIVATE ${BACKWARD_INCLUDE_DIRS})
  target_link_libraries(easydk PUBLIC ${BACKWARD_LIBRARIES})
  list(APPEND EDK_DEFINITIONS ${BACKWARD_DEFINITIONS} HAVE_BACKWARD)
endif()

target_compile_definitions(easydk PRIVATE ${EDK_DEFINITIONS})

set_target_properties(easydk PROPERTIES VERSION ${EDK_VERSION})
set_target_properties(easydk PROPERTIES SOVERSION ${EDK_VERSION_MAJOR})

install(TARGETS easydk LIBRARY DESTINATION lib)

