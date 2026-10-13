# Embed the redistributable supplied with the active MSVC installation. An
# override supports build agents with the redistributable staged separately.
set(VT_VC_REDIST "" CACHE FILEPATH "Path to Microsoft's vc_redist.x64.exe")
if(NOT VT_VC_REDIST)
  get_filename_component(_vt_vc_root "${CMAKE_CXX_COMPILER}/../../../../../../.." ABSOLUTE)
  file(GLOB _vt_redist_candidates "${_vt_vc_root}/Redist/MSVC/*/vc_redist.x64.exe")
  list(SORT _vt_redist_candidates COMPARE NATURAL ORDER DESCENDING)
  find_file(VT_VC_REDIST_AUTO NAMES vc_redist.x64.exe
    HINTS "$ENV{VCToolsRedistDir}" NO_DEFAULT_PATH)
  if(VT_VC_REDIST_AUTO)
    set(VT_VC_REDIST "${VT_VC_REDIST_AUTO}")
  elseif(_vt_redist_candidates)
    list(GET _vt_redist_candidates 0 VT_VC_REDIST)
  endif()
endif()
if(NOT EXISTS "${VT_VC_REDIST}")
  message(FATAL_ERROR "Visual C++ redistributable not found. Set VT_VC_REDIST to Microsoft's vc_redist.x64.exe.")
endif()

# Read the bundled version rather than hard-coding a minimum that can become
# stale when the build machine's Visual Studio toolset is updated.
find_program(VT_POWERSHELL NAMES powershell pwsh REQUIRED)
execute_process(COMMAND "${VT_POWERSHELL}" -NoProfile -NonInteractive -ExecutionPolicy Bypass
  -File "${CMAKE_CURRENT_LIST_DIR}/ReadRedistVersion.ps1" -Path "${VT_VC_REDIST}"
  OUTPUT_VARIABLE VT_VC_REDIST_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _vt_redist_result ERROR_VARIABLE _vt_redist_error)
if(NOT _vt_redist_result EQUAL 0 OR NOT VT_VC_REDIST_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "Cannot validate Visual C++ redistributable: ${_vt_redist_error}")
endif()
message(STATUS "Bundling Visual C++ x64 runtime ${VT_VC_REDIST_VERSION}: ${VT_VC_REDIST}")
set(CPACK_NSIS_DEFINES "!include \"WordFunc.nsh\"\n!insertmacro VersionCompare")
set(CPACK_VERBATIM_VARIABLES ON)
file(TO_NATIVE_PATH "${VT_VC_REDIST}" VT_VC_REDIST_NSIS)
configure_file("${CMAKE_CURRENT_LIST_DIR}/VCRedist.nsh.in"
  "${CMAKE_CURRENT_BINARY_DIR}/VCRedist.nsh" @ONLY)
file(READ "${CMAKE_CURRENT_BINARY_DIR}/VCRedist.nsh" CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS)
