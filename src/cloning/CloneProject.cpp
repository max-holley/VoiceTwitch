#include "cloning/CloneProject.h"

#include <algorithm>
#include <cstdio>
#include <system_error>

#include <nlohmann/json.hpp>

#include "audio/Wav.h"
#include "core/Paths.h"
#include "core/Platform.h"

namespace vt {

namespace fs = std::filesystem;

bool CloneProject::isValidName(const std::string& name) {
  if (name.empty() || name.size() > 40) return false;
  return std::all_of(name.begin(), name.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
  });
}

std::vector<std::string> CloneProject::listProjects() {
  std::vector<std::string> out;
  std::error_code ec;
  for (auto it = fs::directory_iterator(paths::clonedVoicesDir(), ec); !ec && it != fs::directory_iterator();
       it.increment(ec)) {
    if (it->is_directory(ec) && fs::exists(it->path() / "project.json", ec)) {
      out.push_back(paths::toUtf8(it->path().filename()));
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::optional<CloneProject> CloneProject::create(const std::string& name, const std::vector<std::string>& prompts,
                                                 std::string& error) {
  if (!isValidName(name)) {
    error = "Use 1-40 letters, digits, '-' or '_'";
    return std::nullopt;
  }
  CloneProject p;
  p.name_ = name;
  p.prompts_ = prompts;
  std::error_code ec;
  if (fs::exists(p.dir() / "project.json", ec)) {
    error = "A project with that name already exists";
    return std::nullopt;
  }
  fs::create_directories(p.wavsDir(), ec);
  if (ec) {
    error = "Cannot create project folder: " + ec.message();
    return std::nullopt;
  }
  nlohmann::json j{{"name", name}, {"sample_rate", kSampleRate}, {"prompts", prompts}};
  if (!platform::writeFileAtomic(p.dir() / "project.json", j.dump(2))) {
    error = "Cannot write project.json";
    return std::nullopt;
  }
  return p;
}

std::optional<CloneProject> CloneProject::open(const std::string& name, std::string& error) {
  CloneProject p;
  p.name_ = name;
  std::string text;
  if (!platform::readFile(p.dir() / "project.json", text)) {
    error = "project.json not found";
    return std::nullopt;
  }
  try {
    auto j = nlohmann::json::parse(text);
    p.prompts_ = j.at("prompts").get<std::vector<std::string>>();
  } catch (const std::exception& e) {
    error = std::string("invalid project.json: ") + e.what();
    return std::nullopt;
  }
  std::error_code ec;
  fs::create_directories(p.wavsDir(), ec);
  return p;
}

fs::path CloneProject::dir() const { return paths::clonedVoicesDir() / paths::fromUtf8(name_); }
fs::path CloneProject::wavsDir() const { return dir() / "wavs"; }
fs::path CloneProject::metadataPath() const { return dir() / "metadata.csv"; }
fs::path CloneProject::modelPath() const { return dir() / paths::fromUtf8(name_ + ".onnx"); }

fs::path CloneProject::wavPath(size_t index) const {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "utt_%04zu.wav", index + 1);
  return wavsDir() / buf;
}

bool CloneProject::hasRecording(size_t index) const {
  std::error_code ec;
  return fs::exists(wavPath(index), ec);
}

size_t CloneProject::recordedCount() const {
  size_t n = 0;
  for (size_t i = 0; i < prompts_.size(); ++i) n += hasRecording(i) ? 1 : 0;
  return n;
}

double CloneProject::recordedSeconds() const {
  double total = 0;
  std::error_code ec;
  for (size_t i = 0; i < prompts_.size(); ++i) {
    auto sz = fs::file_size(wavPath(i), ec);
    if (!ec && sz > 44) total += static_cast<double>(sz - 44) / 2.0 / kSampleRate;
  }
  return total;
}

bool CloneProject::hasModel() const {
  std::error_code ec;
  auto cfg = modelPath();
  cfg += ".json";
  return fs::exists(modelPath(), ec) && fs::exists(cfg, ec);
}

bool CloneProject::saveRecording(size_t index, std::vector<float> raw, int sampleRate, std::string& error,
                                 dsp::ClipCheck* check) {
  if (index >= prompts_.size()) {
    error = "prompt index out of range";
    return false;
  }
  if (check) *check = dsp::checkClip(raw, sampleRate);
  dsp::removeDc(raw);
  auto trimmed = dsp::trimSilence(raw, sampleRate);
  if (trimmed.empty()) {
    error = "Nothing but silence was recorded - check your microphone";
    return false;
  }
  auto resampled = dsp::resample(trimmed, sampleRate, kSampleRate);
  dsp::normalizePeak(resampled, -1.0f);
  if (check) check->durationSec = static_cast<float>(resampled.size()) / kSampleRate;
  if (!wav::writeMono16(wavPath(index), resampled, kSampleRate)) {
    error = "Cannot write " + paths::toUtf8(wavPath(index));
    return false;
  }
  return true;
}

bool CloneProject::deleteRecording(size_t index) {
  std::error_code ec;
  return fs::remove(wavPath(index), ec);
}

size_t CloneProject::writeMetadata(std::string& error) const {
  std::string csv;
  size_t n = 0;
  for (size_t i = 0; i < prompts_.size(); ++i) {
    if (!hasRecording(i)) continue;
    csv += paths::toUtf8(wavPath(i).filename()) + "|" + prompts_[i] + "\n";
    ++n;
  }
  if (!platform::writeFileAtomic(metadataPath(), csv)) {
    error = "Cannot write metadata.csv";
    return 0;
  }
  return n;
}

}  // namespace vt
