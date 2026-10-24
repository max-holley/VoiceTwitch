#include "cloning/TrainingRunner.h"

#include <system_error>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"

namespace vt {

namespace fs = std::filesystem;

namespace {

constexpr size_t kTailMax = 400;
constexpr const char* kMarker = "@@VT ";

}  // namespace

TrainingRunner::~TrainingRunner() {
  cancel();
  if (thread_.joinable()) thread_.join();
}

const char* TrainingRunner::stageName(Stage s) {
  switch (s) {
    case Stage::Idle: return "Idle";
    case Stage::FindingPython: return "Checking Python";
    case Stage::SettingUp: return "Setting up training environment";
    case Stage::Training: return "Training";
    case Stage::Exporting: return "Exporting model";
    case Stage::Done: return "Done";
    case Stage::Failed: return "Failed";
    case Stage::Cancelled: return "Cancelled";
  }
  return "?";
}

fs::path TrainingRunner::venvPython() {
#ifdef _WIN32
  return paths::trainingEnvDir() / "Scripts" / "python.exe";
#else
  return paths::trainingEnvDir() / "bin" / "python";
#endif
}

bool TrainingRunner::environmentReady() {
  std::error_code ec;
  return fs::exists(venvPython(), ec) && fs::exists(paths::trainingEnvDir() / ".voicetwitch-ready", ec);
}

void TrainingRunner::notify() {
  if (onChange_) onChange_();
}

void TrainingRunner::setStage(Stage s, std::string msg) {
  {
    std::lock_guard lock(mutex_);
    status_.stage = s;
    status_.message = std::move(msg);
    if (s != Stage::Training) status_.progress = -1.0f;
  }
  notify();
}

TrainingRunner::Status TrainingRunner::status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

std::vector<std::string> TrainingRunner::outputTail() const {
  std::lock_guard lock(mutex_);
  return {tail_.begin(), tail_.end()};
}

bool TrainingRunner::start(const TrainingRequest& req, std::string& error) {
  if (running_) {
    error = "Training is already running";
    return false;
  }
  if (thread_.joinable()) thread_.join();
  cancel_ = false;
  running_ = true;
  {
    std::lock_guard lock(mutex_);
    status_ = Status{};
    status_.project = req.projectName;
    tail_.clear();
  }
  thread_ = std::thread([this, req] { run(req); });
  return true;
}

void TrainingRunner::cancel() {
  cancel_ = true;
  process_.kill();
}

void TrainingRunner::onLine(const std::string& line) {
  {
    std::lock_guard lock(mutex_);
    tail_.push_back(line);
    if (tail_.size() > kTailMax) tail_.pop_front();
  }
  if (startsWith(line, kMarker)) {
    try {
      auto j = nlohmann::json::parse(line.substr(std::char_traits<char>::length(kMarker)));
      std::lock_guard lock(mutex_);
      if (j.contains("message")) status_.message = j["message"].get<std::string>();
      if (j.contains("epoch")) status_.epoch = j["epoch"].get<int>();
      if (j.contains("target_epoch")) status_.targetEpoch = j["target_epoch"].get<int>();
      if (j.contains("progress")) status_.progress = j["progress"].get<float>();
      if (j.value("stage", "") == "export") status_.stage = Stage::Exporting;
    } catch (...) {
    }
  }
  notify();
}

bool TrainingRunner::runStep(const fs::path& program, const std::vector<std::string>& args, Stage stage) {
  if (cancel_) return false;
  std::string err;
  std::string cmd = paths::toUtf8(program);
  for (const auto& a : args) cmd += " " + a;
  log::info("Training: {}", cmd);
  {
    std::lock_guard lock(mutex_);
    status_.stage = stage;
  }
  if (!process_.start(program, args, paths::trainingScriptsDir(), [this](const std::string& l) { onLine(l); },
                      nullptr, err)) {
    setStage(Stage::Failed, err);
    return false;
  }
  int code = process_.wait();
  if (cancel_) return false;
  if (code != 0) {
    std::string last;
    {
      std::lock_guard lock(mutex_);
      for (auto it = tail_.rbegin(); it != tail_.rend(); ++it) {
        if (!trim(*it).empty() && !startsWith(*it, kMarker)) {
          last = *it;
          break;
        }
      }
    }
    setStage(Stage::Failed, "Step failed (exit code " + std::to_string(code) + ")" + (last.empty() ? "" : ": " + last));
    return false;
  }
  return true;
}

bool TrainingRunner::findPython(const std::string& configured, fs::path& program, std::vector<std::string>& prefix) {
  struct Candidate {
    std::string exe;
    std::vector<std::string> args;
  };
  std::vector<Candidate> candidates;
  if (!configured.empty()) {
    candidates.push_back({configured, {}});
  } else {
#ifdef _WIN32
    for (const auto* version : {"-3.12", "-3.11", "-3.13"})
      candidates.push_back({"py", {version}});
    candidates.push_back({"py", {"-3"}});
    candidates.push_back({"python", {}});
#else
    for (const auto* version : {"python3.12", "python3.11", "python3.13"})
      candidates.push_back({version, {}});
    candidates.push_back({"python3", {}});
    candidates.push_back({"python", {}});
#endif
  }
  for (const auto& c : candidates) {
    std::string output;
    Process p;
    std::string err;
    auto args = c.args;
    args.insert(args.end(), {paths::toUtf8(paths::trainingScriptsDir() / "train_voice.py"), "check-python"});
    if (!p.start(c.exe, args, {}, [&](const std::string& l) { output += l; }, nullptr, err)) continue;
    if (p.wait() == 0) {
      program = c.exe;
      prefix = c.args;
      log::info("Using {} ({})", c.exe, output);
      onLine(output);
      return true;
    }
    log::warn("{}: {}", c.exe, output);
    onLine(output);
  }
  return false;
}

void TrainingRunner::run(TrainingRequest req) {
  auto finish = [this](Stage s, std::string msg) {
    setStage(s, std::move(msg));
    running_ = false;
    notify();
  };
  const fs::path script = paths::trainingScriptsDir() / "train_voice.py";
  std::error_code ec;
  if (!fs::exists(script, ec)) {
    finish(Stage::Failed, "Training script not found: " + paths::toUtf8(script));
    return;
  }

  if (!environmentReady()) {
    setStage(Stage::FindingPython, "Looking for 64-bit CPython 3.11-3.13...");
    fs::path python;
    std::vector<std::string> prefix;
    if (!findPython(req.pythonPath, python, prefix)) {
      finish(Stage::Failed,
             "64-bit CPython 3.11-3.13 not found. Install standard 64-bit Python 3.12 (python.org) or set its path in Settings > Voice cloning.");
      return;
    }
    setStage(Stage::SettingUp, "Installing Piper training tools (first run, several GB)...");
    auto args = prefix;
    args.insert(args.end(), {"-u", paths::toUtf8(script), "setup", "--venv", paths::toUtf8(paths::trainingEnvDir())});
    if (!runStep(python, args, Stage::SettingUp)) {
      if (cancel_) finish(Stage::Cancelled, "Cancelled");
      else running_ = false, notify();
      return;
    }
  }

  setStage(Stage::Training, "Preparing dataset...");
  std::vector<std::string> args{"-u",
                                paths::toUtf8(script),
                                "train",
                                "--project-dir",
                                paths::toUtf8(req.projectDir),
                                "--name",
                                req.projectName,
                                "--output",
                                paths::toUtf8(req.outputModel),
                                "--espeak-voice",
                                req.espeakVoice,
                                "--sample-rate",
                                std::to_string(req.sampleRate),
                                "--base-checkpoint-url",
                                req.baseCheckpointUrl,
                                "--cache-dir",
                                paths::toUtf8(paths::dataDir() / "training-cache"),
                                "--epochs",
                                std::to_string(req.epochs),
                                "--batch-size",
                                std::to_string(req.batchSize)};
  if (!runStep(venvPython(), args, Stage::Training)) {
    if (cancel_) finish(Stage::Cancelled, "Cancelled");
    else running_ = false, notify();
    return;
  }
  if (!fs::exists(req.outputModel, ec)) {
    finish(Stage::Failed, "Training finished but no model was produced");
    return;
  }
  finish(Stage::Done, "Voice ready: " + paths::toUtf8(req.outputModel.filename()));
}

}  // namespace vt
