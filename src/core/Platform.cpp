#include "core/Platform.h"

#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include "core/StringUtil.h"
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace vt::platform {

namespace {

#ifndef _WIN32
bool spawnDetached(const char* program, const std::string& arg) {
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    // Double fork so we never leave a zombie behind.
    if (fork() == 0) {
      execlp(program, program, arg.c_str(), static_cast<char*>(nullptr));
    }
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return true;
}
#endif

}  // namespace

bool openUrl(const std::string& url) {
#ifdef _WIN32
  auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  return r > 32;
#elif defined(__APPLE__)
  return spawnDetached("open", url);
#else
  return spawnDetached("xdg-open", url);
#endif
}

bool openFolder(const std::filesystem::path& dir) {
#ifdef _WIN32
  auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  return r > 32;
#else
  return openUrl(dir.string());
#endif
}

bool writeFileAtomic(const std::filesystem::path& path, const std::string& contents) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  auto tmp = path;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!f) return false;
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    // rename over an existing file can fail on some Windows setups; fall back.
    std::filesystem::remove(path, ec);
    std::filesystem::rename(tmp, path, ec);
  }
  return !ec;
}

bool readFile(const std::filesystem::path& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

}  // namespace vt::platform
