#include "core/Log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>

namespace vt::log {
namespace {

constexpr size_t kCapacity = 1000;

struct State {
  std::mutex mutex;
  std::deque<LogEntry> entries;
  uint64_t nextSeq = 1;
  std::ofstream file;
};

State& state() {
  static State s;
  return s;
}

const char* levelName(LogLevel l) {
  switch (l) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
  }
  return "?";
}

std::string nowHms() {
  auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

}  // namespace

void setFile(const std::filesystem::path& path) {
  auto& s = state();
  std::lock_guard lock(s.mutex);
  s.file.open(path, std::ios::out | std::ios::trunc);
}

void write(LogLevel level, std::string text) {
  auto& s = state();
  std::lock_guard lock(s.mutex);
  LogEntry e;
  e.seq = s.nextSeq++;
  e.level = level;
  e.time = nowHms();
  e.text = std::move(text);
  if (s.file.is_open()) {
    s.file << e.time << " [" << levelName(level) << "] " << e.text << '\n';
    s.file.flush();
  }
#ifndef NDEBUG
  std::fprintf(stderr, "%s [%s] %s\n", e.time.c_str(), levelName(level), e.text.c_str());
#endif
  s.entries.push_back(std::move(e));
  if (s.entries.size() > kCapacity) s.entries.pop_front();
}

std::vector<LogEntry> since(uint64_t afterSeq) {
  auto& s = state();
  std::lock_guard lock(s.mutex);
  std::vector<LogEntry> out;
  for (const auto& e : s.entries) {
    if (e.seq > afterSeq) out.push_back(e);
  }
  return out;
}

}  // namespace vt::log
