#pragma once

#include <span>
#include <vector>

namespace vt::dsp {

float peak(std::span<const float> s);
float rmsDb(std::span<const float> s);

// Removes leading/trailing audio quieter than `thresholdDb` (measured over
// 10ms windows), keeping `padMs` of context on each side.
std::vector<float> trimSilence(std::span<const float> s, int sampleRate, float thresholdDb = -40.0f,
                               int padMs = 120);

// Scales so the absolute peak hits `targetDb` dBFS. Silent input is unchanged.
void normalizePeak(std::vector<float>& s, float targetDb = -1.0f);

// Removes DC offset.
void removeDc(std::vector<float>& s);

// Converts sample rate using miniaudio's resampler.
std::vector<float> resample(std::span<const float> s, int fromRate, int toRate);

struct ClipCheck {
  bool tooQuiet = false;
  bool clipped = false;
  bool tooShort = false;
  float durationSec = 0.0f;
};
ClipCheck checkClip(std::span<const float> s, int sampleRate);

}  // namespace vt::dsp
