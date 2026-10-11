#include "tts/VoiceConfig.h"

#include <nlohmann/json.hpp>

#include "core/StringUtil.h"

namespace vt {

bool VoiceConfig::parse(const std::string& jsonText, VoiceConfig& out, std::string& error) {
  try {
    auto j = nlohmann::json::parse(jsonText);
    VoiceConfig c;
    if (auto a = j.find("audio"); a != j.end()) c.sampleRate = a->value("sample_rate", c.sampleRate);
    if (auto e = j.find("espeak"); e != j.end()) c.espeakVoice = e->value("voice", c.espeakVoice);
    c.phonemeType = j.value("phoneme_type", c.phonemeType);
    if (auto inf = j.find("inference"); inf != j.end()) {
      c.noiseScale = inf->value("noise_scale", c.noiseScale);
      c.lengthScale = inf->value("length_scale", c.lengthScale);
      c.noiseW = inf->value("noise_w", c.noiseW);
    }
    c.numSpeakers = j.value("num_speakers", 1);
    if (auto s = j.find("speaker_id_map"); s != j.end() && s->is_object()) {
      for (auto& [name, id] : s->items()) c.speakerIdMap[name] = id.get<int>();
    }
    if (auto d = j.find("dataset"); d != j.end() && d->is_string()) c.datasetName = d->get<std::string>();

    auto map = j.find("phoneme_id_map");
    if (map == j.end() || !map->is_object()) {
      error = "voice config has no phoneme_id_map";
      return false;
    }
    for (auto& [phoneme, ids] : map->items()) {
      auto cps = utf8ToUtf32(phoneme);
      if (cps.size() != 1) continue;  // Piper maps single code points
      std::vector<int64_t> v;
      for (auto& id : ids) v.push_back(id.get<int64_t>());
      c.phonemeIdMap[cps[0]] = std::move(v);
    }
    if (c.sampleRate <= 0) {
      error = "invalid sample rate";
      return false;
    }
    out = std::move(c);
    return true;
  } catch (const std::exception& e) {
    error = std::string("invalid voice config: ") + e.what();
    return false;
  }
}

}  // namespace vt
