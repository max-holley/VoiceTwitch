#include "app/App.h"

#include "core/Log.h"
#include "core/Paths.h"

namespace vt {

App::App() : pipeline_(std::make_unique<SpeechPipeline>(player_, filter_)) {}

App::~App() { shutdown(); }

void App::init() {
  paths::ensureDataDirs();
  log::setFile(paths::dataDir() / "voicetwitch.log");
  log::info("VoiceTwitch {} starting; data dir {}", VT_VERSION, paths::toUtf8(paths::dataDir()));

  config_ = AppConfig::load(paths::configFile());
  if (!filter_.load(paths::filtersFile())) filter_.setWords({});

  auto wakeFn = [this] { wake(); };
  pipeline_->setOnChange(wakeFn);
  auth_.setOnChange(wakeFn);
  irc_.setOnChange(wakeFn);
  models_.setOnChange(wakeFn);
  training_.setOnChange(wakeFn);
  irc_.setSink([this](ChatMessage m) { pipeline_->submit(std::move(m)); });

  applySettings();
  pipeline_->start();

  models_.refreshInstalled();
  models_.loadCachedCatalog();
  if (models_.catalog().empty()) models_.refreshCatalog();

  // Model loading happens on the pipeline thread, so the window appears
  // immediately even with a large voice.
  if (!config_.activeVoice.empty()) {
    if (models_.findInstalled(config_.activeVoice)) {
      pipeline_->setVoice(ModelManager::resolveVoiceId(config_.activeVoice));
    } else {
      log::warn("Active voice {} is no longer installed", config_.activeVoice);
    }
  } else if (auto installed = models_.installed(); !installed.empty()) {
    setActiveVoice(installed.front().id);
  }

  auth_.restore();
}

void App::shutdown() {
  if (!pipeline_) return;
  irc_.setOnChange(nullptr);
  irc_.disconnect();
  training_.cancel();
  pipeline_->shutdown();
  if (configDirty_) config_.save(paths::configFile());
  configDirty_ = false;
  pipeline_->setOnChange(nullptr);
  auth_.setOnChange(nullptr);
  models_.setOnChange(nullptr);
  training_.setOnChange(nullptr);
}

void App::markConfigDirty() {
  configDirty_ = true;
  configDirtyAt_ = std::chrono::steady_clock::now();
}

void App::saveFilters() {
  if (!filter_.save(paths::filtersFile())) log::error("Could not save filters.json");
}

void App::setActiveVoice(const std::string& voiceId) {
  config_.activeVoice = voiceId;
  markConfigDirty();
  pipeline_->setVoice(ModelManager::resolveVoiceId(voiceId));
}

void App::applySettings() {
  PipelineSettings s;
  s.readingEnabled = config_.readingEnabled;
  s.announceUsername = config_.announceUsername;
  s.skipCommands = config_.skipCommands;
  s.maxMessageChars = config_.maxMessageChars;
  s.synth.rate = config_.rate;
  s.synth.speakerId = config_.speakerId;
  s.synth.sentenceSilenceSec = config_.sentenceSilence;
  pipeline_->setSettings(s);
  player_.setVolume(config_.volume);
}

void App::connectChat() {
  userDisconnected_ = false;
  auto token = auth_.token();
  if (token.empty()) return;
  auto login = auth_.login();
  irc_.connect(login, token, config_.twitchChannel.empty() ? login : config_.twitchChannel);
}

void App::disconnectChat() {
  userDisconnected_ = true;
  irc_.disconnect();
}

bool App::wantsFastRefresh() const {
  if (recorder_.isRecording() || player_.isPlaying()) return true;
  if (training_.running() || models_.catalogLoading()) return true;
  for (const auto& d : models_.downloads()) {
    if (d.active) return true;
  }
  return false;
}

void App::tick() {
  // Debounced config save.
  if (configDirty_ && std::chrono::steady_clock::now() - configDirtyAt_ > std::chrono::seconds(1)) {
    if (!config_.save(paths::configFile())) log::error("Could not save config.json");
    configDirty_ = false;
  }

  // React to login state transitions.
  auto authState = auth_.state();
  if (authState != lastAuthState_) {
    if (authState == TwitchAuth::State::LoggedIn) {
      if (config_.twitchLogin != auth_.login()) {
        config_.twitchLogin = auth_.login();
        markConfigDirty();
      }
      if (config_.autoConnect && !userDisconnected_) connectChat();
    } else if (lastAuthState_ == TwitchAuth::State::LoggedIn) {
      irc_.disconnect();
    }
    lastAuthState_ = authState;
  }
  auth_.validateIfDue();

  // A fine-tuned voice just finished: make it visible in Voice Models.
  auto stage = training_.status().stage;
  if (stage != lastTrainingStage_) {
    if (stage == TrainingRunner::Stage::Done) models_.refreshInstalled();
    lastTrainingStage_ = stage;
  }
}

}  // namespace vt
