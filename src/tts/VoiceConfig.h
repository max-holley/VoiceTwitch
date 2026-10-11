#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace vt {

using PhonemeIdMap = std::unordered_map<char32_t, std::vector<int64_t>>;

// Contents of a Piper voice's `<model>.onnx.json`.
struct VoiceConfig {
  int sampleRate = 22050;
  std::string espeakVoice = "en-us";
  std::string phonemeType = "espeak";  // "espeak" or "text"
  float noiseScale = 0.667f;
  float lengthScale = 1.0f;
  float noiseW = 0.8f;
  int numSpeakers = 1;
  std::map<std::string, int> speakerIdMap;
  PhonemeIdMap phonemeIdMap;
  std::string datasetName;

  static bool parse(const std::string& jsonText, VoiceConfig& out, std::string& error);
};

}  // namespace vt
