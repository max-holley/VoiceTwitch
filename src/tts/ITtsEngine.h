#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace vt {

struct SynthesisParams {
  float rate = 1.0f;               // >1 speaks faster
  int speakerId = 0;               // multi-speaker models only
  float sentenceSilenceSec = 0.2f; // inserted after each sentence
};

// Receives synthesized mono float PCM (at ITtsEngine::sampleRate()) as each
// sentence finishes. Return false to cancel the rest of the utterance.
using AudioChunkFn = std::function<bool(std::span<const float>)>;

// Text-to-speech engine abstraction. Piper is the only v1 implementation;
// additional engines plug in here without touching the pipeline or UI.
class ITtsEngine {
 public:
  virtual ~ITtsEngine() = default;

  virtual std::string_view engineName() const = 0;
  virtual bool load(const std::filesystem::path& modelPath, std::string& error) = 0;
  virtual bool loaded() const = 0;
  virtual const std::filesystem::path& modelPath() const = 0;
  virtual int sampleRate() const = 0;
  virtual int numSpeakers() const = 0;
  // Whether pitch can be controlled independently (Piper/VITS: no).
  virtual bool supportsPitch() const = 0;

  virtual bool synthesize(std::string_view text, const SynthesisParams& params, const AudioChunkFn& onChunk,
                          std::string& error) = 0;
};

// Picks an engine implementation for a model file (currently always Piper).
std::unique_ptr<ITtsEngine> createEngineForModel(const std::filesystem::path& modelPath);

}  // namespace vt
