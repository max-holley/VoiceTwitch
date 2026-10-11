#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "audio/AudioProcessing.h"

namespace vt {

// A voice-cloning project: a fixed list of prompts, the user's recordings of
// them, and (after training) the fine-tuned Piper model. Lives in
// cloned-voices/<name>/:
//   project.json           prompts + settings
//   wavs/utt_0001.wav ...  preprocessed recordings (22.05 kHz mono 16-bit)
//   metadata.csv           LJSpeech-style "file|text" list for training
//   <name>.onnx(.json)     the trained voice
class CloneProject {
 public:
  static constexpr int kSampleRate = 22050;

  static bool isValidName(const std::string& name);
  static std::vector<std::string> listProjects();
  static std::optional<CloneProject> create(const std::string& name, const std::vector<std::string>& prompts,
                                            std::string& error);
  static std::optional<CloneProject> open(const std::string& name, std::string& error);

  const std::string& name() const { return name_; }
  const std::vector<std::string>& prompts() const { return prompts_; }
  std::filesystem::path dir() const;
  std::filesystem::path wavsDir() const;
  std::filesystem::path wavPath(size_t index) const;
  std::filesystem::path metadataPath() const;
  std::filesystem::path modelPath() const;

  bool hasRecording(size_t index) const;
  size_t recordedCount() const;
  double recordedSeconds() const;
  bool hasModel() const;

  // Preprocesses (DC removal, silence trim, peak normalize, resample to
  // 22.05 kHz) and saves a take. `check` receives quality warnings.
  bool saveRecording(size_t index, std::vector<float> raw, int sampleRate, std::string& error,
                     dsp::ClipCheck* check = nullptr);
  bool deleteRecording(size_t index);

  // Writes metadata.csv for all recorded prompts. Returns lines written.
  size_t writeMetadata(std::string& error) const;

 private:
  std::string name_;
  std::vector<std::string> prompts_;
};

}  // namespace vt
