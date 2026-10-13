#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/App.h"
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

  pageTitle("Twitch Connection", "Sign in and choose the chat to read");
  ImGui::BeginChild("twitch_scroll");

  sectionHeader("Account");
  beginCard("card_account");
  statusDot(authState == TwitchAuth::State::LoggedIn,
            authState == TwitchAuth::State::WaitingForBrowser || authState == TwitchAuth::State::Validating);
  ImGui::SameLine();
  if (authState == TwitchAuth::State::LoggedIn) {
    ImGui::Text("Logged in as %s", auth.login().c_str());
  } else {
    ImGui::TextUnformatted(authStateText(authState));
  }
  if (authState == TwitchAuth::State::WaitingForBrowser || authState == TwitchAuth::State::Validating) {
    ImGui::SameLine();
    spinner(ImGui::GetTextLineHeight() * 0.45f, ImGui::GetTextLineHeight() * 0.13f, kAccentHover);
  }
  if (authState == TwitchAuth::State::Error) {
    ImGui::TextColored(kBad, "%s", auth.error().c_str());
  }

  if (authState == TwitchAuth::State::LoggedIn) {
    if (button("Log out")) {
      app.disconnectChat();
      auth.logout();
    }
  } else if (authState == TwitchAuth::State::WaitingForBrowser) {
    if (button("Cancel")) auth.cancelLogin();
    ImGui::SameLine();
    if (button("Copy login link")) ImGui::SetClipboardText(auth.authorizeUrl().c_str());
    helpMarker("If the browser didn't open, paste this link into it.");
  } else {
    ImGui::BeginDisabled(authState == TwitchAuth::State::Validating);
    if (primaryButton("Log in with Twitch")) auth.beginLogin(kTwitchClientId, kTwitchRedirectPort);
    ImGui::EndDisabled();
  }
  ImGui::TextDisabled("Scope requested: chat:read (read-only). Token stored in: %s", secure::backendName());
  endCard();

  sectionHeader("Chat");
  beginCard("card_chat");
  if (!channelInit) {
    channelEdit = cfg.twitchChannel;
    channelInit = true;
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
  ImGui::InputTextWithHint("Channel", auth.login().empty() ? "your channel" : auth.login().c_str(), &channelEdit);
  helpMarker("Channel whose chat is read aloud. Leave empty to use your own.");
  ImGui::SameLine();
  if (primaryButton("Apply")) {
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
    if (primaryButton("Connect to chat")) app.connectChat();
  } else {
    if (button("Disconnect")) app.disconnectChat();
  }
  ImGui::EndDisabled();
  if (toggle("Connect automatically on startup", &cfg.autoConnect)) app.markConfigDirty();
  endCard();

  ImGui::EndChild();
}

}  // namespace vt::ui
