#include "ui/Dashboard.h"

#include <cstdio>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/App.h"
#include "core/Log.h"

namespace vt::ui {

namespace {

enum class Screen { Activity, Voices, Cloning, Twitch, Filters, Settings, Log };

Screen g_screen = Screen::Activity;

const ImVec4 kAccent{0.57f, 0.27f, 1.00f, 1.00f};  // Twitch purple
const ImVec4 kOk{0.30f, 0.80f, 0.45f, 1.0f};
const ImVec4 kWarn{0.95f, 0.70f, 0.20f, 1.0f};
const ImVec4 kBad{0.90f, 0.30f, 0.30f, 1.0f};

const char* outcomeLabel(MessageOutcome o) {
  switch (o) {
    case MessageOutcome::Queued: return "queued";
    case MessageOutcome::Spoken: return "spoken";
    case MessageOutcome::Filtered: return "filtered";
    case MessageOutcome::Ignored: return "ignored";
    case MessageOutcome::Paused: return "paused";
    case MessageOutcome::Skipped: return "skipped";
    case MessageOutcome::Failed: return "failed";
  }
  return "";
}

ImVec4 outcomeColor(MessageOutcome o) {
  switch (o) {
    case MessageOutcome::Spoken: return kOk;
    case MessageOutcome::Filtered:
    case MessageOutcome::Failed: return kBad;
    case MessageOutcome::Queued: return kAccent;
    default: return ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
  }
}

void navItem(const char* label, Screen s) {
  bool selected = g_screen == s;
  if (selected) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f));
  if (ImGui::Selectable(label, selected, 0, ImVec2(0, ImGui::GetTextLineHeight() * 1.9f))) g_screen = s;
  if (selected) ImGui::PopStyleColor();
}

void drawStatusBar(App& app) {
  auto st = app.pipeline().status();
  auto ircState = app.irc().state();
  bool chatOk = ircState == TwitchIrcClient::State::Joined;
  statusDot(chatOk, ircState == TwitchIrcClient::State::Connecting || ircState == TwitchIrcClient::State::Reconnecting ||
                        ircState == TwitchIrcClient::State::Connected);
  ImGui::SameLine();
  ImGui::TextUnformatted(app.irc().statusText().c_str());
  ImGui::SameLine(0, 24);
  statusDot(st.voiceState == VoiceState::Ready, st.voiceState == VoiceState::Loading);
  ImGui::SameLine();
  switch (st.voiceState) {
    case VoiceState::None: ImGui::TextUnformatted("No voice"); break;
    case VoiceState::Loading: ImGui::Text("Loading %s...", st.voiceMessage.c_str()); break;
    case VoiceState::Ready: ImGui::Text("Voice: %s", st.voiceMessage.c_str()); break;
    case VoiceState::Error: ImGui::TextColored(kBad, "Voice error"); break;
  }
  ImGui::SameLine(0, 24);
  ImGui::Text("Queue: %zu", st.queued);
  ImGui::SameLine(0, 24);
  if (app.config().readingEnabled) {
    ImGui::TextColored(kOk, "Reading ON");
  } else {
    ImGui::TextColored(kWarn, "Reading paused");
  }
}

}  // namespace

std::string formatBytes(uint64_t b) {
  char buf[32];
  if (b >= 1024ull * 1024 * 1024) {
    std::snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(b) / (1024.0 * 1024 * 1024));
  } else if (b >= 1024 * 1024) {
    std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(b) / (1024.0 * 1024));
  } else if (b >= 1024) {
    std::snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(b) / 1024.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(b));
  }
  return buf;
}

void helpMarker(const char* text) {
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void sectionHeader(const char* text) {
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  ImGui::Separator();
  ImGui::Spacing();
}

void statusDot(bool ok, bool warn) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  float h = ImGui::GetTextLineHeight();
  ImVec4 c = ok ? kOk : (warn ? kWarn : kBad);
  ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + h * 0.4f, p.y + h * 0.55f), h * 0.3f,
                                              ImGui::ColorConvertFloat4ToU32(c));
  ImGui::Dummy(ImVec2(h * 0.8f, h));
}

void applyTheme(float scale) {
  ImGui::StyleColorsDark();
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 0.0f;
  s.ChildRounding = 6.0f;
  s.FrameRounding = 5.0f;
  s.GrabRounding = 5.0f;
  s.PopupRounding = 6.0f;
  s.TabRounding = 5.0f;
  s.FramePadding = ImVec2(10, 6);
  s.ItemSpacing = ImVec2(10, 8);
  s.WindowPadding = ImVec2(14, 12);
  s.ScrollbarSize = 12.0f;

  ImVec4* c = s.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.0f);
  c[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
  c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.98f);
  c[ImGuiCol_Border] = ImVec4(0.22f, 0.22f, 0.25f, 1.0f);
  c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.17f, 0.19f, 1.0f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.20f, 0.27f, 1.0f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.22f, 0.34f, 1.0f);
  c[ImGuiCol_Button] = ImVec4(0.40f, 0.20f, 0.75f, 0.85f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.50f, 0.28f, 0.90f, 1.0f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.36f, 0.16f, 0.68f, 1.0f);
  c[ImGuiCol_Header] = ImVec4(0.30f, 0.22f, 0.45f, 0.55f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.40f, 0.26f, 0.65f, 0.70f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.28f, 0.75f, 0.85f);
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_SliderGrabActive] = ImVec4(0.65f, 0.40f, 1.0f, 1.0f);
  c[ImGuiCol_PlotHistogram] = kAccent;
  c[ImGuiCol_TableHeaderBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.0f);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
  c[ImGuiCol_Separator] = ImVec4(0.22f, 0.22f, 0.25f, 1.0f);

  s.ScaleAllSizes(scale);
}

void drawDashboard(App& app) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
  ImGui::Begin("VoiceTwitch", nullptr, flags);

  const float statusH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
  const float navW = ImGui::GetFontSize() * 12.0f;

  ImGui::BeginChild("nav", ImVec2(navW, -statusH), ImGuiChildFlags_Borders);
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::TextUnformatted("VoiceTwitch");
  ImGui::PopStyleColor();
  ImGui::TextDisabled("v" VT_VERSION);
  ImGui::Separator();
  navItem("Activity", Screen::Activity);
  navItem("Voice Models", Screen::Voices);
  navItem("Voice Cloning", Screen::Cloning);
  navItem("Twitch Connection", Screen::Twitch);
  navItem("Filters", Screen::Filters);
  navItem("Settings", Screen::Settings);
  navItem("Log", Screen::Log);

  // Reading toggle always visible.
  ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing() * 2.2f);
  ImGui::Separator();
  bool reading = app.config().readingEnabled;
  if (reading) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.22f, 0.22f, 1.0f));
  }
  if (ImGui::Button(reading ? "Stop reading" : "Start reading", ImVec2(-1, 0))) {
    app.config().readingEnabled = !reading;
    app.markConfigDirty();
    app.applySettings();
  }
  if (reading) ImGui::PopStyleColor(2);
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("content", ImVec2(0, -statusH), ImGuiChildFlags_None);
  switch (g_screen) {
    case Screen::Activity: drawActivityView(app); break;
    case Screen::Voices: drawVoiceModelsView(app); break;
    case Screen::Cloning: drawCloningView(app); break;
    case Screen::Twitch: drawTwitchView(app); break;
    case Screen::Filters: drawFiltersView(app); break;
    case Screen::Settings: drawSettingsView(app); break;
    case Screen::Log: drawLogView(app); break;
  }
  ImGui::EndChild();

  ImGui::Separator();
  drawStatusBar(app);
  ImGui::End();
}

void drawActivityView(App& app) {
  static std::string testText = "Hello chat, this is a test message!";
  auto st = app.pipeline().status();

  ImGui::Text("Activity");
  ImGui::Separator();

  if (ImGui::BeginTable("stats", 4, ImGuiTableFlags_SizingStretchSame)) {
    auto stat = [](const char* label, const std::string& value, ImVec4 color) {
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", label);
      ImGui::TextColored(color, "%s", value.c_str());
    };
    stat("Chat", app.irc().state() == TwitchIrcClient::State::Joined ? "#" + app.irc().channel() : "offline",
         app.irc().state() == TwitchIrcClient::State::Joined ? kOk : kWarn);
    stat("Queued", std::to_string(st.queued), ImGui::GetStyle().Colors[ImGuiCol_Text]);
    stat("Spoken", std::to_string(st.spoken), kOk);
    stat("Filtered", std::to_string(st.filtered), st.filtered ? kBad : ImGui::GetStyle().Colors[ImGuiCol_Text]);
    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::BeginChild("now", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 3.6f), ImGuiChildFlags_Borders);
  if (st.speaking) {
    ImGui::TextColored(kAccent, "Speaking");
    ImGui::SameLine();
    ImGui::ProgressBar(app.player().outputLevel(), ImVec2(ImGui::GetFontSize() * 6, ImGui::GetTextLineHeight()), "");
    ImGui::TextWrapped("%s", st.currentText.c_str());
  } else {
    ImGui::TextDisabled("Idle");
    if (st.lastSynthesisMs > 0) ImGui::TextDisabled("Last time-to-first-audio: %.0f ms", st.lastSynthesisMs);
  }
  ImGui::EndChild();

  if (ImGui::Button("Skip current")) app.pipeline().skipCurrent();
  ImGui::SameLine();
  if (ImGui::Button("Clear queue")) app.pipeline().clearQueue();

  sectionHeader("Test");
  ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 12);
  ImGui::InputText("##test", &testText);
  ImGui::SameLine();
  if (ImGui::Button("Send as chat message")) {
    ChatMessage m;
    m.source = "test";
    m.userLogin = "tester";
    m.displayName = "Tester";
    m.text = testText;
    app.pipeline().submit(std::move(m));
  }
  helpMarker("Runs through the full pipeline (filter, queue, voice) as if a viewer named Tester sent it.");

  sectionHeader("Recent messages");
  auto history = app.pipeline().history();
  if (ImGui::BeginTable("history", 4,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0, 0))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5);
    ImGui::TableSetupColumn("User", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9);
    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6);
    ImGui::TableHeadersRow();
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", it->time.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(it->who.c_str());
      ImGui::TableNextColumn();
      ImGui::TextWrapped("%s", it->text.c_str());
      ImGui::TableNextColumn();
      ImGui::TextColored(outcomeColor(it->outcome), "%s", outcomeLabel(it->outcome));
      if (!it->detail.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", it->detail.c_str());
    }
    ImGui::EndTable();
  }
}

void drawLogView(App&) {
  static std::vector<LogEntry> entries;
  static uint64_t lastSeq = 0;
  static bool autoScroll = true;
  static bool showDebug = false;
  auto fresh = log::since(lastSeq);
  for (auto& e : fresh) {
    lastSeq = e.seq;
    entries.push_back(std::move(e));
  }
  if (entries.size() > 2000) entries.erase(entries.begin(), entries.begin() + 500);

  ImGui::Text("Log");
  ImGui::SameLine();
  ImGui::Checkbox("Auto-scroll", &autoScroll);
  ImGui::SameLine();
  ImGui::Checkbox("Show debug", &showDebug);
  ImGui::SameLine();
  if (ImGui::Button("Clear")) entries.clear();
  ImGui::Separator();
  ImGui::BeginChild("logscroll", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  for (const auto& e : entries) {
    if (e.level == LogLevel::Debug && !showDebug) continue;
    ImVec4 col = e.level == LogLevel::Error  ? kBad
                 : e.level == LogLevel::Warn ? kWarn
                 : e.level == LogLevel::Debug ? ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]
                                              : ImGui::GetStyle().Colors[ImGuiCol_Text];
    ImGui::TextDisabled("%s", e.time.c_str());
    ImGui::SameLine();
    ImGui::TextColored(col, "%s", e.text.c_str());
  }
  if (autoScroll && !fresh.empty()) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
}

}  // namespace vt::ui
