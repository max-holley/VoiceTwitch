#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "audio/AudioPlayer.h"
#include "audio/AudioRecorder.h"
#include "cloning/TrainingRunner.h"
#include "core/Config.h"
#include "models/ModelManager.h"
#include "pipeline/BadWordFilter.h"
#include "pipeline/SpeechPipeline.h"
#include "twitch/TwitchAuth.h"
#include "twitch/TwitchIrcClient.h"

namespace vt {

// Owns every subsystem and wires them together. The UI reads and mutates
// state through this object; background threads call `wake` so the
// event-driven render loop redraws only when something changed.
class App {
 public:
  App();
  ~App();

  void init();
  // Housekeeping called once per frame (cheap).
  void tick();
  void shutdown();

  AppConfig& config() { return config_; }
  void markConfigDirty();
  void saveFilters();

  BadWordFilter& filter() { return filter_; }
  SpeechPipeline& pipeline() { return *pipeline_; }
  AudioPlayer& player() { return player_; }
  AudioRecorder& recorder() { return recorder_; }
  TwitchAuth& auth() { return auth_; }
  TwitchIrcClient& irc() { return irc_; }
  ModelManager& models() { return models_; }
  TrainingRunner& training() { return training_; }

  void setActiveVoice(const std::string& voiceId);
  // Pushes playback/reading settings from config into the pipeline.
  void applySettings();

  void connectChat();
  void disconnectChat();

  // True while something on screen animates (meters, progress bars).
  bool wantsFastRefresh() const;

  void setWake(std::function<void()> fn) { wake_ = std::move(fn); }
  void wake() const {
    if (wake_) wake_();
  }

 private:
  AppConfig config_;
  BadWordFilter filter_;
  AudioPlayer player_;
  AudioRecorder recorder_;
  std::unique_ptr<SpeechPipeline> pipeline_;
  TwitchAuth auth_;
  TwitchIrcClient irc_;
  ModelManager models_;
  TrainingRunner training_;

  std::function<void()> wake_;
  bool configDirty_ = false;
  std::chrono::steady_clock::time_point configDirtyAt_{};
  TwitchAuth::State lastAuthState_ = TwitchAuth::State::LoggedOut;
  bool userDisconnected_ = false;
  TrainingRunner::Stage lastTrainingStage_ = TrainingRunner::Stage::Idle;
};

}  // namespace vt
