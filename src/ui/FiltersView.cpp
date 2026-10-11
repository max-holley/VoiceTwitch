#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app/App.h"
#include "core/Paths.h"
#include "core/StringUtil.h"
#include "ui/Dashboard.h"

namespace vt::ui {

void drawFiltersView(App& app) {
  static std::string newWord;
  static std::string importPath;
  static std::string testText;
  static std::string search;
  static std::string message;

  auto& filter = app.filter();
  ImGui::Text("Filters");
  ImGui::Separator();
  ImGui::TextWrapped("Messages containing any blocked word are dropped before they reach the queue - they are never "
                     "spoken. Matching ignores case and punctuation.");
  ImGui::TextDisabled("word = whole word   |   word* = starts with   |   *word = ends with   |   *word* = anywhere");

  sectionHeader("Add");
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
  bool enter = ImGui::InputTextWithHint("##add", "word, phrase or pattern", &newWord,
                                        ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if (ImGui::Button("Add") || enter) {
    if (filter.add(newWord)) {
      app.saveFilters();
      message = "Added.";
    } else {
      message = "Already in the list (or empty).";
    }
    newWord.clear();
    ImGui::SetKeyboardFocusHere(-1);
  }

  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
  ImGui::InputTextWithHint("##import", "path to a .txt file (one entry per line)", &importPath);
  ImGui::SameLine();
  if (ImGui::Button("Import")) {
    std::string err;
    auto n = filter.importFile(paths::fromUtf8(trim(importPath)), err);
    if (!err.empty()) {
      message = err;
    } else {
      app.saveFilters();
      message = "Imported " + std::to_string(n) + " new entr" + (n == 1 ? "y." : "ies.");
    }
  }
  if (!message.empty()) ImGui::TextDisabled("%s", message.c_str());

  sectionHeader("Test");
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24);
  ImGui::InputTextWithHint("##test", "type a message to check it", &testText);
  if (!testText.empty()) {
    if (auto hit = filter.match(testText)) {
      ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1), "Blocked by \"%s\"", hit->c_str());
    } else {
      ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.45f, 1), "Allowed");
    }
  }

  auto words = filter.words();
  char header[64];
  std::snprintf(header, sizeof(header), "Blocked words (%zu)", words.size());
  sectionHeader(header);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
  ImGui::InputTextWithHint("##search", "Search...", &search);
  ImGui::SameLine();
  if (ImGui::Button("Remove all")) ImGui::OpenPopup("Remove all words?");
  if (ImGui::BeginPopupModal("Remove all words?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Remove all %zu entries from the blocklist?", words.size());
    if (ImGui::Button("Remove all")) {
      filter.clear();
      app.saveFilters();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::BeginChild("words", ImVec2(0, 0), ImGuiChildFlags_Borders);
  const std::string needle = toLowerAscii(search);
  std::string toRemove;
  for (const auto& w : words) {
    if (!needle.empty() && w.find(needle) == std::string::npos) continue;
    ImGui::PushID(w.c_str());
    if (ImGui::SmallButton("x")) toRemove = w;
    ImGui::SameLine();
    ImGui::TextUnformatted(w.c_str());
    ImGui::PopID();
  }
  ImGui::EndChild();
  if (!toRemove.empty() && filter.remove(toRemove)) app.saveFilters();
}

}  // namespace vt::ui
