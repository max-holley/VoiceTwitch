#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace vt {

// A unit of text entering the speech pipeline. Twitch chat is the only v1
// source; EventSub events (subs/bits/raids) would arrive as messages with a
// different `source` and feed the same pipeline.
struct ChatMessage {
  std::string source = "twitch-chat";
  std::string channel;
  std::string userLogin;
  std::string displayName;
  std::string text;
  std::chrono::system_clock::time_point received = std::chrono::system_clock::now();
  uint64_t seq = 0;  // assigned by the pipeline

  // Internal: UI-triggered preview. Bypasses the filter/reading toggle and
  // may use a voice other than the active one.
  bool isPreview = false;
  std::filesystem::path voiceOverride;
};

// Something that produces ChatMessages (Twitch IRC today, EventSub later).
class IMessageSource {
 public:
  using Sink = std::function<void(ChatMessage)>;
  virtual ~IMessageSource() = default;
  virtual void setSink(Sink sink) = 0;
};

}  // namespace vt
