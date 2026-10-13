#pragma once

#include <imgui.h>

namespace vt::ui {

// ---- palette ---------------------------------------------------------------

inline const ImVec4 kAccent{0.60f, 0.38f, 1.00f, 1.00f};
inline const ImVec4 kAccentHover{0.69f, 0.50f, 1.00f, 1.00f};
inline const ImVec4 kAccentDown{0.50f, 0.30f, 0.90f, 1.00f};
inline const ImVec4 kOk{0.31f, 0.82f, 0.56f, 1.0f};
inline const ImVec4 kWarn{0.96f, 0.72f, 0.26f, 1.0f};
inline const ImVec4 kBad{0.93f, 0.34f, 0.40f, 1.0f};
inline const ImVec4 kTextDim{0.60f, 0.60f, 0.68f, 1.0f};

// ---- animation plumbing ----------------------------------------------------

// Widgets call this while something is still moving so the (event-driven)
// main loop keeps rendering frames. animationActive() reports whether the
// previous frame asked for more; resetAnimationRequest() starts a new frame.
void requestFrames();
bool animationActive();
void resetAnimationRequest();

// Move `value` towards `target` with exponential easing; requests frames until
// it settles.
float animateTo(float value, float target, float speed = 14.0f);
float easeOutCubic(float t);

// ---- buttons ---------------------------------------------------------------

// One look for every button: rounded, soft drop shadow, colour eases on hover.
//   button()         neutral surface, for most actions
//   primaryButton()  accent, the main action of a section
//   dangerButton()   destructive or "stop" actions
// small* variants are compact, shadowless, for use inside table rows.
bool button(const char* label, ImVec2 size = ImVec2(0, 0));
bool primaryButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool dangerButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool smallButton(const char* label);
bool smallPrimaryButton(const char* label);
bool smallDangerButton(const char* label);

// ---- inputs ----------------------------------------------------------------

// Labelled slider with a filled track, animated knob and the value shown on
// the right of the label. width <= 0 uses a sensible default.
bool sliderFloat(const char* label, float* v, float lo, float hi, const char* fmt = "%.2f", float width = 0);
bool sliderInt(const char* label, int* v, int lo, int hi, const char* fmt = "%d", float width = 0);

// iOS-style switch with the label to its right.
bool toggle(const char* label, bool* v);

// ---- display ---------------------------------------------------------------

// Rounded container with padding; always pair beginCard with endCard.
void beginCard(const char* id, float height = 0);
void endCard();

// Small rounded status pill.
void badge(const char* text, ImVec4 color);

// Indeterminate circular spinner (`radius` in pixels, already scaled).
void spinner(float radius, float thickness, ImVec4 color);

// Dimmed, word-wrapped helper text.
void mutedText(const char* fmt, ...) IM_FMTARGS(1);

// Page heading with an optional dimmed one-line description.
void pageTitle(const char* title, const char* subtitle = nullptr);

// Value tile: dimmed label, large value, optional dimmed caption.
void statCard(const char* id, const char* label, const char* value, ImVec4 color, const char* caption = nullptr);

}  // namespace vt::ui
