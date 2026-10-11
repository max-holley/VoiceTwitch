#include "pipeline/SpeechPipeline.h"

#include <chrono>
#include <ctime>

#include "audio/AudioPlayer.h"
#include "core/Log.h"
#include "core/StringUtil.h"
#include "pipeline/BadWordFilter.h"

namespace vt {

namespace {

constexpr size_t kHistoryMax = 200;
// Padding after each utterance so the tail isn't cut when the device stops.
constexpr float kTailSeconds = 0.15f;

std::string hms(std::chrono::system_clock::time_point tp) {
  auto t = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

// Cuts at a UTF-8 boundary, preferring the last space before `maxBytes`.
std::string truncateText(const std::string& s, size_t maxBytes) {
  if (s.size() <= maxBytes) return s;
  size_t cut = maxBytes;
  while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
  if (auto sp = s.rfind(' ', cut); sp != std::string::npos && sp > maxBytes / 2) cut = sp;
  return s.substr(0, cut);
}

}  // namespace

SpeechPipeline::SpeechPipeline(AudioPlayer& player, const BadWordFilter& filter) : player_(player), filter_(filter) {}

SpeechPipeline::~SpeechPipeline() { shutdown(); }

void SpeechPipeline::start() {
  if (worker_.joinable()) return;
  stopping_ = false;
  worker_ = std::thread([this] { workerLoop(); });
}

void SpeechPipeline::shutdown() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  skipFlag_ = true;
  player_.stop();
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void SpeechPipeline::notify() {
  if (onChange_) onChange_();
}

std::string SpeechPipeline::composeSpeech(const ChatMessage& m, const PipelineSettings& s) {
  std::string body = trim(m.text);
  // Strip the IRC /me wrapper ("\x01ACTION ...\x01").
  if (startsWith(body, "\x01" "ACTION ")) {
    body = body.substr(8);
    if (!body.empty() && body.back() == '\x01') body.pop_back();
    if (s.announceUsername && !m.isPreview) {
      return truncateText((m.displayName.empty() ? m.userLogin : m.displayName) + " " + body,
                          static_cast<size_t>(s.maxMessageChars));
    }
  }
  body = truncateText(body, static_cast<size_t>(s.maxMessageChars));
  if (s.announceUsername && !m.isPreview) {
    const std::string& who = m.displayName.empty() ? m.userLogin : m.displayName;
    if (!who.empty()) return who + " says " + body;
  }
  return body;
}

void SpeechPipeline::addHistory(const ChatMessage& m, MessageOutcome o, std::string detail) {
  HistoryEntry e;
  e.seq = m.seq;
  e.time = hms(m.received);
  e.who = m.isPreview ? "[preview]" : (m.displayName.empty() ? m.userLogin : m.displayName);
  e.text = m.text;
  e.outcome = o;
  e.detail = std::move(detail);
  history_.push_back(std::move(e));
  if (history_.size() > kHistoryMax) history_.pop_front();
}

void SpeechPipeline::setOutcome(const ChatMessage& m, MessageOutcome o) {
  for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
    if (it->seq == m.seq) {
      it->outcome = o;
      return;
    }
  }
}

void SpeechPipeline::submit(ChatMessage msg) {
  {
    std::lock_guard lock(mutex_);
    const auto trimmed = trim(msg.text);
    if (trimmed.empty()) return;
    msg.seq = ++nextSeq_;
    if (!msg.isPreview) {
      if (!settings_.readingEnabled) {
        addHistory(msg, MessageOutcome::Paused);
      } else if (settings_.skipCommands && trimmed.front() == '!') {
        addHistory(msg, MessageOutcome::Ignored, "command");
      } else if (auto hit = filter_.match(msg.text)) {
        // Step 2: filtered messages never reach the queue.
        ++status_.filtered;
        addHistory(msg, MessageOutcome::Filtered, *hit);
      } else {
        addHistory(msg, MessageOutcome::Queued);
        queue_.push_back(std::move(msg));
      }
    } else {
      addHistory(msg, MessageOutcome::Queued);
      queue_.push_back(std::move(msg));
    }
    status_.queued = queue_.size();
  }
  cv_.notify_all();
  notify();
}

void SpeechPipeline::preview(std::string text, const std::filesystem::path& voice) {
  ChatMessage m;
  m.source = "preview";
  m.text = std::move(text);
  m.isPreview = true;
  m.voiceOverride = voice;
  submit(std::move(m));
}

void SpeechPipeline::setVoice(const std::filesystem::path& modelPath) {
  {
    std::lock_guard lock(mutex_);
    pendingVoice_ = modelPath;
    voiceChangePending_ = true;
    status_.voiceState = modelPath.empty() ? VoiceState::None : VoiceState::Loading;
    status_.voiceMessage = modelPath.empty() ? "" : modelPath.filename().string();
  }
  cv_.notify_all();
  notify();
}

void SpeechPipeline::setSettings(const PipelineSettings& s) {
  std::lock_guard lock(mutex_);
  settings_ = s;
}

PipelineSettings SpeechPipeline::settings() const {
  std::lock_guard lock(mutex_);
  return settings_;
}

void SpeechPipeline::skipCurrent() {
  skipFlag_ = true;
  player_.stop();
}

void SpeechPipeline::clearQueue() {
  {
    std::lock_guard lock(mutex_);
    for (auto& m : queue_) setOutcome(m, MessageOutcome::Skipped);
    queue_.clear();
    status_.queued = 0;
  }
  skipCurrent();
  notify();
}

PipelineStatus SpeechPipeline::status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

std::vector<HistoryEntry> SpeechPipeline::history() const {
  std::lock_guard lock(mutex_);
  return {history_.begin(), history_.end()};
}

void SpeechPipeline::workerLoop() {
  while (true) {
    ChatMessage msg;
    std::filesystem::path voiceToLoad;
    bool loadVoice = false;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || voiceChangePending_ || !queue_.empty(); });
      if (stopping_) break;
      if (voiceChangePending_) {
        voiceChangePending_ = false;
        loadVoice = true;
        voiceToLoad = pendingVoice_;
      } else {
        msg = std::move(queue_.front());
        queue_.pop_front();
        status_.queued = queue_.size();
      }
    }

    if (loadVoice) {
      engine_.reset();
      if (!voiceToLoad.empty()) {
        auto engine = createEngineForModel(voiceToLoad);
        std::string err;
        bool ok = engine->load(voiceToLoad, err);
        std::lock_guard lock(mutex_);
        if (ok) {
          status_.voiceState = VoiceState::Ready;
          status_.voiceMessage = voiceToLoad.filename().string();
          status_.voiceSampleRate = engine->sampleRate();
          status_.voiceSpeakers = engine->numSpeakers();
          engine_ = std::move(engine);
        } else {
          status_.voiceState = VoiceState::Error;
          status_.voiceMessage = err;
          log::error("Voice load failed: {}", err);
        }
      }
      notify();
      continue;
    }

    // Pick the engine: active voice, or a (cached) preview engine.
    ITtsEngine* engine = engine_.get();
    if (msg.isPreview && !msg.voiceOverride.empty() &&
        (!engine_ || engine_->modelPath() != msg.voiceOverride)) {
      if (!previewEngine_ || previewEngine_->modelPath() != msg.voiceOverride) {
        previewEngine_ = createEngineForModel(msg.voiceOverride);
        std::string err;
        if (!previewEngine_->load(msg.voiceOverride, err)) {
          log::error("Preview voice load failed: {}", err);
          previewEngine_.reset();
          std::lock_guard lock(mutex_);
          setOutcome(msg, MessageOutcome::Failed);
          notify();
          continue;
        }
      }
      engine = previewEngine_.get();
    }
    if (!engine) {
      std::lock_guard lock(mutex_);
      setOutcome(msg, MessageOutcome::Failed);
      log::warn("No voice loaded; message not spoken");
      notify();
      continue;
    }
    speak(msg, *engine);
  }
  engine_.reset();
  previewEngine_.reset();
}

void SpeechPipeline::speak(const ChatMessage& msg, ITtsEngine& engine) {
  PipelineSettings s = settings();
  const std::string text = composeSpeech(msg, s);
  {
    std::lock_guard lock(mutex_);
    status_.speaking = true;
    status_.currentText = text;
  }
  notify();
  skipFlag_ = false;

  std::string err;
  bool started = player_.begin(engine.sampleRate(), err);
  bool ok = started;
  if (started) {
    auto t0 = std::chrono::steady_clock::now();
    bool first = true;
    ok = engine.synthesize(text, s.synth,
                           [&](std::span<const float> chunk) {
                             if (first) {
                               first = false;
                               auto ms = std::chrono::duration<double, std::milli>(
                                             std::chrono::steady_clock::now() - t0)
                                             .count();
                               std::lock_guard lock(mutex_);
                               status_.lastSynthesisMs = ms;
                             }
                             if (skipFlag_) return false;
                             player_.push(chunk);
                             return !skipFlag_;
                           },
                           err);
    if (!skipFlag_) {
      std::vector<float> tail(static_cast<size_t>(kTailSeconds * static_cast<float>(engine.sampleRate())), 0.0f);
      player_.push(tail);
      player_.waitUntilDrained();
    }
    player_.end();
  }
  if (!ok) log::error("Speech failed: {}", err);

  {
    std::lock_guard lock(mutex_);
    status_.speaking = false;
    status_.currentText.clear();
    if (ok && !skipFlag_) {
      ++status_.spoken;
      setOutcome(msg, MessageOutcome::Spoken);
    } else {
      setOutcome(msg, skipFlag_ ? MessageOutcome::Skipped : MessageOutcome::Failed);
    }
  }
  notify();
}

}  // namespace vt
