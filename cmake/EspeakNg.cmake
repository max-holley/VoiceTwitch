# Builds espeak-ng (Piper's phonemizer front end) from source as a static
# library, plus its compiled espeak-ng-data directory.
#
# espeak-ng has no vcpkg port, so it is built with ExternalProject. The pinned
# commit matches what upstream Piper (OHF-Voice/piper1-gpl) uses, which provides
# espeak_TextToPhonemesWithTerminator().
#
# NOTE: espeak-ng is GPL-3.0-or-later. Linking it makes the VoiceTwitch binary
# subject to the GPL as well.

include(ExternalProject)

set(VT_ESPEAKNG_GIT_TAG "212928b394a96e8fd2096616bfd54e17845c48f6" CACHE STRING "espeak-ng commit")

set(ESPEAKNG_PREFIX "${CMAKE_BINARY_DIR}/_deps/espeak_ng")
set(ESPEAKNG_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/espeak_ng-install")
set(ESPEAKNG_DATA_DIR "${ESPEAKNG_INSTALL_DIR}/share/espeak-ng-data")
set(_espeak_src "${ESPEAKNG_PREFIX}/src/espeak_ng_external")
set(_ucd_dir "${ESPEAKNG_PREFIX}/src/espeak_ng_external-build/src/ucd-tools")

if(MSVC)
  set(ESPEAKNG_STATIC_LIB "${ESPEAKNG_INSTALL_DIR}/lib/espeak-ng.lib")
  set(UCD_STATIC_LIB "${_ucd_dir}/ucd.lib")
else()
  set(ESPEAKNG_STATIC_LIB "${ESPEAKNG_INSTALL_DIR}/lib/libespeak-ng.a")
  set(UCD_STATIC_LIB "${_ucd_dir}/libucd.a")
endif()

if(CMAKE_BUILD_TYPE)
  set(_espeak_build_type "${CMAKE_BUILD_TYPE}")
else()
  set(_espeak_build_type "Release")
endif()

set(_espeak_cmake_args
  -DCMAKE_INSTALL_PREFIX=${ESPEAKNG_INSTALL_DIR}
  -DCMAKE_BUILD_TYPE=${_espeak_build_type}
  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
  -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
  -DBUILD_SHARED_LIBS:BOOL=OFF
  -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
  -DUSE_ASYNC:BOOL=OFF
  -DUSE_MBROLA:BOOL=OFF
  -DUSE_LIBSONIC:BOOL=OFF
  -DUSE_LIBPCAUDIO:BOOL=OFF
  -DUSE_KLATT:BOOL=OFF
  -DUSE_SPEECHPLAYER:BOOL=OFF
  -DEXTRA_cmn:BOOL=ON
  -DEXTRA_ru:BOOL=ON
  "-DCMAKE_C_FLAGS=-D_FILE_OFFSET_BITS=64 -I${_espeak_src}/src/ucd-tools/src/include"
  "-DCMAKE_CXX_FLAGS=-D_FILE_OFFSET_BITS=64 -I${_espeak_src}/src/ucd-tools/src/include")

if(MSVC)
  # Match the runtime used by the main project (/MD or /MDd).
  if(_espeak_build_type STREQUAL "Debug")
    set(_espeak_crt "MultiThreadedDebugDLL")
  else()
    set(_espeak_crt "MultiThreadedDLL")
  endif()
  list(APPEND _espeak_cmake_args
    -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=${_espeak_crt}")
endif()
if(APPLE)
  list(APPEND _espeak_cmake_args
    "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

ExternalProject_Add(espeak_ng_external
  GIT_REPOSITORY https://github.com/espeak-ng/espeak-ng.git
  GIT_TAG ${VT_ESPEAKNG_GIT_TAG}
  GIT_SHALLOW FALSE
  PREFIX ${ESPEAKNG_PREFIX}
  CMAKE_ARGS ${_espeak_cmake_args}
  BUILD_BYPRODUCTS ${ESPEAKNG_STATIC_LIB} ${UCD_STATIC_LIB}
  UPDATE_DISCONNECTED TRUE)

file(MAKE_DIRECTORY "${ESPEAKNG_INSTALL_DIR}/include")

add_library(espeak_ng::espeak_ng STATIC IMPORTED GLOBAL)
set_target_properties(espeak_ng::espeak_ng PROPERTIES
  IMPORTED_LOCATION "${ESPEAKNG_STATIC_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${ESPEAKNG_INSTALL_DIR}/include"
  INTERFACE_LINK_LIBRARIES "${UCD_STATIC_LIB}")
add_dependencies(espeak_ng::espeak_ng espeak_ng_external)
