#include "audio/AudioProcessing.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <miniaudio.h>

namespace vt::dsp {

float peak(std::span<const float> s) {
  float p = 0.0f;
  for (float v : s) p = std::max(p, std::fabs(v));
  return p;
}

float rmsDb(std::span<const float> s) {
  if (s.empty()) return -120.0f;
  double acc = 0.0;
  for (float v : s) acc += static_cast<double>(v) * v;
  double rms = std::sqrt(acc / static_cast<double>(s.size()));
  return rms <= 1e-6 ? -120.0f : static_cast<float>(20.0 * std::log10(rms));
}

std::vector<float> trimSilence(std::span<const float> s, int sampleRate, float thresholdDb, int padMs) {
  const size_t win = std::max<size_t>(1, static_cast<size_t>(sampleRate) / 100);
  if (s.size() < win) return {s.begin(), s.end()};
  const size_t nWin = s.size() / win;
  size_t first = nWin, last = 0;
  for (size_t w = 0; w < nWin; ++w) {
    if (rmsDb(s.subspan(w * win, win)) > thresholdDb) {
      first = std::min(first, w);
      last = w;
    }
  }
  if (first == nWin) return {};  // all silence
  const size_t pad = static_cast<size_t>(sampleRate) * static_cast<size_t>(padMs) / 1000;
  size_t b = first * win > pad ? first * win - pad : 0;
  size_t e = std::min(s.size(), (last + 1) * win + pad);
  return {s.begin() + static_cast<std::ptrdiff_t>(b), s.begin() + static_cast<std::ptrdiff_t>(e)};
}

void normalizePeak(std::vector<float>& s, float targetDb) {
  float p = peak(s);
  if (p < 1e-4f) return;
  float gain = std::pow(10.0f, targetDb / 20.0f) / p;
  for (auto& v : s) v *= gain;
}

void removeDc(std::vector<float>& s) {
  if (s.empty()) return;
  double mean = std::accumulate(s.begin(), s.end(), 0.0) / static_cast<double>(s.size());
  for (auto& v : s) v -= static_cast<float>(mean);
}

std::vector<float> resample(std::span<const float> s, int fromRate, int toRate) {
  if (fromRate == toRate || s.empty()) return {s.begin(), s.end()};
  ma_resampler_config cfg = ma_resampler_config_init(ma_format_f32, 1, static_cast<ma_uint32>(fromRate),
                                                     static_cast<ma_uint32>(toRate), ma_resample_algorithm_linear);
  cfg.linear.lpfOrder = 8;
  ma_resampler rs;
  if (ma_resampler_init(&cfg, nullptr, &rs) != MA_SUCCESS) return {s.begin(), s.end()};
  ma_uint64 outFrames = 0;
  ma_resampler_get_expected_output_frame_count(&rs, s.size(), &outFrames);
  std::vector<float> out(static_cast<size_t>(outFrames) + 16);
  ma_uint64 inCount = s.size();
  ma_uint64 outCount = out.size();
  ma_resampler_process_pcm_frames(&rs, s.data(), &inCount, out.data(), &outCount);
  out.resize(static_cast<size_t>(outCount));
  ma_resampler_uninit(&rs, nullptr);
  return out;
}

ClipCheck checkClip(std::span<const float> s, int sampleRate) {
  ClipCheck c;
  c.durationSec = sampleRate > 0 ? static_cast<float>(s.size()) / static_cast<float>(sampleRate) : 0.0f;
  c.tooShort = c.durationSec < 0.6f;
  c.tooQuiet = peak(s) < 0.05f;
  size_t clippedSamples = 0;
  for (float v : s) {
    if (std::fabs(v) >= 0.999f) ++clippedSamples;
  }
  c.clipped = clippedSamples > 20;
  return c;
}

}  // namespace vt::dsp
