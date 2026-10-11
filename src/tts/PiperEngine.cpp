#include "tts/PiperEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <thread>

#include <onnxruntime_cxx_api.h>

#include "core/Log.h"
#include "core/Paths.h"
#include "core/Platform.h"
#include "core/StringUtil.h"
#include "tts/PhonemeIds.h"
#include "tts/Phonemizer.h"

namespace vt {

namespace {

Ort::Env& ortEnv() {
  static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "voicetwitch");
  return env;
}

// Lowercase + code points, used for "text" phoneme_type voices.
std::u32string textPhonemes(std::string_view text) {
  auto cps = utf8ToUtf32(text);
  for (auto& c : cps) {
    if (c >= U'A' && c <= U'Z') c = c - U'A' + U'a';
  }
  return cps;
}

}  // namespace

std::unique_ptr<ITtsEngine> createEngineForModel(const std::filesystem::path&) {
  return std::make_unique<PiperEngine>();
}

PiperEngine::PiperEngine() = default;
PiperEngine::~PiperEngine() = default;

bool PiperEngine::load(const std::filesystem::path& modelPath, std::string& error) {
  session_.reset();
  modelPath_.clear();

  auto configPath = modelPath;
  configPath += ".json";
  std::string text;
  if (!platform::readFile(configPath, text)) {
    error = "missing voice config " + configPath.filename().string();
    return false;
  }
  VoiceConfig cfg;
  if (!VoiceConfig::parse(text, cfg, error)) return false;

  if (cfg.phonemeType == "espeak" && !phonemizer::init(paths::espeakDataDir(), error)) return false;

  try {
    Ort::SessionOptions opts;
    const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) / 2, 1, 4);
    opts.SetIntraOpNumThreads(threads);
    opts.SetInterOpNumThreads(1);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    // Input length varies per sentence, so memory patterns don't help, and
    // the arena would keep the high-water mark of the longest sentence.
    opts.DisableMemPattern();
    opts.DisableCpuMemArena();
    // Prepacking duplicates weights into kernel-specific layouts; skipping it
    // keeps the footprint close to the model size.
    opts.AddConfigEntry("session.disable_prepacking", "1");
    session_ = std::make_unique<Ort::Session>(ortEnv(), modelPath.c_str(), opts);
  } catch (const Ort::Exception& e) {
    error = std::string("failed to load model: ") + e.what();
    return false;
  }
  config_ = std::move(cfg);
  modelPath_ = modelPath;
  log::info("Loaded Piper voice {} ({} Hz, {} speaker(s))", modelPath.filename().string(), config_.sampleRate,
            config_.numSpeakers);
  return true;
}

bool PiperEngine::runModel(const std::vector<int64_t>& idsIn, const SynthesisParams& params,
                           std::vector<float>& audio, std::string& error) {
  try {
    auto mem = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    std::vector<int64_t> ids = idsIn;
    std::array<int64_t, 2> idsShape{1, static_cast<int64_t>(ids.size())};
    std::array<int64_t, 1> lengths{static_cast<int64_t>(ids.size())};
    std::array<int64_t, 1> lengthsShape{1};
    const float rate = std::clamp(params.rate, 0.25f, 4.0f);
    std::array<float, 3> scales{config_.noiseScale, config_.lengthScale / rate, config_.noiseW};
    std::array<int64_t, 1> scalesShape{3};
    std::array<int64_t, 1> sid{std::clamp<int64_t>(params.speakerId, 0, std::max(0, config_.numSpeakers - 1))};
    std::array<int64_t, 1> sidShape{1};

    std::vector<Ort::Value> inputs;
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, ids.data(), ids.size(), idsShape.data(), idsShape.size()));
    inputs.push_back(
        Ort::Value::CreateTensor<int64_t>(mem, lengths.data(), lengths.size(), lengthsShape.data(), 1));
    inputs.push_back(Ort::Value::CreateTensor<float>(mem, scales.data(), scales.size(), scalesShape.data(), 1));
    std::vector<const char*> inputNames{"input", "input_lengths", "scales"};
    if (config_.numSpeakers > 1) {
      inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, sid.data(), sid.size(), sidShape.data(), 1));
      inputNames.push_back("sid");
    }
    const char* outputNames[] = {"output"};
    auto outputs = session_->Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), inputs.size(),
                                 outputNames, 1);
    if (outputs.empty() || !outputs[0].IsTensor()) {
      error = "model produced no audio";
      return false;
    }
    auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    const auto n = static_cast<size_t>(shape.back());
    const float* data = outputs[0].GetTensorData<float>();
    audio.assign(data, data + n);
    return true;
  } catch (const Ort::Exception& e) {
    error = std::string("inference failed: ") + e.what();
    return false;
  }
}

bool PiperEngine::synthesize(std::string_view text, const SynthesisParams& params, const AudioChunkFn& onChunk,
                             std::string& error) {
  if (!session_) {
    error = "no voice loaded";
    return false;
  }
  std::vector<std::u32string> sentences;
  if (config_.phonemeType == "text") {
    sentences.push_back(textPhonemes(text));
  } else if (!phonemizer::phonemize(text, config_.espeakVoice, sentences, error)) {
    return false;
  }

  const auto silenceSamples =
      static_cast<size_t>(std::max(0.0f, params.sentenceSilenceSec) * static_cast<float>(config_.sampleRate));
  std::vector<float> audio;
  for (const auto& sentence : sentences) {
    size_t missing = 0;
    auto ids = phonemesToIds(sentence, config_.phonemeIdMap, &missing);
    if (missing > 0) log::debug("{} phoneme(s) not in voice map were skipped", missing);
    if (ids.size() <= 3) continue;  // only BOS/PAD/EOS
    if (!runModel(ids, params, audio, error)) return false;
    audio.resize(audio.size() + silenceSamples, 0.0f);
    if (!onChunk(audio)) return true;  // cancelled
  }
  return true;
}

}  // namespace vt
