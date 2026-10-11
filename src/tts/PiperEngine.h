#pragma once

#include <memory>

#include "tts/ITtsEngine.h"
#include "tts/VoiceConfig.h"

namespace Ort {
struct Session;
}

namespace vt {

// Piper (VITS) voice running on onnxruntime's CPU provider.
// Expects `<name>.onnx` with its config at `<name>.onnx.json`.
class PiperEngine final : public ITtsEngine {
 public:
  PiperEngine();
  ~PiperEngine() override;

  std::string_view engineName() const override { return "Piper"; }
  bool load(const std::filesystem::path& modelPath, std::string& error) override;
  bool loaded() const override { return session_ != nullptr; }
  const std::filesystem::path& modelPath() const override { return modelPath_; }
  int sampleRate() const override { return config_.sampleRate; }
  int numSpeakers() const override { return config_.numSpeakers; }
  bool supportsPitch() const override { return false; }

  bool synthesize(std::string_view text, const SynthesisParams& params, const AudioChunkFn& onChunk,
                  std::string& error) override;

  const VoiceConfig& config() const { return config_; }

 private:
  bool runModel(const std::vector<int64_t>& ids, const SynthesisParams& params, std::vector<float>& audio,
                std::string& error);

  std::unique_ptr<Ort::Session> session_;
  VoiceConfig config_;
  std::filesystem::path modelPath_;
};

}  // namespace vt
