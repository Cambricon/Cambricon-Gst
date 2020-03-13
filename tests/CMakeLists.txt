pkg_check_modules(GSTREAMER-CHECK REQUIRED gstreamer-check-1.0)

include_directories(
  ${GSTREAMER_INCLUDE_DIRS}
  ${GSTREAMER_VIDEO_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/../gst/
  ${CMAKE_CURRENT_SOURCE_DIR}/../gst-libs/
)

link_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/../lib
)

set(TEST_LINK_LIBRARIES
  ${GLIB_LIBRARIES}
  ${GLIB_GOBJECT_LIBRARIES}
  ${GSTREAMER_LIBRARIES}
  ${GSTREAMER-VIDEO_LIBRARIES}
  ${GSTREAMER-CHECK_LIBRARIES}
  gstapp-1.0
  ${EDK_LIBS}
  gstcnstream
)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src SRC_FILES)

set(name gstcnstream-test)
message("target :  ${name}")
add_executable(${name} ${SRC_FILES})
target_link_libraries(${name} ${TEST_LINK_LIBRARIES})
