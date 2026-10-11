#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace vt {

// Minimal cross-platform child process with merged stdout/stderr delivered
// line by line on a background thread. Used to run the out-of-process
// voice-cloning training script. The whole process tree is killed on kill()
// or destruction.
class Process {
 public:
  using LineFn = std::function<void(const std::string&)>;
  using ExitFn = std::function<void(int)>;

  Process() = default;
  ~Process();
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;

  bool start(const std::filesystem::path& program, const std::vector<std::string>& args,
             const std::filesystem::path& workingDir, LineFn onLine, ExitFn onExit, std::string& error);
  bool running() const { return running_.load(); }
  void kill();
  // Blocks until the reader thread finishes. Returns the exit code (or -1).
  int wait();

 private:
  void readerLoop();

  std::atomic<bool> running_{false};
  std::thread reader_;
  LineFn onLine_;
  ExitFn onExit_;
  int exitCode_ = -1;
#ifdef _WIN32
  void* process_ = nullptr;
  void* job_ = nullptr;
  void* readPipe_ = nullptr;
#else
  int pid_ = -1;
  int readFd_ = -1;
#endif
};

}  // namespace vt
