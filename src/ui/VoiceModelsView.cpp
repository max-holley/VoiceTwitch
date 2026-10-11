#include <algorithm>
#include <map>
#include <set>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/App.h"
#include "core/Paths.h"
#include "core/Platform.h"
#include "core/StringUtil.h"
#include "ui/Dashboard.h"

namespace vt::ui {

namespace {

struct State {
  std::string previewText = "Hi chat! This is how I sound. Thanks for hanging out on stream today.";
  std::string search;
  std::string language = "en_US";
  std::string pendingDelete;  // voice id
  std::string cardTitle;
  std::string cardKey;        // catalog key for remote cards
  std::string cardText;       // local card text
  bool openCard = false;
};

State g;

bool containsI(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return true;
  return toLowerAscii(hay).find(toLowerAscii(needle)) != std::string::npos;
}

void modelCardPopup(App& app) {
  if (g.openCard) {
    ImGui::OpenPopup("Model card");
    g.openCard = false;
  }
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 40, ImGui::GetFontSize() * 28), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Model card", nullptr)) {
    ImGui::TextUnformatted(g.cardTitle.c_str());
    ImGui::TextDisabled("Check the license and dataset terms before using a voice on stream.");
    ImGui::Separator();
    std::string text = g.cardText;
    if (!g.cardKey.empty()) {
      auto card = app.models().modelCard(g.cardKey);
      text = !card ? "" : (card->empty() ? "Fetching..." : *card);
    }
    ImGui::BeginChild("cardtext", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::EndChild();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void installedSection(App& app) {
  sectionHeader("Installed voices");
  auto installed = app.models().installed();
  auto status = app.pipeline().status();
  const std::string& active = app.config().activeVoice;

  if (installed.empty()) {
    ImGui::TextWrapped("No voices installed yet. Download one below - en_US-lessac-medium or "
                       "en_US-amy-medium are good starting points.");
  } else if (ImGui::BeginTable("installed", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                   ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Voice", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.5f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 18);
    ImGui::TableHeadersRow();
    for (const auto& v : installed) {
      ImGui::PushID(v.id.c_str());
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(v.name.c_str());
      ImGui::TableNextColumn();
      ImGui::TextDisabled(v.cloned ? "Cloned" : "Piper");
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", formatBytes(v.sizeBytes).c_str());
      ImGui::TableNextColumn();
      if (v.id == active) {
        if (status.voiceState == VoiceState::Loading) ImGui::TextDisabled("Loading...");
        else if (status.voiceState == VoiceState::Error) ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1), "Error");
        else ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.45f, 1), "Active");
      }
      ImGui::TableNextColumn();
      ImGui::BeginDisabled(v.id == active);
      if (ImGui::SmallButton("Use")) app.setActiveVoice(v.id);
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::SmallButton("Preview")) app.pipeline().preview(g.previewText, v.onnxPath);
      ImGui::SameLine();
      if (!v.modelCardPath.empty() && ImGui::SmallButton("Model card")) {
        g.cardTitle = v.name;
        g.cardKey.clear();
        platform::readFile(v.modelCardPath, g.cardText);
        g.openCard = true;
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Delete")) {
        g.pendingDelete = v.id;
        ImGui::OpenPopup("Delete voice?");
      }
      if (ImGui::BeginPopupModal("Delete voice?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete %s?", v.name.c_str());
        if (v.cloned) ImGui::TextDisabled("Your recordings are kept; only the trained model is removed.");
        if (ImGui::Button("Delete")) {
          std::string err;
          if (v.id == active) app.setActiveVoice("");
          app.models().remove(v, err);
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (status.voiceState == VoiceState::Error) {
    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1), "Voice failed to load: %s", status.voiceMessage.c_str());
  }

  ImGui::Spacing();
  ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 9);
  ImGui::InputText("Preview text", &g.previewText);

  if (status.voiceState == VoiceState::Ready && status.voiceSpeakers > 1) {
    int sid = app.config().speakerId;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::SliderInt("Speaker", &sid, 0, status.voiceSpeakers - 1)) {
      app.config().speakerId = sid;
      app.markConfigDirty();
      app.applySettings();
    }
    helpMarker("This voice has multiple speakers. Pick one, then use Preview to hear it.");
  }

  if (ImGui::Button("Open models folder")) platform::openFolder(paths::modelsDir());
  ImGui::SameLine();
  if (ImGui::Button("Rescan")) app.models().refreshInstalled();
}

void catalogSection(App& app) {
  sectionHeader("Available to download");
  ImGui::TextWrapped("Voices come from the Piper project's public repository (huggingface.co/rhasspy/piper-voices). "
                     "Each voice has its own license - open its model card before using it.");

  auto catalog = app.models().catalog();
  std::set<std::string> langs;
  for (const auto& v : catalog) langs.insert(v.languageCode);

  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
  if (ImGui::BeginCombo("Language", g.language.empty() ? "All" : g.language.c_str())) {
    if (ImGui::Selectable("All", g.language.empty())) g.language.clear();
    for (const auto& l : langs) {
      if (ImGui::Selectable(l.c_str(), g.language == l)) g.language = l;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
  ImGui::InputTextWithHint("##search", "Search...", &g.search);
  ImGui::SameLine();
  ImGui::BeginDisabled(app.models().catalogLoading());
  if (ImGui::Button(app.models().catalogLoading() ? "Loading..." : "Refresh list")) app.models().refreshCatalog();
  ImGui::EndDisabled();
  if (auto err = app.models().catalogError(); !err.empty()) ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1), "%s", err.c_str());

  std::map<std::string, DownloadProgress> dl;
  for (auto& d : app.models().downloads()) dl[d.key] = d;

  if (ImGui::BeginTable("catalog", 5,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0, ImGui::GetFontSize() * 22))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Voice", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Language", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9);
    ImGui::TableSetupColumn("Quality", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 13);
    ImGui::TableHeadersRow();
    for (const auto& v : catalog) {
      if (!g.language.empty() && v.languageCode != g.language) continue;
      if (!containsI(v.key, g.search) && !containsI(v.languageName, g.search)) continue;
      ImGui::PushID(v.key.c_str());
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(v.key.c_str());
      if (v.numSpeakers > 1) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%d speakers)", v.numSpeakers);
      }
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s (%s)", v.languageName.c_str(), v.countryName.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(v.quality.c_str());
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", formatBytes(v.totalSize()).c_str());
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("Model card")) {
        app.models().fetchModelCard(v);
        g.cardTitle = v.key;
        g.cardKey = v.key;
        g.openCard = true;
      }
      ImGui::SameLine();
      auto it = dl.find(v.key);
      if (it != dl.end() && it->second.active) {
        float frac = it->second.total ? static_cast<float>(it->second.done) / static_cast<float>(it->second.total) : 0.f;
        ImGui::ProgressBar(frac, ImVec2(ImGui::GetFontSize() * 4, 0));
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) app.models().cancelDownload(v.key);
      } else if (app.models().isInstalled(v.key)) {
        ImGui::TextDisabled("Installed");
      } else {
        if (ImGui::SmallButton("Download")) app.models().download(v);
        if (it != dl.end() && !it->second.error.empty()) {
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1), "!");
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", it->second.error.c_str());
        }
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

}  // namespace

void drawVoiceModelsView(App& app) {
  ImGui::Text("Voice Models");
  ImGui::Separator();
  ImGui::BeginChild("voices_scroll");
  installedSection(app);
  catalogSection(app);
  ImGui::EndChild();
  modelCardPopup(app);
}

}  // namespace vt::ui
