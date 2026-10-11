# Fetches the official prebuilt onnxruntime release for the current platform
# and exposes it as the imported target onnxruntime::onnxruntime.
#
# Building onnxruntime from source (e.g. via its vcpkg port) pulls in abseil,
# protobuf, onnx, eigen... and takes a very long time. Piper only needs the CPU
# execution provider, which the prebuilt archives ship as a single shared lib.
#
# Override with -DONNXRUNTIME_DIR=<extracted release dir> to use a local copy.

set(VT_ONNXRUNTIME_VERSION "1.22.0" CACHE STRING "onnxruntime prebuilt release version")

if(NOT ONNXRUNTIME_DIR)
  if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
      set(_ort_prefix "onnxruntime-win-arm64-${VT_ONNXRUNTIME_VERSION}")
    else()
      set(_ort_prefix "onnxruntime-win-x64-${VT_ONNXRUNTIME_VERSION}")
    endif()
    set(_ort_ext "zip")
  elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" OR (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64"))
      set(_ort_prefix "onnxruntime-osx-x86_64-${VT_ONNXRUNTIME_VERSION}")
    else()
      set(_ort_prefix "onnxruntime-osx-arm64-${VT_ONNXRUNTIME_VERSION}")
    endif()
    set(_ort_ext "tgz")
  else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
      set(_ort_prefix "onnxruntime-linux-aarch64-${VT_ONNXRUNTIME_VERSION}")
    else()
      set(_ort_prefix "onnxruntime-linux-x64-${VT_ONNXRUNTIME_VERSION}")
    endif()
    set(_ort_ext "tgz")
  endif()

  set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${VT_ONNXRUNTIME_VERSION}/${_ort_prefix}.${_ort_ext}")
  set(_ort_root "${CMAKE_BINARY_DIR}/_deps")
  set(ONNXRUNTIME_DIR "${_ort_root}/${_ort_prefix}")

  if(NOT EXISTS "${ONNXRUNTIME_DIR}/include")
    set(_ort_archive "${_ort_root}/${_ort_prefix}.${_ort_ext}")
    if(NOT EXISTS "${_ort_archive}")
      message(STATUS "Downloading ${_ort_url}")
      file(DOWNLOAD "${_ort_url}" "${_ort_archive}" SHOW_PROGRESS STATUS _ort_status TLS_VERIFY ON)
      list(GET _ort_status 0 _ort_code)
      if(NOT _ort_code EQUAL 0)
        file(REMOVE "${_ort_archive}")
        message(FATAL_ERROR "Failed to download onnxruntime: ${_ort_status}")
      endif()
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_ort_archive}" DESTINATION "${_ort_root}")
  endif()
endif()

if(WIN32)
  find_library(ONNXRUNTIME_LIB NAMES onnxruntime PATHS "${ONNXRUNTIME_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
  set(ONNXRUNTIME_RUNTIME_FILES "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll")
  if(EXISTS "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll")
    list(APPEND ONNXRUNTIME_RUNTIME_FILES "${ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll")
  endif()
  add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_IMPLIB "${ONNXRUNTIME_LIB}"
    IMPORTED_LOCATION "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_DIR}/include")
else()
  find_library(ONNXRUNTIME_LIB NAMES onnxruntime PATHS "${ONNXRUNTIME_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
  file(GLOB ONNXRUNTIME_RUNTIME_FILES "${ONNXRUNTIME_DIR}/lib/libonnxruntime*${CMAKE_SHARED_LIBRARY_SUFFIX}*")
  add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ONNXRUNTIME_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_DIR}/include")
endif()

message(STATUS "onnxruntime: ${ONNXRUNTIME_DIR}")
