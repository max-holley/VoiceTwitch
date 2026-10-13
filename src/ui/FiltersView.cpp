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
  pageTitle("Filters", "Messages containing a blocked word are dropped before they reach the queue and never spoken");
  ImGui::TextDisabled("Matching ignores case and punctuation.   word = whole word   |   word* = starts with   |   *word = ends with   |   *word* = anywhere");

  sectionHeader("Add");
  beginCard("card_add");
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
  bool enter = ImGui::InputTextWithHint("##add", "word, phrase or pattern", &newWord,
                                        ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if (primaryButton("Add") || enter) {
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
  if (button("Import")) {
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
  endCard();

  sectionHeader("Test");
  beginCard("card_test");
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24);
  ImGui::InputTextWithHint("##test", "type a message to check it", &testText);
  if (!testText.empty()) {
    if (auto hit = filter.match(testText)) {
      ImGui::TextColored(kBad, "Blocked by \"%s\"", hit->c_str());
    } else {
      ImGui::TextColored(kOk, "Allowed");
    }
  }
  endCard();

  auto words = filter.words();
  char header[64];
  std::snprintf(header, sizeof(header), "Blocked words (%zu)", words.size());
  sectionHeader(header);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
  ImGui::InputTextWithHint("##search", "Search...", &search);
  ImGui::SameLine();
  if (dangerButton("Remove all")) ImGui::OpenPopup("Remove all words?");
  if (ImGui::BeginPopupModal("Remove all words?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Remove all %zu entries from the blocklist?", words.size());
    if (dangerButton("Remove all")) {
      filter.clear();
      app.saveFilters();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.115f, 0.115f, 0.145f, 1));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 0.6f, ImGui::GetFontSize() * 0.5f));
  ImGui::BeginChild("words", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  const std::string needle = toLowerAscii(search);
  std::string toRemove;
  bool any = false;
  if (ImGui::BeginTable("wordlist", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("word", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.5f);
    for (const auto& w : words) {
      if (!needle.empty() && w.find(needle) == std::string::npos) continue;
      any = true;
      ImGui::PushID(w.c_str());
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(w.c_str());
      ImGui::TableNextColumn();
      if (smallDangerButton("Remove")) toRemove = w;
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (!any) ImGui::TextDisabled(words.empty() ? "The blocklist is empty." : "No entries match your search.");
  ImGui::EndChild();
  if (!toRemove.empty() && filter.remove(toRemove)) app.saveFilters();
}

}  // namespace vt::ui
