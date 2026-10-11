#pragma once

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace vt {

enum class LogLevel { Debug, Info, Warn, Error };

struct LogEntry {
  uint64_t seq = 0;
  LogLevel level = LogLevel::Info;
  std::string time;  // HH:MM:SS
  std::string text;
};

namespace log {

// Optional: also append every line to a file.
void setFile(const std::filesystem::path& path);
void write(LogLevel level, std::string text);
// Returns entries with seq > afterSeq (up to the ring-buffer capacity).
std::vector<LogEntry> since(uint64_t afterSeq);

template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
  write(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
  write(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
  write(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
  write(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace log
}  // namespace vt
