#pragma once

#include <cstdint>
#include <string>

#include "ui/Widgets.h"

namespace vt {
class App;
}

namespace vt::ui {

void applyTheme(float scale);
void drawDashboard(App& app);

// Screens (left navigation). New v2 screens (events, per-user voices, queue
// rules) slot in as extra entries without changing the layout.
void drawActivityView(App& app);
void drawVoiceModelsView(App& app);
void drawCloningView(App& app);
void drawTwitchView(App& app);
void drawFiltersView(App& app);
void drawSettingsView(App& app);
void drawLogView(App& app);

// Shared widgets/helpers.
std::string formatBytes(uint64_t bytes);
void helpMarker(const char* text);
void sectionHeader(const char* text);
void statusDot(bool ok, bool warn = false);

}  // namespace vt::ui
