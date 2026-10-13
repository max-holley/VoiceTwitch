#include "core/Config.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "core/Platform.h"

namespace vt {

using nlohmann::json;

namespace {

template <class T>
void get(const json& j, const char* key, T& out) {
  if (auto it = j.find(key); it != j.end() && !it->is_null()) {
    try {
      out = it->get<T>();
    } catch (const std::exception&) {
      // keep default on type mismatch
    }
  }
}

}  // namespace

json AppConfig::toJson() const {
  return json{
      {"twitch",
       {{"channel", twitchChannel},
        {"login", twitchLogin},
        {"auto_connect", autoConnect}}},
      {"voice", {{"active", activeVoice}, {"speaker_id", speakerId}}},
      {"playback",
       {{"volume", volume},
        {"rate", rate},
        {"sentence_silence", sentenceSilence},
        {"reading_enabled", readingEnabled},
        {"announce_username", announceUsername},
        {"skip_commands", skipCommands},
        {"max_message_chars", maxMessageChars}}},
      {"cloning",
       {{"python_path", pythonPath},
        {"espeak_voice", cloneEspeakVoice},
        {"base_checkpoint_url", baseCheckpointUrl},
        {"epochs", trainingEpochs},
        {"batch_size", trainingBatchSize}}},
  };
}

AppConfig AppConfig::fromJson(const json& j) {
  AppConfig c;
  if (auto t = j.find("twitch"); t != j.end() && t->is_object()) {
    get(*t, "channel", c.twitchChannel);
    get(*t, "login", c.twitchLogin);
    get(*t, "auto_connect", c.autoConnect);
  }
  if (auto v = j.find("voice"); v != j.end() && v->is_object()) {
    get(*v, "active", c.activeVoice);
    get(*v, "speaker_id", c.speakerId);
  }
  if (auto p = j.find("playback"); p != j.end() && p->is_object()) {
    get(*p, "volume", c.volume);
    get(*p, "rate", c.rate);
    get(*p, "sentence_silence", c.sentenceSilence);
    get(*p, "reading_enabled", c.readingEnabled);
    get(*p, "announce_username", c.announceUsername);
    get(*p, "skip_commands", c.skipCommands);
    get(*p, "max_message_chars", c.maxMessageChars);
  }
  if (auto cl = j.find("cloning"); cl != j.end() && cl->is_object()) {
    get(*cl, "python_path", c.pythonPath);
    get(*cl, "espeak_voice", c.cloneEspeakVoice);
    get(*cl, "base_checkpoint_url", c.baseCheckpointUrl);
    get(*cl, "epochs", c.trainingEpochs);
    get(*cl, "batch_size", c.trainingBatchSize);
  }
  c.volume = std::clamp(c.volume, 0.0f, 1.0f);
  c.rate = std::clamp(c.rate, 0.5f, 2.0f);
  c.sentenceSilence = std::clamp(c.sentenceSilence, 0.0f, 2.0f);
  c.maxMessageChars = std::clamp(c.maxMessageChars, 20, 2000);
  return c;
}

AppConfig AppConfig::load(const std::filesystem::path& path) {
  std::string text;
  if (!platform::readFile(path, text)) return AppConfig{};
  try {
    return fromJson(json::parse(text));
  } catch (const std::exception& e) {
    log::warn("config.json is invalid ({}); using defaults", e.what());
    return AppConfig{};
  }
}

bool AppConfig::save(const std::filesystem::path& path) const {
  return platform::writeFileAtomic(path, toJson().dump(2));
}

}  // namespace vt
