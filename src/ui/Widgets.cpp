#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>

namespace vt::ui {

namespace {

bool g_animRequested = false;
bool g_animActive = false;

std::unordered_map<ImGuiID, float>& anims() {
  static std::unordered_map<ImGuiID, float> m;
  return m;
}

ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec2 add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }

ImU32 col(const ImVec4& c, float alphaMul = 1.0f) {
  return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alphaMul));
}

enum class Kind { Neutral, Primary, Danger };

struct ButtonColors {
  ImVec4 base, hover, down;
};

ButtonColors colorsFor(Kind k) {
  switch (k) {
    case Kind::Primary: return {ImVec4(0.47f, 0.28f, 0.88f, 1), kAccent, kAccentDown};
    case Kind::Danger: return {ImVec4(0.62f, 0.20f, 0.26f, 1), ImVec4(0.80f, 0.27f, 0.33f, 1), ImVec4(0.55f, 0.16f, 0.22f, 1)};
    default: return {ImVec4(0.20f, 0.20f, 0.25f, 1), ImVec4(0.29f, 0.28f, 0.37f, 1), ImVec4(0.17f, 0.17f, 0.22f, 1)};
  }
}

bool drawButton(const char* label, ImVec2 size, Kind kind, bool small) {
  ImGuiStyle& style = ImGui::GetStyle();
  const ImGuiID id = ImGui::GetID(label);
  float& hot = anims()[id];
  const ButtonColors bc = colorsFor(kind);
  const ImVec4 cur = lerp(bc.base, bc.hover, hot);

  if (small) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x * 0.75f, style.FramePadding.y * 0.4f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, style.FrameRounding * 0.8f);
  }

  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 sz = size;
  const ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, true);
  if (sz.x <= 0) sz.x = textSize.x + ImGui::GetStyle().FramePadding.x * 2.0f;
  if (sz.y <= 0) sz.y = textSize.y + ImGui::GetStyle().FramePadding.y * 2.0f;

  if (!small) {
    // Soft shadow that lifts slightly while hovered.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lift = ImGui::GetFontSize() * (0.10f + 0.10f * hot);
    const float rounding = ImGui::GetStyle().FrameRounding;
    for (int i = 3; i >= 1; --i) {
      const float grow = ImGui::GetFontSize() * 0.07f * static_cast<float>(i);
      dl->AddRectFilled(ImVec2(pos.x - grow, pos.y + lift - grow * 0.4f), ImVec2(pos.x + sz.x + grow, pos.y + sz.y + lift + grow),
                        col(ImVec4(0, 0, 0, 0.11f + 0.04f * hot)), rounding + grow);
    }
  }

  ImGui::PushStyleColor(ImGuiCol_Button, cur);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cur);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, bc.down);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  if (small) ImGui::PopStyleVar(2);

  const float target = (ImGui::IsItemHovered() || ImGui::IsItemActive()) ? 1.0f : 0.0f;
  hot = animateTo(hot, target, 16.0f);
  return pressed;
}

float& animFor(ImGuiID id) { return anims()[id]; }

// Part of a label before "##".
const char* labelEnd(const char* label) {
  const char* e = std::strstr(label, "##");
  return e ? e : label + std::strlen(label);
}

}  // namespace

// ---- animation plumbing ----------------------------------------------------

void requestFrames() { g_animRequested = true; }
bool animationActive() { return g_animActive; }
void resetAnimationRequest() {
  g_animActive = g_animRequested;
  g_animRequested = false;
}

float animateTo(float value, float target, float speed) {
  const float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
  const float next = value + (target - value) * std::min(1.0f, dt * speed);
  if (std::fabs(target - next) < 0.004f) return target;
  requestFrames();
  return next;
}

float easeOutCubic(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}

// ---- buttons ---------------------------------------------------------------

bool button(const char* label, ImVec2 size) { return drawButton(label, size, Kind::Neutral, false); }
bool primaryButton(const char* label, ImVec2 size) { return drawButton(label, size, Kind::Primary, false); }
bool dangerButton(const char* label, ImVec2 size) { return drawButton(label, size, Kind::Danger, false); }
bool smallButton(const char* label) { return drawButton(label, ImVec2(0, 0), Kind::Neutral, true); }
bool smallPrimaryButton(const char* label) { return drawButton(label, ImVec2(0, 0), Kind::Primary, true); }
bool smallDangerButton(const char* label) { return drawButton(label, ImVec2(0, 0), Kind::Danger, true); }

// ---- sliders ---------------------------------------------------------------

namespace {

// Shared layout/drawing. `interact` runs the real ImGui slider (invisible) and
// returns whether the value changed; `valueText` is formatted afterwards so it
// reflects the new value.
template <typename Interact, typename Format>
bool sliderCommon(const char* label, float width, double t, Interact interact, Format format) {
  ImGui::PushID(label);
  const float fs = ImGui::GetFontSize();
  const float w = width > 0 ? width : std::min(ImGui::GetContentRegionAvail().x, fs * 22.0f);
  const float knobR = fs * 0.46f;
  const ImVec2 p0 = ImGui::GetCursorScreenPos();

  ImGui::TextUnformatted(label, labelEnd(label));

  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, knobR * 2.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::SetNextItemWidth(w);
  const bool changed = interact();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(5);

  const ImGuiID id = ImGui::GetItemID();
  const bool active = ImGui::IsItemActive();
  const bool hovered = ImGui::IsItemHovered();
  float& hot = animFor(id);
  hot = animateTo(hot, active ? 1.0f : (hovered ? 0.55f : 0.0f), 16.0f);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 rmin = ImGui::GetItemRectMin();
  const ImVec2 rmax = ImGui::GetItemRectMax();
  const float cy = (rmin.y + rmax.y) * 0.5f;
  const float trackH = fs * 0.34f;
  const float x0 = rmin.x + 2.0f + knobR;
  const float x1 = rmax.x - 2.0f - knobR;
  const float kx = x0 + static_cast<float>(std::clamp(t, 0.0, 1.0)) * (x1 - x0);
  const float rad = trackH * 0.5f;
  dl->AddRectFilled(ImVec2(rmin.x + 2.0f, cy - rad), ImVec2(rmax.x - 2.0f, cy + rad), col(ImVec4(0.21f, 0.21f, 0.27f, 1)), rad);
  if (kx > rmin.x + 2.0f) {
    dl->AddRectFilled(ImVec2(rmin.x + 2.0f, cy - rad), ImVec2(kx, cy + rad), col(lerp(kAccent, kAccentHover, hot)), rad);
  }
  const float r = knobR * (0.82f + 0.14f * hot);
  dl->AddCircleFilled(ImVec2(kx, cy + fs * 0.08f), r + fs * 0.08f, col(ImVec4(0, 0, 0, 0.30f)), 24);
  if (hot > 0.01f) dl->AddCircleFilled(ImVec2(kx, cy), r + fs * 0.32f * hot, col(kAccent, 0.22f * hot), 24);
  dl->AddCircleFilled(ImVec2(kx, cy), r, col(ImVec4(0.97f, 0.96f, 1.0f, 1)), 24);

  char buf[64];
  format(buf, sizeof(buf));
  const ImVec2 ts = ImGui::CalcTextSize(buf);
  dl->AddText(ImVec2(p0.x + w - ts.x, p0.y), col(kAccentHover), buf);

  ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.5f));
  ImGui::PopID();
  return changed;
}

}  // namespace

bool sliderFloat(const char* label, float* v, float lo, float hi, const char* fmt, float width) {
  auto interact = [&] { return ImGui::SliderFloat("##s", v, lo, hi, "", ImGuiSliderFlags_AlwaysClamp); };
  auto format = [&](char* b, size_t n) { std::snprintf(b, n, fmt, static_cast<double>(*v)); };
  return sliderCommon(label, width, hi > lo ? static_cast<double>((*v - lo) / (hi - lo)) : 0.0, interact, format);
}

bool sliderInt(const char* label, int* v, int lo, int hi, const char* fmt, float width) {
  auto interact = [&] { return ImGui::SliderInt("##s", v, lo, hi, "", ImGuiSliderFlags_AlwaysClamp); };
  auto format = [&](char* b, size_t n) { std::snprintf(b, n, fmt, *v); };
  return sliderCommon(label, width, hi > lo ? static_cast<double>(*v - lo) / (hi - lo) : 0.0, interact, format);
}

// ---- toggle ----------------------------------------------------------------

bool toggle(const char* label, bool* v) {
  ImGui::PushID(label);
  const float fs = ImGui::GetFontSize();
  const float trackH = fs * 1.25f;
  const float trackW = trackH * 1.85f;
  const float gap = fs * 0.6f;
  const char* end = labelEnd(label);
  const ImVec2 textSize = ImGui::CalcTextSize(label, end);
  const float h = std::max(trackH, textSize.y) + ImGui::GetStyle().FramePadding.y * 0.5f;
  const ImVec2 pos = ImGui::GetCursorScreenPos();

  const bool pressed = ImGui::InvisibleButton("##t", ImVec2(trackW + gap + textSize.x, h));
  if (pressed) *v = !*v;
  const bool hovered = ImGui::IsItemHovered();

  float& a = animFor(ImGui::GetItemID());
  a = animateTo(a, *v ? 1.0f : 0.0f, 16.0f);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float ty = pos.y + (h - trackH) * 0.5f;
  const ImVec4 off = hovered ? ImVec4(0.31f, 0.31f, 0.39f, 1) : ImVec4(0.25f, 0.25f, 0.32f, 1);
  const ImVec4 on = hovered ? kAccentHover : kAccent;
  dl->AddRectFilled(ImVec2(pos.x, ty), ImVec2(pos.x + trackW, ty + trackH), col(lerp(off, on, a)), trackH * 0.5f);
  const float pad = trackH * 0.14f;
  const float kr = trackH * 0.5f - pad;
  const float kx = pos.x + trackH * 0.5f + a * (trackW - trackH);
  dl->AddCircleFilled(ImVec2(kx, ty + trackH * 0.5f + 1.5f), kr + 1.0f, col(ImVec4(0, 0, 0, 0.28f)), 20);
  dl->AddCircleFilled(ImVec2(kx, ty + trackH * 0.5f), kr, col(ImVec4(0.98f, 0.97f, 1.0f, 1)), 20);
  dl->AddText(ImVec2(pos.x + trackW + gap, pos.y + (h - textSize.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label, end);

  ImGui::PopID();
  return pressed;
}

// ---- display ---------------------------------------------------------------

void beginCard(const char* id, float height) {
  const float fs = ImGui::GetFontSize();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 1.0f, fs * 0.85f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, fs * 0.65f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.115f, 0.115f, 0.145f, 1));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.19f, 0.19f, 0.24f, 1));
  ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
  if (height <= 0) flags |= ImGuiChildFlags_AutoResizeY;
  ImGui::BeginChild(id, ImVec2(0, height), flags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);
}

void endCard() {
  ImGui::EndChild();
  ImGui::Dummy(ImVec2(0, 2));
}

void badge(const char* text, ImVec4 color) {
  const float fs = ImGui::GetFontSize();
  const ImVec2 ts = ImGui::CalcTextSize(text);
  const ImVec2 pad(fs * 0.55f, fs * 0.12f);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const ImVec2 size(ts.x + pad.x * 2, ts.y + pad.y * 2);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(pos, add(pos, size), col(color, 0.16f), size.y * 0.5f);
  dl->AddRect(pos, add(pos, size), col(color, 0.38f), size.y * 0.5f);
  dl->AddText(add(pos, pad), col(color), text);
  ImGui::Dummy(size);
}

void spinner(float radius, float thickness, ImVec4 color) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const ImVec2 size(radius * 2, radius * 2);
  ImGui::Dummy(size);
  const ImVec2 c(pos.x + radius, pos.y + radius);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float t = static_cast<float>(ImGui::GetTime());
  const float r = radius - thickness * 0.5f;
  dl->AddCircle(c, r, col(color, 0.18f), 32, thickness);

  constexpr int kSeg = 30;
  const float start = std::fabs(std::sin(t * 1.8f)) * (kSeg - 5);
  const float aMin = 3.14159265f * 2.0f * start / kSeg;
  const float aMax = 3.14159265f * 2.0f * (kSeg - 3) / kSeg;
  for (int i = 0; i < kSeg; ++i) {
    const float a = aMin + (static_cast<float>(i) / kSeg) * (aMax - aMin);
    dl->PathLineTo(ImVec2(c.x + std::cos(a + t * 8.0f) * r, c.y + std::sin(a + t * 8.0f) * r));
  }
  dl->PathStroke(col(color), ImDrawFlags_None, thickness);
  requestFrames();
}

void mutedText(const char* fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
  ImGui::TextWrapped("%s", buf);
  ImGui::PopStyleColor();
}

void pageTitle(const char* title, const char* subtitle) {
  ImGui::SetWindowFontScale(1.55f);
  ImGui::TextUnformatted(title);
  ImGui::SetWindowFontScale(1.0f);
  if (subtitle) mutedText("%s", subtitle);
  ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.5f));
}

void statCard(const char* id, const char* label, const char* value, ImVec4 color, const char* caption) {
  beginCard(id);
  ImGui::TextColored(kTextDim, "%s", label);
  ImGui::SetWindowFontScale(1.6f);
  ImGui::TextColored(color, "%s", value);
  ImGui::SetWindowFontScale(1.0f);
  if (caption) mutedText("%s", caption);
  endCard();
}

}  // namespace vt::ui
