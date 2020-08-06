cmake_minimum_required(VERSION 2.8.7)
if(POLICY CMP0046)
  cmake_policy(SET CMP0046 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

# ---[ easydk project
project(easydk C CXX)

#######################################################################
# -- Build Flags

option(RELEASE "Build type" ON)

option(WITH_CODEC "Build toolkit codec" ON)
option(WITH_INFER "Build toolkit infer" ON)
option(WITH_TRACKER "Build toolkit tracker" ON)
option(WITH_BANG "Build Bang operator" ON)
option(ENABLE_KCF "Build with KCF track" OFF)
option(WITH_CNPLUGIN "Build CNPlugin operator" OFF)

if (RELEASE)
  set(CMAKE_BUILD_TYPE RELEASE)
else()
  set(CMAKE_BUILD_TYPE DEBUG)
endif()

set(CMAKE_CXX_FLAGS "-fPIC -Wall -Werror -std=c++11")
set(CMAKE_CXX_FLAGS_DEBUG "-DDEBUG -g")
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3")

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

message("----------------------------- BUILD ${CMAKE_BUILD_TYPE} -----------------------------")

if(USE_libstdcpp)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libstdc++")
  message(WARNING "Warning: forcing libstdc++ (controlled by USE_libstdcpp option in cmake)")
endif()

#######################################################################

# neuware include/libs
if(NOT DEFINED ENV{NEUWARE_HOME})
  set(ENV{NEUWARE_HOME} /usr/local/neuware)
endif()

set(MLU_PLATFORM MLU270)
#cp neuware/.h or .so for easydk
execute_process(COMMAND mkdir -p ${PROJECT_SOURCE_DIR}/mlu/${MLU_PLATFORM}/include)
execute_process(COMMAND mkdir -p ${PROJECT_SOURCE_DIR}/mlu/${MLU_PLATFORM}/libs/${CMAKE_SYSTEM_PROCESSOR})
execute_process(COMMAND sh ${PROJECT_SOURCE_DIR}/tools/copy_neuware_mlu270.sh $ENV{NEUWARE_HOME} ${PROJECT_SOURCE_DIR}/mlu/${MLU_PLATFORM}/include  ${PROJECT_SOURCE_DIR}/mlu/${MLU_PLATFORM}/libs/${CMAKE_SYSTEM_PROCESSOR} ${WITH_CNPLUGIN})
message(STATUS "MLU270 neuware files link DONE")

set(MLU_LIBS_PATH ${PROJECT_SOURCE_DIR}/mlu/${MLU_PLATFORM}/libs/${CMAKE_SYSTEM_PROCESSOR})

# ---[ cnrt
list(APPEND CNRT_LIBS "${MLU_LIBS_PATH}/libcnrt.so")

# ---[ cncodec & kcf
if(WITH_CODEC)
  list(APPEND CNCODEC_LIBS "${MLU_LIBS_PATH}/libcncodec.so")
endif()
if(WITH_TRACKER)
  list(APPEND KCF_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/src/easytrack/kcf/libkcf_mlu270.a")
endif()

# ---[ Google-glog
include("${PROJECT_SOURCE_DIR}/cmake/FindGlog.cmake")
# list(APPEND 3RDPARTY_INCLUDE_DIRS ${GLOG_INCLUDE_DIRS})
list(APPEND GlogLib ${GLOG_LIBRARIES})

if(WITH_CNPLUGIN)
  # ---[ cnml
  list(APPEND CNML_LIBS "${MLU_LIBS_PATH}/libcnml.so")
  # ---[ cnplugin
  list(APPEND CNPLUGIN_LIBS "${MLU_LIBS_PATH}/libcnplugin.so")
endif()

# ---[ Includes
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include
                    ${PROJECT_SOURCE_DIR}/mlu/${MLU_PLATFORM}/include)


# ---[ Sources
file(GLOB cxxutil_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/cxxutil/*.cpp)
file(GLOB device_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/device/*.cpp)

if(WITH_INFER)
  message(STATUS "Build with EasyInfer")
  file(GLOB infer_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easyinfer/*.cpp)
endif()

if(WITH_CODEC)
  message(STATUS "Build with EasyCodec")
  file(GLOB codec_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easycodec/*.cpp)
endif()

if(WITH_TRACKER)
  message(STATUS "Build with EasyTrack")
  file(GLOB_RECURSE track_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easytrack/*.cpp)
  if(ENABLE_KCF)
    add_definitions(-DENABLE_KCF)
  endif()
endif()

if(WITH_BANG)
  message(STATUS "Build with EasyBang")
  file(GLOB_RECURSE bang_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easybang/resize_and_convert/kernel_*.o ${CMAKE_CURRENT_SOURCE_DIR}/src/easybang/resize_and_convert/*.cpp)
endif()

if(WITH_CNPLUGIN)
  file(GLOB_RECURSE plugin_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easyplugin/*.cpp)
endif()

message(STATUS "Target : easydk")
add_library(easydk SHARED ${cxxutil_srcs} ${device_srcs} ${infer_srcs} ${codec_srcs} ${bang_srcs}
                                    ${track_srcs} ${plugin_srcs})

target_link_libraries(easydk ${CNRT_LIBS} ${CNCODEC_LIBS} ${CNML_LIBS} ${CNPLUGIN_LIBS} ${KCF_LIBS} ${GlogLib})
install(TARGETS easydk LIBRARY DESTINATION lib)

