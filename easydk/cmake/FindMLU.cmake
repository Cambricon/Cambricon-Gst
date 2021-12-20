# ==============================================
# Try to find Cambricon Neuware libraries:
# - cnrt
# - cndrv
# - ion (required on mlu220 edge)
# - cncodec
# - cncv
# - magicmind_runtime
#
# SET NEUWARE_INCLUDE_DIR with neuware include directory
# SET CNRT_LIBS with cnrt path and cndrv path
# SET CNCODEC_LIBS with cncodec path and ion(if has) path
# ==============================================

if(NEUWARE_HOME)
  get_filename_component(NEUWARE_HOME ${NEUWARE_HOME} ABSOLUTE)
  message(STATUS "NEUWARE_HOME: ${NEUWARE_HOME}")
elseif(DEFINED ENV{NEUWARE_HOME})
  get_filename_component(NEUWARE_HOME $ENV{NEUWARE_HOME} ABSOLUTE)
  message(STATUS "ENV{NEUWARE_HOME}: ${NEUWARE_HOME}")
else()
  set(NEUWARE_HOME "/usr/local/neuware")
  message(STATUS "Default NEUWARE_HOME: ${NEUWARE_HOME}")
endif()

if((NOT EXISTS ${NEUWARE_HOME}) OR (NOT EXISTS ${NEUWARE_HOME}/include) OR (NOT EXISTS ${NEUWARE_HOME}/lib64))
  message(FATAL_ERROR "NEUWARE_HOME: ${NEUWARE_HOME} not exists!")
else()
  set(NEUWARE_INCLUDE_DIR ${NEUWARE_HOME}/include)
endif()

# ---[ cnrt
find_library(CNRT_LIBS
             NAMES cnrt
             PATHS ${NEUWARE_HOME}/lib64
             NO_CMAKE_FIND_ROOT_PATH
             NO_CMAKE_PATH
             NO_DEFAULT_PATH
             NO_CMAKE_SYSTEM_PATH)
find_library(CNDRV_LIBS
             NAMES cndrv
             PATHS ${NEUWARE_HOME}/lib64
             NO_CMAKE_FIND_ROOT_PATH
             NO_CMAKE_PATH
             NO_DEFAULT_PATH
             NO_CMAKE_SYSTEM_PATH)

# ---[ cncodec
find_library(CNCODEC_LIBS
             NAMES cncodec
             PATHS ${NEUWARE_HOME}/lib64
             NO_CMAKE_FIND_ROOT_PATH
             NO_CMAKE_PATH
             NO_DEFAULT_PATH
             NO_CMAKE_SYSTEM_PATH)
find_library(ION_LIBS
             NAMES ion
             PATHS ${NEUWARE_HOME}/lib64
             NO_CMAKE_FIND_ROOT_PATH
             NO_CMAKE_PATH
             NO_DEFAULT_PATH
             NO_CMAKE_SYSTEM_PATH)
if(ION_LIBS)
  list(APPEND CNCODEC_LIBS ${ION_LIBS})
endif()

# ---[ cncv
find_library(CNCV_LIBS
             NAMES cncv
             PATHS ${NEUWARE_HOME}/lib64
             NO_CMAKE_FIND_ROOT_PATH
             NO_CMAKE_PATH
             NO_DEFAULT_PATH
             NO_CMAKE_SYSTEM_PATH)
if(CNCV_LIBS)
  message(STATUS "Found CNCV: ${CNCV_LIBS}")
endif()

# ---[ magicmind
find_library(MAGICMIND_RUNTIME_LIBS
             NAMES magicmind_runtime
             PATHS ${NEUWARE_HOME}/lib64
             NO_CMAKE_FIND_ROOT_PATH
             NO_CMAKE_PATH
             NO_DEFAULT_PATH
             NO_CMAKE_SYSTEM_PATH)

if(MAGICMIND_RUNTIME_LIBS)
  message(STATUS "Found MAGICMIND_RUNTIME: ${MAGICMIND_RUNTIME_LIBS}")
endif()
