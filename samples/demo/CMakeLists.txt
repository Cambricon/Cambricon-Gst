# ---[ set binary file path to be build in
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} DEMO_SRCS)
add_executable(demo ${DEMO_SRCS})
target_link_libraries(demo ${LINK_LIBRARIES})

