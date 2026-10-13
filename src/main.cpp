#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

#include "app/App.h"
#include "audio/AudioPlayer.h"
#include "audio/Wav.h"
#include "core/Http.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "tts/PiperEngine.h"
#include "ui/Dashboard.h"
#include "ui/Renderer.h"

namespace {

// Headless mode for scripting and verification:
//   voicetwitch --say "text" --model path/to/voice.onnx [--wav out.wav] [--rate 1.0]
int runSay(int argc, char** argv) {
  std::string text, model, wavOut;
  float rate = 1.0f;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--say" && i + 1 < argc) text = argv[++i];
    else if (a == "--model" && i + 1 < argc) model = argv[++i];
    else if (a == "--wav" && i + 1 < argc) wavOut = argv[++i];
    else if (a == "--rate" && i + 1 < argc) rate = std::stof(argv[++i]);
  }
  if (text.empty() || model.empty()) {
    std::fprintf(stderr, "usage: voicetwitch --say TEXT --model VOICE.onnx [--wav OUT.wav] [--rate R]\n");
    return 2;
  }
  vt::PiperEngine engine;
  std::string err;
  auto t0 = std::chrono::steady_clock::now();
  if (!engine.load(vt::paths::fromUtf8(model), err)) {
    std::fprintf(stderr, "load failed: %s\n", err.c_str());
    return 1;
  }
  auto t1 = std::chrono::steady_clock::now();
  std::vector<float> all;
  vt::SynthesisParams params;
  params.rate = rate;
  bool ok = engine.synthesize(text, params, [&](std::span<const float> c) {
    all.insert(all.end(), c.begin(), c.end());
    return true;
  }, err);
  auto t2 = std::chrono::steady_clock::now();
  if (!ok) {
    std::fprintf(stderr, "synthesis failed: %s\n", err.c_str());
    return 1;
  }
  const double audioSec = static_cast<double>(all.size()) / engine.sampleRate();
  const double synthMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
  std::printf("model load %.0f ms, synthesis %.0f ms for %.2f s of audio (RTF %.3f)\n",
              std::chrono::duration<double, std::milli>(t1 - t0).count(), synthMs, audioSec,
              audioSec > 0 ? synthMs / 1000.0 / audioSec : 0.0);
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS pmc{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    std::printf("peak working set %.0f MB\n", static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0));
  }
#endif
  if (!wavOut.empty()) {
    if (!vt::wav::writeMono16(vt::paths::fromUtf8(wavOut), all, engine.sampleRate())) {
      std::fprintf(stderr, "could not write %s\n", wavOut.c_str());
      return 1;
    }
    std::printf("wrote %s\n", wavOut.c_str());
  } else {
    vt::AudioPlayer player;
    if (!player.begin(engine.sampleRate(), err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    player.push(all);
    std::vector<float> tail(static_cast<size_t>(engine.sampleRate() / 5), 0.0f);
    player.push(tail);
    player.waitUntilDrained();
    player.end();
  }
  return 0;
}

void loadFonts(float scale) {
  ImGuiIO& io = ImGui::GetIO();
  const char* candidates[] = {
#if defined(_WIN32)
      "C:\\Windows\\Fonts\\segoeui.ttf",
#elif defined(__APPLE__)
      "/System/Library/Fonts/SFNS.ttf",
      "/System/Library/Fonts/Helvetica.ttc",
#else
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
#endif
  };
  std::error_code ec;
  for (const char* path : candidates) {
    if (std::filesystem::exists(path, ec) && io.Fonts->AddFontFromFileTTF(path, 17.0f * scale)) return;
  }
  ImFontConfig cfg;
  cfg.SizePixels = 13.0f * scale;
  io.Fonts->AddFontDefault(&cfg);
}

void glfwErrorCallback(int code, const char* desc) { vt::log::error("GLFW error {}: {}", code, desc); }

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version") == 0) {
      std::printf("VoiceTwitch %s\n", VT_VERSION);
      return 0;
    }
    if (std::strcmp(argv[i], "--say") == 0) {
      vt::http::globalInit();
      int rc = runSay(argc, argv);
      vt::http::globalCleanup();
      return rc;
    }
  }

  vt::http::globalInit();
  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwInit()) {
    std::fprintf(stderr, "Failed to initialize GLFW\n");
    return 1;
  }

  vt::ui::Renderer::setWindowHints();

  float scale = 1.0f;
  if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
    float sx = 1, sy = 1;
    glfwGetMonitorContentScale(mon, &sx, &sy);
    scale = std::max(1.0f, sx);
  }
#if defined(__APPLE__)
  // macOS scales the framebuffer itself; keep logical sizes.
  const float windowScale = 1.0f;
#else
  const float windowScale = scale;
#endif

  // Default size, clamped to 90% of the monitor work area so the window
  // (including the status bar) never ends up under the taskbar/dock.
  int winW = static_cast<int>(1180 * windowScale);
  int winH = static_cast<int>(760 * windowScale);
  int areaX = 0, areaY = 0, areaW = 0, areaH = 0;
  if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
    glfwGetMonitorWorkarea(mon, &areaX, &areaY, &areaW, &areaH);
    if (areaW > 0 && areaH > 0) {
      winW = std::min(winW, areaW * 9 / 10);
      winH = std::min(winH, areaH * 9 / 10);
    }
  }
  GLFWwindow* window = glfwCreateWindow(winW, winH, "VoiceTwitch", nullptr, nullptr);
  if (window && areaW > 0 && areaH > 0) {
    glfwSetWindowPos(window, areaX + (areaW - winW) / 2, areaY + (areaH - winH) / 2);
  }
  if (!window) {
    std::fprintf(stderr, "Failed to create window\n");
    glfwTerminate();
    return 1;
  }
  glfwSetWindowSizeLimits(window, static_cast<int>(820 * windowScale), static_cast<int>(520 * windowScale),
                          GLFW_DONT_CARE, GLFW_DONT_CARE);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // fixed layout, nothing to persist
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  vt::ui::applyTheme(windowScale);
  loadFonts(windowScale);
  vt::ui::Renderer renderer;
  if (!renderer.init(window)) {
    std::fprintf(stderr, "Failed to initialize the renderer\n");
    return 1;
  }

  vt::App app;
  app.setWake([] { glfwPostEmptyEvent(); });
  app.init();

  // Event-driven loop: sleep until input or a background change, so idle CPU
  // stays near zero while connected to chat.
  int extraFrames = 2;
  while (!glfwWindowShouldClose(window)) {
    // 30 fps while something animates or a widget is being edited; otherwise
    // wake once a second at most (background threads post events on change).
    const bool uiAnimating = vt::ui::animationActive();  // hover fades, page transitions, spinners
    const bool animating = app.wantsFastRefresh() || ImGui::IsAnyItemActive() || io.WantTextInput;
    const double timeout = extraFrames > 0 ? 0.0 : (uiAnimating ? 1.0 / 60.0 : (animating ? 1.0 / 30.0 : 1.0));
    auto before = std::chrono::steady_clock::now();
    if (timeout > 0.0) {
      glfwWaitEventsTimeout(timeout);
    } else {
      glfwPollEvents();
    }
    const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
    if (extraFrames > 0) {
      --extraFrames;
    } else if (waited < timeout * 0.9) {
      // Woken by an event: render a couple of extra frames so ImGui settles
      // (popups, hover states, text cursors).
      extraFrames = 2;
    }

    app.tick();

    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) continue;

    renderer.newFrame();
    ImGui::NewFrame();
    vt::ui::drawDashboard(app);
    ImGui::Render();

    renderer.render(0.09f, 0.09f, 0.10f);
  }

  app.shutdown();
  renderer.shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  vt::http::globalCleanup();
  return 0;
}
