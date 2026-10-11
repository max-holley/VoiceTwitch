#include "audio/AudioRecorder.h"

#include <algorithm>
#include <cmath>

#include <miniaudio.h>

namespace vt {

AudioRecorder::AudioRecorder() : device_(std::make_unique<ma_device>()) {}

AudioRecorder::~AudioRecorder() { stop(); }

bool AudioRecorder::start(int sampleRate, std::string& error) {
  stop();
  {
    std::lock_guard lock(mutex_);
    samples_.clear();
    samples_.reserve(static_cast<size_t>(sampleRate) * 15);
  }
  ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
  cfg.capture.format = ma_format_f32;
  cfg.capture.channels = 1;
  cfg.sampleRate = static_cast<ma_uint32>(sampleRate);
  cfg.dataCallback = &AudioRecorder::dataCallback;
  cfg.pUserData = this;
  ma_result r = ma_device_init(nullptr, &cfg, device_.get());
  if (r != MA_SUCCESS) {
    error = std::string("microphone init failed: ") + ma_result_description(r);
    return false;
  }
  deviceInit_ = true;
  rate_ = sampleRate;
  recording_ = true;
  r = ma_device_start(device_.get());
  if (r != MA_SUCCESS) {
    recording_ = false;
    ma_device_uninit(device_.get());
    deviceInit_ = false;
    error = std::string("microphone start failed: ") + ma_result_description(r);
    return false;
  }
  return true;
}

std::vector<float> AudioRecorder::stop() {
  if (deviceInit_) {
    recording_ = false;
    ma_device_uninit(device_.get());
    deviceInit_ = false;
  }
  level_ = 0.0f;
  std::lock_guard lock(mutex_);
  return std::move(samples_);
}

double AudioRecorder::secondsRecorded() const {
  std::lock_guard lock(mutex_);
  return rate_ > 0 ? static_cast<double>(samples_.size()) / rate_ : 0.0;
}

void AudioRecorder::dataCallback(ma_device* device, void*, const void* in, unsigned int frameCount) {
  auto* self = static_cast<AudioRecorder*>(device->pUserData);
  if (!self->recording_ || !in) return;
  const auto* src = static_cast<const float*>(in);
  float peak = 0.0f;
  for (unsigned i = 0; i < frameCount; ++i) peak = std::max(peak, std::fabs(src[i]));
  // Smooth decay for a readable meter.
  float prev = self->level_.load();
  self->level_.store(std::max(peak, prev * 0.85f));
  std::lock_guard lock(self->mutex_);
  self->samples_.insert(self->samples_.end(), src, src + frameCount);
}

}  // namespace vt
