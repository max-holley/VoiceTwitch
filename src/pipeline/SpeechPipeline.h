#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "pipeline/ChatMessage.h"
#include "tts/ITtsEngine.h"

namespace vt {

class AudioPlayer;
class BadWordFilter;

struct PipelineSettings {
  bool readingEnabled = true;
  bool announceUsername = true;
  bool skipCommands = true;
  int maxMessageChars = 300;
  SynthesisParams synth;
};

enum class MessageOutcome { Queued, Spoken, Filtered, Ignored, Paused, Skipped, Failed };

struct HistoryEntry {
  uint64_t seq = 0;
  std::string time;
  std::string who;
  std::string text;
  MessageOutcome outcome = MessageOutcome::Queued;
  std::string detail;  // e.g. which filter entry matched
};

enum class VoiceState { None, Loading, Ready, Error };

struct PipelineStatus {
  size_t queued = 0;
  uint64_t spoken = 0;
  uint64_t filtered = 0;
  bool speaking = false;
  std::string currentText;
  VoiceState voiceState = VoiceState::None;
  std::string voiceMessage;  // error text or loaded voice name
  int voiceSampleRate = 0;
  int voiceSpeakers = 0;
  double lastSynthesisMs = 0;  // wall time of the last utterance's synthesis
};

// Chat processing pipeline:
//   1. message received (submit)
//   2. bad-word filter  -> dropped if matched (never queued)
//   3. FIFO queue
//   4. synthesis with the active voice (worker thread)
//   5. playback; next message waits until the previous finishes (no overlap)
class SpeechPipeline {
 public:
  SpeechPipeline(AudioPlayer& player, const BadWordFilter& filter);
  ~SpeechPipeline();

  void start();
  void shutdown();

  void submit(ChatMessage msg);
  void preview(std::string text, const std::filesystem::path& voice);

  // Loads the voice asynchronously on the worker thread. Empty path unloads.
  void setVoice(const std::filesystem::path& modelPath);
  void setSettings(const PipelineSettings& s);
  PipelineSettings settings() const;

  void skipCurrent();
  void clearQueue();

  PipelineStatus status() const;
  std::vector<HistoryEntry> history() const;

  // Invoked (from any thread) whenever status/history changes, so the UI can
  // wake up and redraw.
  void setOnChange(std::function<void()> fn) { onChange_ = std::move(fn); }

  // Builds the text that will actually be spoken (exposed for tests).
  static std::string composeSpeech(const ChatMessage& m, const PipelineSettings& s);

 private:
  void workerLoop();
  void speak(const ChatMessage& msg, ITtsEngine& engine);
  void addHistory(const ChatMessage& m, MessageOutcome o, std::string detail = {});
  void setOutcome(const ChatMessage& m, MessageOutcome o);
  void notify();

  AudioPlayer& player_;
  const BadWordFilter& filter_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<ChatMessage> queue_;
  std::deque<HistoryEntry> history_;
  PipelineSettings settings_;
  PipelineStatus status_;
  uint64_t nextSeq_ = 0;
  bool stopping_ = false;
  bool voiceChangePending_ = false;
  std::filesystem::path pendingVoice_;

  std::unique_ptr<ITtsEngine> engine_;         // worker thread only
  std::unique_ptr<ITtsEngine> previewEngine_;  // worker thread only
  std::atomic<bool> skipFlag_{false};
  std::thread worker_;
  std::function<void()> onChange_;
};

}  // namespace vt
