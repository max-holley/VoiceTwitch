#include "ui/Dashboard.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/App.h"
#include "core/Log.h"

namespace vt::ui {

namespace {

enum class Screen { Activity, Voices, Cloning, Twitch, Filters, Settings, Log };

Screen g_screen = Screen::Activity;

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

struct NavEntry {
  const char* label;
  Screen screen;
};

constexpr NavEntry kNav[] = {
    {"Activity", Screen::Activity},        {"Voice Models", Screen::Voices}, {"Voice Cloning", Screen::Cloning},
    {"Twitch Connection", Screen::Twitch}, {"Filters", Screen::Filters},     {"Settings", Screen::Settings},
    {"Log", Screen::Log},
};

// Navigation list with a highlight pill that glides to the selected entry.
void drawNav() {
  static float pillY = -1.0f;
  const float itemH = ImGui::GetTextLineHeight() * 2.0f;
  const float gap = ImGui::GetStyle().ItemSpacing.y * 0.5f;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  ImDrawList* dl = ImGui::GetWindowDrawList();

  int selected = 0;
  for (int i = 0; i < static_cast<int>(std::size(kNav)); ++i) {
    if (kNav[i].screen == g_screen) selected = i;
  }
  const float targetY = origin.y + static_cast<float>(selected) * (itemH + gap);
  pillY = pillY < 0 ? targetY : animateTo(pillY, targetY, 18.0f);
  const float r = itemH * 0.3f;
  dl->AddRectFilled(ImVec2(origin.x, pillY), ImVec2(origin.x + width, pillY + itemH),
                    ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.17f)), r);
  dl->AddRectFilled(ImVec2(origin.x + 2, pillY + itemH * 0.24f), ImVec2(origin.x + 5, pillY + itemH * 0.76f),
                    ImGui::GetColorU32(kAccent), 2.0f);

  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.055f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1, 1, 1, 0.09f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, gap));
  for (int i = 0; i < static_cast<int>(std::size(kNav)); ++i) {
    ImGui::PushID(i);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##nav", false, 0, ImVec2(0, itemH))) g_screen = kNav[i].screen;
    const bool sel = kNav[i].screen == g_screen;
    const ImVec4 text = sel ? ImVec4(1, 1, 1, 1) : (ImGui::IsItemHovered() ? ImVec4(0.9f, 0.9f, 0.95f, 1) : kTextDim);
    dl->AddText(ImVec2(p.x + ImGui::GetFontSize() * 1.0f, p.y + (itemH - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::GetColorU32(text), kNav[i].label);
    ImGui::PopID();
  }
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);
}

void drawStatusBar(App& app) {
  auto st = app.pipeline().status();
  auto ircState = app.irc().state();
  bool chatOk = ircState == TwitchIrcClient::State::Joined;
  statusDot(chatOk, ircState == TwitchIrcClient::State::Connecting || ircState == TwitchIrcClient::State::Reconnecting ||
                        ircState == TwitchIrcClient::State::Connected);
  ImGui::SameLine();
  ImGui::TextUnformatted(app.irc().statusText().c_str());
  ImGui::SameLine(0, 28);
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
    badge("Reading", kOk);
  } else {
    badge("Paused", kWarn);
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
  ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.6f));
  ImGui::PushStyleColor(ImGuiCol_Text, kAccentHover);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  // Hairline that fills the rest of the row.
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const float x0 = max.x + ImGui::GetFontSize() * 0.7f;
  const float x1 = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
  const float y = (min.y + max.y) * 0.5f;
  if (x1 > x0) {
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, y), ImVec2(x1, y), ImGui::GetColorU32(ImGuiCol_Separator));
  }
  ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.2f));
}

void statusDot(bool ok, bool warn) {
  ImVec2 p = ImGui::GetCursorScreenPos();
  float h = ImGui::GetTextLineHeight();
  ImVec4 c = ok ? kOk : (warn ? kWarn : kBad);
  const ImVec2 centre(p.x + h * 0.4f, p.y + h * 0.55f);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float pulse = 0.0f;
  if (warn && !ok) {
    pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 5.0f);
    requestFrames();
  }
  dl->AddCircleFilled(centre, h * (0.46f + 0.12f * pulse), ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 0.20f)), 16);
  dl->AddCircleFilled(centre, h * 0.26f, ImGui::GetColorU32(c), 16);
  ImGui::Dummy(ImVec2(h * 0.8f, h));
}

void applyTheme(float scale) {
  ImGui::StyleColorsDark();
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 0.0f;
  s.ChildRounding = 10.0f;
  s.FrameRounding = 8.0f;
  s.GrabRounding = 10.0f;
  s.PopupRounding = 12.0f;
  s.TabRounding = 8.0f;
  s.ScrollbarRounding = 10.0f;
  s.FramePadding = ImVec2(14, 8);
  s.ItemSpacing = ImVec2(12, 10);
  s.ItemInnerSpacing = ImVec2(8, 6);
  s.WindowPadding = ImVec2(18, 14);
  s.CellPadding = ImVec2(10, 9);
  s.ScrollbarSize = 11.0f;
  s.GrabMinSize = 14.0f;
  s.FrameBorderSize = 1.0f;
  s.PopupBorderSize = 1.0f;
  s.ChildBorderSize = 1.0f;
  s.WindowBorderSize = 0.0f;
  s.IndentSpacing = 20.0f;

  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.97f, 1.0f);
  c[ImGuiCol_TextDisabled] = kTextDim;
  c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.075f, 0.094f, 1.0f);
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = ImVec4(0.115f, 0.115f, 0.145f, 0.99f);
  c[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.24f, 1.0f);
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = ImVec4(0.145f, 0.145f, 0.18f, 1.0f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.17f, 0.23f, 1.0f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.21f, 0.19f, 0.29f, 1.0f);
  c[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.25f, 1.0f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.29f, 0.28f, 0.37f, 1.0f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.17f, 0.17f, 0.22f, 1.0f);
  c[ImGuiCol_Header] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.25f);
  c[ImGuiCol_HeaderHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
  c[ImGuiCol_HeaderActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.50f);
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_SliderGrabActive] = kAccentHover;
  c[ImGuiCol_PlotHistogram] = kAccent;
  c[ImGuiCol_PlotHistogramHovered] = kAccentHover;
  c[ImGuiCol_TableHeaderBg] = ImVec4(0.125f, 0.125f, 0.155f, 1.0f);
  c[ImGuiCol_TableBorderStrong] = ImVec4(0.19f, 0.19f, 0.24f, 1.0f);
  c[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.15f, 0.19f, 1.0f);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.022f);
  c[ImGuiCol_Separator] = ImVec4(0.17f, 0.17f, 0.22f, 1.0f);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.26f, 0.26f, 0.33f, 1.0f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.33f, 0.44f, 1.0f);
  c[ImGuiCol_ScrollbarGrabActive] = kAccent;
  c[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.40f);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);

  s.ScaleAllSizes(scale);
}

void drawDashboard(App& app) {
  resetAnimationRequest();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
  ImGui::Begin("VoiceTwitch", nullptr, flags);

  const float statusH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
  const float navW = ImGui::GetFontSize() * 12.5f;

  // Sidebar: slightly lighter panel, no border.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.098f, 0.098f, 0.125f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.16f, 0.16f, 0.21f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetFontSize() * 0.8f);
  ImGui::BeginChild("nav", ImVec2(navW, -statusH), ImGuiChildFlags_Borders);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);

  ImGui::SetWindowFontScale(1.25f);
  ImGui::PushStyleColor(ImGuiCol_Text, kAccentHover);
  ImGui::TextUnformatted("VoiceTwitch");
  ImGui::PopStyleColor();
  ImGui::SetWindowFontScale(1.0f);
  ImGui::TextDisabled("v" VT_VERSION);
  ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
  drawNav();

  // Reading toggle always visible.
  ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - ImGui::GetStyle().WindowPadding.y);
  bool reading = app.config().readingEnabled;
  const bool clicked = reading ? dangerButton("Stop reading", ImVec2(-1, 0)) : primaryButton("Start reading", ImVec2(-1, 0));
  if (clicked) {
    app.config().readingEnabled = !reading;
    app.markConfigDirty();
    app.applySettings();
  }
  ImGui::EndChild();

  // Page transition: content fades in and slides up a few pixels whenever the
  // selected screen changes.
  static Screen shown = g_screen;
  static double switchedAt = -10.0;
  if (shown != g_screen) {
    shown = g_screen;
    switchedAt = ImGui::GetTime();
  }
  const float tAnim = static_cast<float>((ImGui::GetTime() - switchedAt) / 0.32);
  const float e = easeOutCubic(tAnim);
  if (tAnim < 1.0f) requestFrames();
  const float slide = (1.0f - e) * ImGui::GetFontSize() * 1.6f;

  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + slide);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(e, 0.001f));
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
  ImGui::PopStyleVar();

  ImGui::Separator();
  drawStatusBar(app);
  ImGui::End();
}

void drawActivityView(App& app) {
  static std::string testText = "Hello chat, this is a test message!";
  auto st = app.pipeline().status();

  pageTitle("Activity", "Live view of what's being read out");

  const bool joined = app.irc().state() == TwitchIrcClient::State::Joined;
  if (ImGui::BeginTable("stats", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
    ImGui::TableNextColumn();
    statCard("stat_chat", "CHAT", joined ? ("#" + app.irc().channel()).c_str() : "Offline", joined ? kOk : kWarn,
             joined ? "Connected" : "Not connected");
    ImGui::TableNextColumn();
    statCard("stat_queued", "QUEUED", std::to_string(st.queued).c_str(), ImGui::GetStyle().Colors[ImGuiCol_Text],
             "Waiting to be spoken");
    ImGui::TableNextColumn();
    statCard("stat_spoken", "SPOKEN", std::to_string(st.spoken).c_str(), kOk, "This session");
    ImGui::TableNextColumn();
    statCard("stat_filtered", "FILTERED", std::to_string(st.filtered).c_str(),
             st.filtered ? kBad : ImGui::GetStyle().Colors[ImGuiCol_Text], "Blocked by your filters");
    ImGui::EndTable();
  }

  beginCard("now", ImGui::GetTextLineHeight() * 5.2f);
  if (st.speaking) {
    spinner(ImGui::GetTextLineHeight() * 0.5f, ImGui::GetTextLineHeight() * 0.14f, kAccentHover);
    ImGui::SameLine();
    ImGui::TextColored(kAccentHover, "Speaking now");
    ImGui::SameLine();
    ImGui::ProgressBar(app.player().outputLevel(), ImVec2(ImGui::GetFontSize() * 7, ImGui::GetTextLineHeight() * 0.7f), "");
    ImGui::TextWrapped("%s", st.currentText.c_str());
  } else {
    ImGui::TextColored(kTextDim, "Idle - nothing is being spoken");
    if (st.lastSynthesisMs > 0) {
      ImGui::TextColored(kTextDim, "Last time-to-first-audio: %.0f ms", st.lastSynthesisMs);
    }
  }
  endCard();

  if (button("Skip current")) app.pipeline().skipCurrent();
  ImGui::SameLine();
  if (button("Clear queue")) app.pipeline().clearQueue();

  sectionHeader("Test");
  ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 13);
  ImGui::InputText("##test", &testText);
  ImGui::SameLine();
  if (primaryButton("Send as chat message")) {
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
  if (history.empty()) {
    ImGui::TextColored(kTextDim, "No messages yet. Connect to chat or send a test message above.");
    return;
  }
  if (ImGui::BeginTable("history", 4,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0, 0))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.5f);
    ImGui::TableSetupColumn("User", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9);
    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.5f);
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
      badge(outcomeLabel(it->outcome), outcomeColor(it->outcome));
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

  pageTitle("Log", "Diagnostics from the app and its background tasks");
  toggle("Auto-scroll", &autoScroll);
  ImGui::SameLine(0, ImGui::GetFontSize() * 1.5f);
  toggle("Show debug", &showDebug);
  ImGui::SameLine(0, ImGui::GetFontSize() * 1.5f);
  if (button("Clear")) entries.clear();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.055f, 0.07f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 0.9f, ImGui::GetFontSize() * 0.7f));
  ImGui::BeginChild("logscroll", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, ImGui::GetStyle().ItemSpacing.y * 0.45f));
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
  ImGui::PopStyleVar();
  if (autoScroll && !fresh.empty()) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
}

}  // namespace vt::ui
