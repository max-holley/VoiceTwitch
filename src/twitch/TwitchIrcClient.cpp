#include "twitch/TwitchIrcClient.h"

#include <algorithm>
#include <chrono>

#include <curl/curl.h>

#include "core/Log.h"
#include "core/StringUtil.h"
#include "twitch/IrcParser.h"

#ifndef _WIN32
#include <sys/select.h>
#endif

namespace vt {

namespace {

constexpr const char* kUrl = "wss://irc-ws.chat.twitch.tv:443";
constexpr auto kPingInterval = std::chrono::minutes(4);
constexpr auto kDeadAfter = std::chrono::minutes(7);
constexpr auto kStableAfter = std::chrono::seconds(60);

std::string normalizeChannel(std::string c) {
  c = toLowerAscii(trim(c));
  if (!c.empty() && c[0] == '#') c.erase(0, 1);
  return c;
}

}  // namespace

TwitchIrcClient::~TwitchIrcClient() { disconnect(); }

void TwitchIrcClient::setSink(Sink sink) {
  std::lock_guard lock(mutex_);
  sink_ = std::move(sink);
}

std::string TwitchIrcClient::statusText() const {
  std::lock_guard lock(mutex_);
  return statusText_;
}

std::string TwitchIrcClient::channel() const {
  std::lock_guard lock(mutex_);
  return channel_;
}

void TwitchIrcClient::setState(State s, std::string text) {
  state_ = s;
  {
    std::lock_guard lock(mutex_);
    statusText_ = std::move(text);
  }
  if (onChange_) onChange_();
}

void TwitchIrcClient::connect(const std::string& login, const std::string& token, const std::string& channel) {
  disconnect();
  {
    std::lock_guard lock(mutex_);
    login_ = toLowerAscii(login);
    token_ = token;
    channel_ = normalizeChannel(channel.empty() ? login : channel);
  }
  stop_ = false;
  thread_ = std::thread([this] { run(); });
}

void TwitchIrcClient::disconnect() {
  stop_ = true;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  if (state_ != State::AuthFailed) setState(State::Disconnected, "Not connected");
}

bool TwitchIrcClient::waitInterruptible(std::chrono::milliseconds d) {
  std::unique_lock lock(mutex_);
  return !cv_.wait_for(lock, d, [this] { return stop_.load(); });
}

void TwitchIrcClient::run() {
  int attempt = 0;
  while (!stop_) {
    bool stable = false;
    bool retry = session(stable);
    if (stop_ || !retry) break;
    if (stable) attempt = 0;
    // Exponential backoff: 1, 2, 4, ... 60s. Keeps us well inside Twitch's
    // 20-logins-per-10s limit even when the network flaps.
    auto delay = std::chrono::seconds(std::min(60, 1 << std::min(attempt, 6)));
    ++attempt;
    setState(State::Reconnecting, "Reconnecting in " + std::to_string(delay.count()) + "s");
    log::warn("Twitch chat disconnected; reconnecting in {}s", delay.count());
    if (!waitInterruptible(delay)) break;
  }
}

bool TwitchIrcClient::sendLine(void* handle, const std::string& line) {
  auto* curl = static_cast<CURL*>(handle);
  std::string data = line + "\r\n";
  size_t offset = 0;
  for (int tries = 0; offset < data.size() && tries < 200; ++tries) {
    size_t sent = 0;
    CURLcode rc = curl_ws_send(curl, data.data() + offset, data.size() - offset, &sent, 0, CURLWS_TEXT);
    if (rc == CURLE_OK) {
      offset += sent;
    } else if (rc == CURLE_AGAIN) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } else {
      log::warn("IRC send failed: {}", curl_easy_strerror(rc));
      return false;
    }
  }
  return offset == data.size();
}

bool TwitchIrcClient::session(bool& wasStable) {
  std::string login, token, channel;
  {
    std::lock_guard lock(mutex_);
    login = login_;
    token = token_;
    channel = channel_;
  }
  setState(State::Connecting, "Connecting to Twitch chat...");

  CURL* curl = curl_easy_init();
  if (!curl) return true;
  curl_easy_setopt(curl, CURLOPT_URL, kUrl);
  curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);  // WebSocket upgrade, then manual I/O
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "VoiceTwitch/" VT_VERSION);
#ifdef _WIN32
  curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
#endif
  CURLcode rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    log::warn("Twitch chat connect failed: {}", curl_easy_strerror(rc));
    curl_easy_cleanup(curl);
    return true;
  }

  curl_socket_t sock = CURL_SOCKET_BAD;
  curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &sock);

  bool ok = sendLine(curl, "CAP REQ :twitch.tv/tags twitch.tv/commands") &&
            sendLine(curl, "PASS oauth:" + token) && sendLine(curl, "NICK " + login) &&
            sendLine(curl, "JOIN #" + channel);
  if (!ok) {
    curl_easy_cleanup(curl);
    return true;
  }

  const auto connectedAt = std::chrono::steady_clock::now();
  auto lastRx = connectedAt;
  auto lastPing = connectedAt;
  std::string pending;
  bool reconnect = false;
  bool closed = false;

  while (!stop_ && !reconnect && !closed && state_ != State::AuthFailed) {
    // Drain everything curl has buffered (TLS may hold data the socket
    // readiness check can't see).
    while (true) {
      char buf[8192];
      size_t n = 0;
      const curl_ws_frame* meta = nullptr;
      rc = curl_ws_recv(curl, buf, sizeof(buf), &n, &meta);
      if (rc == CURLE_AGAIN) break;
      if (rc != CURLE_OK) {
        log::warn("Twitch chat read error: {}", curl_easy_strerror(rc));
        closed = true;
        break;
      }
      lastRx = std::chrono::steady_clock::now();
      if (meta && (meta->flags & CURLWS_CLOSE)) {
        closed = true;
        break;
      }
      if (meta && (meta->flags & (CURLWS_TEXT | CURLWS_CONT | CURLWS_BINARY))) pending.append(buf, n);
      size_t pos;
      while ((pos = pending.find("\r\n")) != std::string::npos) {
        std::string line = pending.substr(0, pos);
        pending.erase(0, pos + 2);
        handleLine(line, curl, reconnect);
      }
    }
    if (closed || reconnect) break;

    auto now = std::chrono::steady_clock::now();
    if (now - lastRx > kDeadAfter) {
      log::warn("Twitch chat connection timed out");
      break;
    }
    if (now - lastPing > kPingInterval) {
      sendLine(curl, "PING :tmi.twitch.tv");
      lastPing = now;
    }
    if (!wasStable && state_ == State::Joined && now - connectedAt > kStableAfter) wasStable = true;

    // Sleep until the socket has data (or 500ms, to notice stop requests).
    if (sock != CURL_SOCKET_BAD) {
      fd_set rf;
      FD_ZERO(&rf);
      FD_SET(sock, &rf);
      timeval tv{0, 500000};
      select(static_cast<int>(sock) + 1, &rf, nullptr, nullptr, &tv);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }

  if (!closed) {
    size_t sent = 0;
    curl_ws_send(curl, "", 0, &sent, 0, CURLWS_CLOSE);
  }
  curl_easy_cleanup(curl);

  if (state_ == State::AuthFailed) return false;
  if (stop_) return false;
  if (reconnect) wasStable = true;  // server-requested: reconnect immediately-ish
  return true;
}

void TwitchIrcClient::handleLine(const std::string& line, void* curl, bool& reconnectRequested) {
  auto msg = irc::parseLine(line);
  if (!msg) return;
  const auto& cmd = msg->command;

  if (cmd == "PING") {
    sendLine(curl, "PONG :" + msg->trailing());
  } else if (cmd == "001") {
    setState(State::Connected, "Logged in, joining channel...");
  } else if (cmd == "JOIN") {
    std::string login, channel;
    {
      std::lock_guard lock(mutex_);
      login = login_;
      channel = channel_;
    }
    if (toLowerAscii(msg->nick()) == login) {
      setState(State::Joined, "Reading #" + channel);
      log::info("Joined #{}", channel);
    }
  } else if (cmd == "PRIVMSG" && msg->params.size() >= 2) {
    ChatMessage m;
    m.channel = msg->params[0];
    if (!m.channel.empty() && m.channel[0] == '#') m.channel.erase(0, 1);
    m.userLogin = msg->nick();
    m.displayName = msg->tag("display-name");
    m.text = msg->trailing();
    ++received_;
    Sink sink;
    {
      std::lock_guard lock(mutex_);
      sink = sink_;
    }
    if (sink) sink(std::move(m));
  } else if (cmd == "NOTICE") {
    const auto& text = msg->trailing();
    log::warn("Twitch NOTICE: {}", text);
    if (text.find("Login authentication failed") != std::string::npos ||
        text.find("Improperly formatted auth") != std::string::npos) {
      setState(State::AuthFailed, "Twitch rejected the login token - log in again");
    }
  } else if (cmd == "RECONNECT") {
    log::info("Twitch requested reconnect");
    reconnectRequested = true;
  }
}

}  // namespace vt
