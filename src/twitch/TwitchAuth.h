#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace vt {

struct TokenInfo {
  std::string login;
  std::string userId;
  std::string clientId;
  int64_t expiresIn = 0;  // seconds; 0 = never/unknown
};

// Twitch OAuth 2.0 (implicit grant) with a localhost redirect.
//
// The user registers a Twitch application with redirect URL
// http://localhost:<port> and enters its Client ID. Login opens the browser at
// id.twitch.tv; Twitch redirects back to the loopback server, whose page hands
// the token (from the URL fragment) to the app. Only `chat:read` is requested.
// Tokens are kept in the OS credential store and validated hourly, as Twitch
// requires.
class TwitchAuth {
 public:
  enum class State { LoggedOut, WaitingForBrowser, Validating, LoggedIn, Error };

  TwitchAuth() = default;
  ~TwitchAuth();

  // Starts the browser flow on a background thread.
  void beginLogin(const std::string& clientId, int port);
  void cancelLogin();
  // Revokes the token (best effort) and removes it from the credential store.
  void logout();
  // Loads a stored token and validates it (background thread).
  void restore();
  // Re-validates the current token if the last check is older than an hour.
  void validateIfDue();

  State state() const { return state_.load(); }
  std::string error() const;
  std::string login() const;
  std::string authorizeUrl() const;
  // Access token for IRC; empty unless LoggedIn.
  std::string token() const;

  void setOnChange(std::function<void()> fn) { onChange_ = std::move(fn); }

  static std::optional<TokenInfo> validate(const std::string& token, std::string& error);
  static std::string buildAuthorizeUrl(const std::string& clientId, int port, const std::string& state);

 private:
  void loginThread(std::string clientId, int port);
  void finishWithToken(const std::string& token);
  void setError(std::string msg);
  void joinWorker();
  void notify();

  mutable std::mutex mutex_;
  std::atomic<State> state_{State::LoggedOut};
  std::atomic<bool> cancel_{false};
  std::thread worker_;
  std::string token_;
  std::string login_;
  std::string clientId_;
  std::string error_;
  std::string authorizeUrl_;
  std::chrono::steady_clock::time_point lastValidated_{};
  std::function<void()> onChange_;
};

}  // namespace vt
