#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace vt {

// Public Twitch application (Client type: Public). The Client ID is not a
// secret. The redirect URL registered in the Twitch console must be exactly
// http://localhost:<kTwitchRedirectPort>.
inline constexpr const char* kTwitchClientId = "f6p1gsui2375swkw82u2aw54v2oz21";
inline constexpr int kTwitchRedirectPort = 17563;

// Persistent app settings (config.json). The Twitch OAuth token is NOT stored
// here; see SecureStore.
struct AppConfig {
  // Twitch
  std::string twitchChannel;  // channel to read; defaults to the logged-in user
  std::string twitchLogin;    // cached login name of the authorized account
  bool autoConnect = true;

  // Voice
  std::string activeVoice;  // model path relative to the data dir
  int speakerId = 0;

  // Playback / reading
  float volume = 0.8f;      // 0..1
  float rate = 1.0f;        // speaking rate multiplier (divides length_scale)
  float sentenceSilence = 0.2f;  // seconds between sentences
  bool readingEnabled = true;
  bool announceUsername = true;
  bool skipCommands = true;  // ignore messages starting with '!'
  int maxMessageChars = 300;

  // Voice cloning
  std::string pythonPath;  // empty = auto-detect
  std::string cloneEspeakVoice = "en-us";
  std::string baseCheckpointUrl =
      "https://huggingface.co/datasets/rhasspy/piper-checkpoints/resolve/main/en/en_US/lessac/medium/"
      "epoch%3D2164-step%3D1355540.ckpt";
  int trainingEpochs = 300;  // additional epochs on top of the base checkpoint
  int trainingBatchSize = 16;

  static AppConfig load(const std::filesystem::path& path);
  bool save(const std::filesystem::path& path) const;

  nlohmann::json toJson() const;
  static AppConfig fromJson(const nlohmann::json& j);
};

}  // namespace vt
