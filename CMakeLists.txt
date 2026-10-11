cmake_minimum_required(VERSION 3.25)

project(VoiceTwitch
  VERSION 0.1.0
  DESCRIPTION "Local TTS for Twitch chat"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VT_BUILD_TESTS "Build unit tests" ON)
set(VT_TWITCH_CLIENT_ID "" CACHE STRING "Default Twitch application Client ID baked into the build (optional; can be set in the app)")

if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
  add_compile_options(-Wall -Wextra)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---- third-party ------------------------------------------------------------

find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
if(NOT WIN32)
  find_package(OpenGL REQUIRED)
endif()
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h PATH_SUFFIXES miniaudio REQUIRED)
find_package(Threads REQUIRED)

include(OnnxRuntime)
include(EspeakNg)

if(UNIX AND NOT APPLE)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)
  endif()
endif()

# ---- core library (everything except the GUI) ------------------------------

add_library(voicetwitch_core STATIC
  src/core/Config.cpp
  src/core/Http.cpp
  src/core/Log.cpp
  src/core/Paths.cpp
  src/core/Platform.cpp
  src/core/Process.cpp
  src/core/SecureStore.cpp
  src/core/StringUtil.cpp
  src/audio/AudioPlayer.cpp
  src/audio/AudioProcessing.cpp
  src/audio/AudioRecorder.cpp
  src/audio/MiniaudioImpl.cpp
  src/audio/Wav.cpp
  src/tts/PhonemeIds.cpp
  src/tts/Phonemizer.cpp
  src/tts/PiperEngine.cpp
  src/tts/VoiceConfig.cpp
  src/pipeline/BadWordFilter.cpp
  src/pipeline/SpeechPipeline.cpp
  src/twitch/IrcParser.cpp
  src/twitch/LoopbackServer.cpp
  src/twitch/TwitchAuth.cpp
  src/twitch/TwitchIrcClient.cpp
  src/models/ModelManager.cpp
  src/models/VoiceCatalog.cpp
  src/cloning/CloneProject.cpp
  src/cloning/RecordingScript.cpp
  src/cloning/TrainingRunner.cpp)

add_dependencies(voicetwitch_core espeak_ng_external)
target_include_directories(voicetwitch_core PUBLIC src ${MINIAUDIO_INCLUDE_DIR})
target_compile_definitions(voicetwitch_core PUBLIC
  VT_VERSION="${PROJECT_VERSION}"
  VT_DEFAULT_CLIENT_ID="${VT_TWITCH_CLIENT_ID}")
target_link_libraries(voicetwitch_core PUBLIC
  nlohmann_json::nlohmann_json
  CURL::libcurl
  onnxruntime::onnxruntime
  espeak_ng::espeak_ng
  Threads::Threads)

if(WIN32)
  target_link_libraries(voicetwitch_core PUBLIC ws2_32 advapi32 shell32 ole32 psapi)
elseif(APPLE)
  target_link_libraries(voicetwitch_core PUBLIC
    "-framework Security" "-framework CoreFoundation"
    "-framework CoreAudio" "-framework AudioToolbox")
else()
  target_link_libraries(voicetwitch_core PUBLIC ${CMAKE_DL_LIBS} m)
  if(LIBSECRET_FOUND)
    target_link_libraries(voicetwitch_core PUBLIC PkgConfig::LIBSECRET)
    target_compile_definitions(voicetwitch_core PUBLIC VT_HAVE_LIBSECRET=1)
  endif()
endif()

# ---- application -----------------------------------------------------------

add_executable(voicetwitch
  src/main.cpp
  src/app/App.cpp
  src/ui/Dashboard.cpp
  src/ui/Renderer.cpp
  src/ui/CloningView.cpp
  src/ui/FiltersView.cpp
  src/ui/SettingsView.cpp
  src/ui/TwitchView.cpp
  src/ui/VoiceModelsView.cpp)

target_link_libraries(voicetwitch PRIVATE voicetwitch_core imgui::imgui glfw)
if(WIN32)
  target_link_libraries(voicetwitch PRIVATE d3d11 dxgi d3dcompiler)
else()
  target_link_libraries(voicetwitch PRIVATE OpenGL::GL)
endif()

if(WIN32)
  set_target_properties(voicetwitch PROPERTIES WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
  target_link_options(voicetwitch PRIVATE $<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>)
elseif(APPLE)
  set_target_properties(voicetwitch PROPERTIES INSTALL_RPATH "@executable_path")
else()
  set_target_properties(voicetwitch PROPERTIES INSTALL_RPATH "$ORIGIN" BUILD_RPATH "$ORIGIN")
endif()

# Runtime files that must sit next to the executable.
add_custom_command(TARGET voicetwitch POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_RUNTIME_FILES} $<TARGET_FILE_DIR:voicetwitch>
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${ESPEAKNG_DATA_DIR} $<TARGET_FILE_DIR:voicetwitch>/espeak-ng-data
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/training $<TARGET_FILE_DIR:voicetwitch>/training
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:voicetwitch>/assets
  VERBATIM)

# ---- tests -----------------------------------------------------------------

if(VT_BUILD_TESTS)
  enable_testing()
  add_executable(voicetwitch_tests tests/TestMain.cpp)
  target_link_libraries(voicetwitch_tests PRIVATE voicetwitch_core)
  add_custom_command(TARGET voicetwitch_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_RUNTIME_FILES} $<TARGET_FILE_DIR:voicetwitch_tests>
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${ESPEAKNG_DATA_DIR} $<TARGET_FILE_DIR:voicetwitch_tests>/espeak-ng-data
    VERBATIM)
  add_test(NAME voicetwitch_tests COMMAND voicetwitch_tests)
endif()

# ---- install / package -----------------------------------------------------

install(TARGETS voicetwitch RUNTIME DESTINATION .)
install(FILES ${ONNXRUNTIME_RUNTIME_FILES} DESTINATION .)
install(DIRECTORY ${ESPEAKNG_DATA_DIR} DESTINATION .)
install(DIRECTORY training assets DESTINATION .)
install(FILES README.md LICENSE DESTINATION . OPTIONAL)

set(CPACK_PACKAGE_NAME "VoiceTwitch")
set(CPACK_PACKAGE_VENDOR "VoiceTwitch")
set(CPACK_GENERATOR "ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
