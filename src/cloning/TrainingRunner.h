#pragma once

#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Process.h"

namespace vt {

struct TrainingRequest {
  std::string projectName;
  std::filesystem::path projectDir;
  std::filesystem::path outputModel;  // <project>/<name>.onnx
  std::string pythonPath;             // empty = auto-detect
  std::string espeakVoice = "en-us";
  std::string baseCheckpointUrl;
  int epochs = 300;
  int batchSize = 16;
  int sampleRate = 22050;
};

// Runs the out-of-process fine-tuning workflow (Piper's training tooling is
// Python/PyTorch). Inference stays native; this only runs on demand.
//
//   1. use the bundled Windows runtime, or find 64-bit CPython 3.11-3.13
//   2. create <data>/training-env venv + install piper1-gpl[train] (first run)
//   3. training/train_voice.py train  -> fine-tune from a base checkpoint
//   4. export to ONNX + config next to the project
//
// The script reports progress as lines prefixed with "@@VT " + JSON.
class TrainingRunner {
 public:
  enum class Stage { Idle, FindingPython, SettingUp, Training, Exporting, Done, Failed, Cancelled };

  struct Status {
    Stage stage = Stage::Idle;
    std::string message;
    float progress = -1.0f;  // 0..1, or <0 when unknown
    int epoch = 0;
    int targetEpoch = 0;
    std::string project;
  };

  TrainingRunner() = default;
  ~TrainingRunner();

  bool start(const TrainingRequest& req, std::string& error);
  void cancel();
  bool running() const { return running_.load(); }
  Status status() const;
  std::vector<std::string> outputTail() const;

  static std::filesystem::path venvPython();
  static bool environmentReady();
  static bool bundledEnvironmentReady();
  static std::filesystem::path bundledPython();
  static const char* stageName(Stage s);

  void setOnChange(std::function<void()> fn) { onChange_ = std::move(fn); }

 private:
  void run(TrainingRequest req);
  bool runStep(const std::filesystem::path& program, const std::vector<std::string>& args, Stage stage);
  bool findPython(const std::string& configured, std::filesystem::path& program, std::vector<std::string>& prefixArgs);
  void onLine(const std::string& line);
  void setStage(Stage s, std::string msg);
  void notify();

  mutable std::mutex mutex_;
  Status status_;
  std::deque<std::string> tail_;
  std::atomic<bool> running_{false};
  std::atomic<bool> cancel_{false};
  std::thread thread_;
  Process process_;
  std::function<void()> onChange_;
};

}  // namespace vt
