#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "pipeline/ChatMessage.h"

namespace vt {

namespace irc {
struct Message;
}

// Read-only Twitch chat over IRC-on-WebSocket (wss://irc-ws.chat.twitch.tv).
// Runs on its own thread, reconnects with exponential backoff, answers PINGs,
// honours RECONNECT, and stops retrying on authentication failures.
class TwitchIrcClient final : public IMessageSource {
 public:
  enum class State { Disconnected, Connecting, Connected, Joined, Reconnecting, AuthFailed };

  TwitchIrcClient() = default;
  ~TwitchIrcClient() override;

  void setSink(Sink sink) override;
  void setOnChange(std::function<void()> fn) { onChange_ = std::move(fn); }

  void connect(const std::string& login, const std::string& token, const std::string& channel);
  void disconnect();

  State state() const { return state_.load(); }
  std::string statusText() const;
  std::string channel() const;
  uint64_t messagesReceived() const { return received_.load(); }

 private:
  void run();
  // Returns true if the connection should be retried.
  bool session(bool& wasStable);
  void handleLine(const std::string& line, void* curl, bool& reconnectRequested);
  bool sendLine(void* curl, const std::string& line);
  void setState(State s, std::string text);
  bool waitInterruptible(std::chrono::milliseconds d);

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<State> state_{State::Disconnected};
  std::atomic<uint64_t> received_{0};
  std::string statusText_ = "Not connected";
  std::string login_, token_, channel_;
  Sink sink_;
  std::function<void()> onChange_;
};

}  // namespace vt
