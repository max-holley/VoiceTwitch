#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/App.h"
#include "core/Platform.h"
#include "core/SecureStore.h"
#include "ui/Dashboard.h"

namespace vt::ui {

namespace {

const char* authStateText(TwitchAuth::State s) {
  switch (s) {
    case TwitchAuth::State::LoggedOut: return "Not logged in";
    case TwitchAuth::State::WaitingForBrowser: return "Waiting for you to approve in the browser...";
    case TwitchAuth::State::Validating: return "Checking token...";
    case TwitchAuth::State::LoggedIn: return "Logged in";
    case TwitchAuth::State::Error: return "Login problem";
  }
  return "";
}

}  // namespace

void drawTwitchView(App& app) {
  static std::string channelEdit;
  static bool channelInit = false;
  auto& cfg = app.config();
  auto& auth = app.auth();
  auto authState = auth.state();

  ImGui::Text("Twitch Connection");
  ImGui::Separator();
  ImGui::BeginChild("twitch_scroll");

  sectionHeader("Account");
  statusDot(authState == TwitchAuth::State::LoggedIn,
            authState == TwitchAuth::State::WaitingForBrowser || authState == TwitchAuth::State::Validating);
  ImGui::SameLine();
  if (authState == TwitchAuth::State::LoggedIn) {
    ImGui::Text("Logged in as %s", auth.login().c_str());
  } else {
    ImGui::TextUnformatted(authStateText(authState));
  }
  if (authState == TwitchAuth::State::Error) {
    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1), "%s", auth.error().c_str());
  }

  if (authState == TwitchAuth::State::LoggedIn) {
    if (ImGui::Button("Log out")) {
      app.disconnectChat();
      auth.logout();
    }
  } else if (authState == TwitchAuth::State::WaitingForBrowser) {
    if (ImGui::Button("Cancel")) auth.cancelLogin();
    ImGui::SameLine();
    if (ImGui::Button("Copy login link")) ImGui::SetClipboardText(auth.authorizeUrl().c_str());
    helpMarker("If the browser didn't open, paste this link into it.");
  } else {
    ImGui::BeginDisabled(cfg.twitchClientId.empty() || authState == TwitchAuth::State::Validating);
    if (ImGui::Button("Log in with Twitch")) auth.beginLogin(cfg.twitchClientId, cfg.twitchRedirectPort);
    ImGui::EndDisabled();
  }
  ImGui::TextDisabled("Scope requested: chat:read (read-only). Token stored in: %s", secure::backendName());

  sectionHeader("Chat");
  if (!channelInit) {
    channelEdit = cfg.twitchChannel;
    channelInit = true;
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
  ImGui::InputTextWithHint("Channel", auth.login().empty() ? "your channel" : auth.login().c_str(), &channelEdit);
  helpMarker("Channel whose chat is read aloud. Leave empty to use your own.");
  ImGui::SameLine();
  if (ImGui::Button("Apply")) {
    cfg.twitchChannel = channelEdit;
    app.markConfigDirty();
    if (app.irc().state() != TwitchIrcClient::State::Disconnected) app.connectChat();
  }

  auto ircState = app.irc().state();
  statusDot(ircState == TwitchIrcClient::State::Joined, ircState == TwitchIrcClient::State::Connecting ||
                                                            ircState == TwitchIrcClient::State::Connected ||
                                                            ircState == TwitchIrcClient::State::Reconnecting);
  ImGui::SameLine();
  ImGui::TextUnformatted(app.irc().statusText().c_str());
  ImGui::TextDisabled("Messages received this session: %llu",
                      static_cast<unsigned long long>(app.irc().messagesReceived()));

  ImGui::BeginDisabled(authState != TwitchAuth::State::LoggedIn);
  if (ircState == TwitchIrcClient::State::Disconnected || ircState == TwitchIrcClient::State::AuthFailed) {
    if (ImGui::Button("Connect to chat")) app.connectChat();
  } else {
    if (ImGui::Button("Disconnect")) app.disconnectChat();
  }
  ImGui::EndDisabled();
  if (ImGui::Checkbox("Connect automatically on startup", &cfg.autoConnect)) app.markConfigDirty();

  sectionHeader("Twitch application (one-time setup)");
  ImGui::TextWrapped(
      "VoiceTwitch logs in through your own Twitch application so no secrets ship with the app. "
      "Register one at dev.twitch.tv/console/apps (Category: Chat Bot, Client Type: Public) with this exact "
      "OAuth Redirect URL, then paste its Client ID below.");
  std::string redirect = "http://localhost:" + std::to_string(cfg.twitchRedirectPort);
  ImGui::Text("Redirect URL: %s", redirect.c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(redirect.c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("Open Twitch console")) platform::openUrl("https://dev.twitch.tv/console/apps");

  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22);
  if (ImGui::InputText("Client ID", &cfg.twitchClientId, ImGuiInputTextFlags_CharsNoBlank)) app.markConfigDirty();
  int port = cfg.twitchRedirectPort;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
  if (ImGui::InputInt("Redirect port", &port, 0, 0) && port >= 1024 && port <= 65535) {
    cfg.twitchRedirectPort = port;
    app.markConfigDirty();
  }
  helpMarker("Must match the port in the Redirect URL registered with your Twitch application.");

  ImGui::EndChild();
}

}  // namespace vt::ui
