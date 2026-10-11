#include "twitch/TwitchAuth.h"

#include <nlohmann/json.hpp>

#include "core/Http.h"
#include "core/Log.h"
#include "core/Platform.h"
#include "core/SecureStore.h"
#include "core/StringUtil.h"
#include "twitch/LoopbackServer.h"

namespace vt {

namespace {

constexpr const char* kTokenKey = "twitch_oauth_token";
constexpr const char* kScopes = "chat:read";

// Served at the redirect URL. The token arrives in the URL fragment, which
// browsers never send to servers, so this page forwards it to /token.
const char* kCallbackPage = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>VoiceTwitch</title>
<style>body{font-family:system-ui,sans-serif;background:#18181b;color:#efeff1;display:flex;
align-items:center;justify-content:center;height:100vh;margin:0}div{max-width:420px;text-align:center}
h1{font-size:20px}</style></head>
<body><div><h1 id="t">Finishing login&hellip;</h1><p id="m"></p></div>
<script>
(function(){
  var h = window.location.hash.substring(1);
  var t = document.getElementById('t'), m = document.getElementById('m');
  if (!h) { t.textContent = 'Login failed'; m.textContent = 'No token was returned by Twitch.'; return; }
  fetch('/token?' + h).then(function(r){ return r.text(); }).then(function(x){
    t.textContent = x === 'ok' ? 'Logged in to VoiceTwitch' : 'Login failed';
    m.textContent = x === 'ok' ? 'You can close this tab and return to the app.' : x;
    history.replaceState(null, '', '/');
  }).catch(function(){ t.textContent = 'Login failed'; m.textContent = 'Could not reach VoiceTwitch.'; });
})();
</script></body></html>)HTML";

std::string messagePage(const std::string& title, const std::string& msg) {
  auto esc = [](const std::string& s) {
    std::string o;
    for (char c : s) {
      if (c == '<') o += "&lt;";
      else if (c == '>') o += "&gt;";
      else if (c == '&') o += "&amp;";
      else o.push_back(c);
    }
    return o;
  };
  return "<!doctype html><html><head><meta charset=\"utf-8\"><title>VoiceTwitch</title></head>"
         "<body style=\"font-family:system-ui;background:#18181b;color:#efeff1;text-align:center;padding-top:20vh\">"
         "<h1 style=\"font-size:20px\">" + esc(title) + "</h1><p>" + esc(msg) + "</p></body></html>";
}

}  // namespace

TwitchAuth::~TwitchAuth() {
  cancel_ = true;
  joinWorker();
}

void TwitchAuth::joinWorker() {
  if (worker_.joinable()) worker_.join();
}

void TwitchAuth::notify() {
  if (onChange_) onChange_();
}

void TwitchAuth::setError(std::string msg) {
  log::error("Twitch auth: {}", msg);
  {
    std::lock_guard lock(mutex_);
    error_ = std::move(msg);
  }
  state_ = State::Error;
  notify();
}

std::string TwitchAuth::error() const {
  std::lock_guard lock(mutex_);
  return error_;
}

std::string TwitchAuth::login() const {
  std::lock_guard lock(mutex_);
  return login_;
}

std::string TwitchAuth::authorizeUrl() const {
  std::lock_guard lock(mutex_);
  return authorizeUrl_;
}

std::string TwitchAuth::token() const {
  std::lock_guard lock(mutex_);
  return state_ == State::LoggedIn ? token_ : std::string{};
}

std::string TwitchAuth::buildAuthorizeUrl(const std::string& clientId, int port, const std::string& state) {
  return "https://id.twitch.tv/oauth2/authorize?response_type=token&client_id=" + urlEncode(clientId) +
         "&redirect_uri=" + urlEncode("http://localhost:" + std::to_string(port)) + "&scope=" + urlEncode(kScopes) +
         "&state=" + urlEncode(state) + "&force_verify=true";
}

std::optional<TokenInfo> TwitchAuth::validate(const std::string& token, std::string& error) {
  auto r = http::get("https://id.twitch.tv/oauth2/validate", {"Authorization: OAuth " + token});
  if (r.status == 401) {
    error = "token is invalid or expired";
    return std::nullopt;
  }
  if (!r.ok()) {
    error = r.error.empty() ? "validate returned HTTP " + std::to_string(r.status) : r.error;
    return std::nullopt;
  }
  try {
    auto j = nlohmann::json::parse(r.body);
    TokenInfo info;
    info.login = j.value("login", "");
    info.userId = j.value("user_id", "");
    info.clientId = j.value("client_id", "");
    info.expiresIn = j.value("expires_in", int64_t{0});
    if (info.login.empty()) {
      error = "token has no associated user";
      return std::nullopt;
    }
    return info;
  } catch (const std::exception& e) {
    error = std::string("bad validate response: ") + e.what();
    return std::nullopt;
  }
}

void TwitchAuth::beginLogin(const std::string& clientId, int port) {
  if (clientId.empty()) {
    setError("Enter your Twitch application's Client ID first");
    return;
  }
  cancel_ = true;
  joinWorker();
  cancel_ = false;
  state_ = State::WaitingForBrowser;
  {
    std::lock_guard lock(mutex_);
    error_.clear();
  }
  notify();
  worker_ = std::thread([this, clientId, port] { loginThread(clientId, port); });
}

void TwitchAuth::cancelLogin() {
  cancel_ = true;
  joinWorker();
  if (state_ == State::WaitingForBrowser) state_ = State::LoggedOut;
  notify();
}

void TwitchAuth::loginThread(std::string clientId, int port) {
  LoopbackServer server;
  std::string err;
  if (!server.listen(port, err)) {
    setError("Cannot start login listener: " + err);
    return;
  }
  const std::string state = randomHex(16);
  const std::string url = buildAuthorizeUrl(clientId, port, state);
  {
    std::lock_guard lock(mutex_);
    authorizeUrl_ = url;
    clientId_ = clientId;
  }
  log::info("Opening browser for Twitch login");
  if (!platform::openUrl(url)) log::warn("Could not open browser; copy the login URL from the Twitch page");

  std::string receivedToken;
  std::string failure;
  bool done = server.serve(
      [&](const LoopbackServer::Request& req, bool& finished) {
        LoopbackServer::Reply reply;
        if (req.path == "/" || req.path == "/callback") {
          if (auto e = req.query.find("error"); e != req.query.end()) {
            auto d = req.query.find("error_description");
            failure = d != req.query.end() ? d->second : e->second;
            finished = true;
            reply.body = messagePage("Login cancelled", failure);
          } else {
            reply.body = kCallbackPage;
          }
        } else if (req.path == "/token") {
          reply.contentType = "text/plain; charset=utf-8";
          auto st = req.query.find("state");
          auto tok = req.query.find("access_token");
          if (st == req.query.end() || st->second != state) {
            reply.status = 400;
            reply.body = "State mismatch - please retry the login from the app.";
          } else if (tok == req.query.end() || tok->second.empty()) {
            reply.status = 400;
            reply.body = "No access token in redirect.";
          } else {
            receivedToken = tok->second;
            reply.body = "ok";
            finished = true;
          }
        } else {
          reply.status = 404;
          reply.body = "not found";
        }
        return reply;
      },
      cancel_, std::chrono::minutes(5));
  server.close();

  if (cancel_) return;
  if (!done) {
    setError("Login timed out");
    return;
  }
  if (!failure.empty()) {
    setError("Login cancelled: " + failure);
    return;
  }
  finishWithToken(receivedToken);
}

void TwitchAuth::finishWithToken(const std::string& token) {
  state_ = State::Validating;
  notify();
  std::string err;
  auto info = validate(token, err);
  if (!info) {
    setError("Token validation failed: " + err);
    return;
  }
  if (!secure::store(kTokenKey, token)) log::warn("Could not save token to {}", secure::backendName());
  {
    std::lock_guard lock(mutex_);
    token_ = token;
    login_ = info->login;
    error_.clear();
    lastValidated_ = std::chrono::steady_clock::now();
  }
  state_ = State::LoggedIn;
  log::info("Logged in to Twitch as {}", info->login);
  notify();
}

void TwitchAuth::restore() {
  auto stored = secure::load(kTokenKey);
  if (!stored || stored->empty()) return;
  cancel_ = true;
  joinWorker();
  cancel_ = false;
  worker_ = std::thread([this, token = *stored] {
    state_ = State::Validating;
    notify();
    std::string err;
    auto info = validate(token, err);
    if (!info) {
      if (err == "token is invalid or expired") {
        secure::remove(kTokenKey);
        setError("Saved Twitch login expired - please log in again");
      } else {
        // Offline or Twitch unreachable: keep the token and retry later.
        setError("Could not validate saved login: " + err);
      }
      return;
    }
    {
      std::lock_guard lock(mutex_);
      token_ = token;
      login_ = info->login;
      lastValidated_ = std::chrono::steady_clock::now();
    }
    state_ = State::LoggedIn;
    log::info("Restored Twitch login for {}", info->login);
    notify();
  });
}

void TwitchAuth::validateIfDue() {
  if (state_ != State::LoggedIn) return;
  {
    std::lock_guard lock(mutex_);
    if (std::chrono::steady_clock::now() - lastValidated_ < std::chrono::hours(1)) return;
    lastValidated_ = std::chrono::steady_clock::now();
  }
  joinWorker();
  worker_ = std::thread([this] {
    std::string tok;
    {
      std::lock_guard lock(mutex_);
      tok = token_;
    }
    std::string err;
    if (!validate(tok, err) && err == "token is invalid or expired") {
      secure::remove(kTokenKey);
      {
        std::lock_guard lock(mutex_);
        token_.clear();
      }
      setError("Twitch login expired - please log in again");
    }
  });
}

void TwitchAuth::logout() {
  cancel_ = true;
  joinWorker();
  std::string tok, clientId;
  {
    std::lock_guard lock(mutex_);
    tok = token_;
    clientId = clientId_;
    token_.clear();
    login_.clear();
    error_.clear();
  }
  if (tok.empty()) {
    if (auto stored = secure::load(kTokenKey)) tok = *stored;
  }
  secure::remove(kTokenKey);
  state_ = State::LoggedOut;
  notify();
  if (!tok.empty()) {
    // Revoke in the background; failure is harmless.
    std::thread([tok, clientId] {
      std::string cid = clientId;
      if (cid.empty()) {
        std::string err;
        if (auto info = validate(tok, err)) cid = info->clientId;
      }
      if (!cid.empty()) http::postForm("https://id.twitch.tv/oauth2/revoke", "client_id=" + urlEncode(cid) + "&token=" + urlEncode(tok));
    }).detach();
  }
}

}  // namespace vt
