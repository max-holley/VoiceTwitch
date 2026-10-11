#include "core/Paths.h"

#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <unistd.h>
#include <climits>
#endif

namespace vt::paths {

#ifndef _WIN32
namespace {

fs::path homeDir() {
  if (const char* h = std::getenv("HOME")) return h;
  return fs::current_path();
}

}  // namespace
#endif

fs::path dataDir() {
  static const fs::path dir = [] {
    if (const char* o = std::getenv("VOICETWITCH_DATA_DIR"); o && *o) return fs::path(o);
#ifdef _WIN32
    PWSTR p = nullptr;
    fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
      base = p;
    } else {
      base = fs::current_path();
    }
    CoTaskMemFree(p);
    return base / "VoiceTwitch";
#elif defined(__APPLE__)
    return homeDir() / "Library" / "Application Support" / "VoiceTwitch";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return fs::path(x) / "VoiceTwitch";
    return homeDir() / ".local" / "share" / "VoiceTwitch";
#endif
  }();
  return dir;
}

fs::path modelsDir() { return dataDir() / "models"; }
fs::path clonedVoicesDir() { return dataDir() / "cloned-voices"; }
fs::path configFile() { return dataDir() / "config.json"; }
fs::path filtersFile() { return dataDir() / "filters.json"; }
fs::path trainingEnvDir() { return dataDir() / "training-env"; }

fs::path executableDir() {
  static const fs::path dir = [] {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
      std::error_code ec;
      auto p = fs::canonical(buf, ec);
      return (ec ? fs::path(buf) : p).parent_path();
    }
    return fs::current_path();
#else
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
#endif
  }();
  return dir;
}

fs::path espeakDataDir() { return executableDir() / "espeak-ng-data"; }
fs::path trainingScriptsDir() { return executableDir() / "training"; }
fs::path assetsDir() { return executableDir() / "assets"; }

std::string toUtf8(const fs::path& p) {
  auto u8 = p.generic_u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

fs::path fromUtf8(std::string_view s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

void ensureDataDirs() {
  std::error_code ec;
  fs::create_directories(modelsDir(), ec);
  fs::create_directories(clonedVoicesDir(), ec);
}

}  // namespace vt::paths
