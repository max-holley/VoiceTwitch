#include "audio/AudioPlayer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include <miniaudio.h>

#include "core/Log.h"

namespace vt {

AudioPlayer::AudioPlayer() : device_(std::make_unique<ma_device>()) {}

AudioPlayer::~AudioPlayer() {
  stop();
  closeDevice();
}

void AudioPlayer::closeDevice() {
  if (deviceInit_) {
    ma_device_uninit(device_.get());
    deviceInit_ = false;
    deviceRate_ = 0;
  }
}

bool AudioPlayer::begin(int sampleRate, std::string& error) {
  {
    std::lock_guard lock(mutex_);
    buffer_.clear();
    readPos_ = 0;
    interrupted_ = false;
  }
  if (!deviceInit_ || deviceRate_ != sampleRate) {
    closeDevice();
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 1;
    cfg.sampleRate = static_cast<ma_uint32>(sampleRate);
    cfg.dataCallback = &AudioPlayer::dataCallback;
    cfg.pUserData = this;
    cfg.noPreSilencedOutputBuffer = MA_FALSE;
    ma_result r = ma_device_init(nullptr, &cfg, device_.get());
    if (r != MA_SUCCESS) {
      error = std::string("audio output init failed: ") + ma_result_description(r);
      return false;
    }
    deviceInit_ = true;
    deviceRate_ = sampleRate;
  }
  if (ma_device_get_state(device_.get()) != ma_device_state_started) {
    ma_result r = ma_device_start(device_.get());
    if (r != MA_SUCCESS) {
      error = std::string("audio output start failed: ") + ma_result_description(r);
      return false;
    }
  }
  playing_ = true;
  return true;
}

void AudioPlayer::push(std::span<const float> samples) {
  std::lock_guard lock(mutex_);
  if (interrupted_) return;
  // Compact consumed data so the buffer doesn't grow unbounded.
  if (readPos_ > 0 && readPos_ == buffer_.size()) {
    buffer_.clear();
    readPos_ = 0;
  }
  buffer_.insert(buffer_.end(), samples.begin(), samples.end());
}

bool AudioPlayer::waitUntilDrained() {
  std::unique_lock lock(mutex_);
  // Guard against a stalled/unplugged device: give up if playback makes no
  // progress for several seconds instead of blocking the pipeline forever.
  size_t lastPos = readPos_;
  int stalledSeconds = 0;
  while (!interrupted_ && readPos_ < buffer_.size()) {
    if (!drained_.wait_for(lock, std::chrono::seconds(1),
                           [this] { return interrupted_ || readPos_ >= buffer_.size(); })) {
      if (readPos_ == lastPos) {
        if (++stalledSeconds >= 5) {
          log::warn("Audio output stalled; dropping remaining audio");
          buffer_.clear();
          readPos_ = 0;
          return false;
        }
      } else {
        stalledSeconds = 0;
        lastPos = readPos_;
      }
    }
  }
  return !interrupted_;
}

void AudioPlayer::stop() {
  {
    std::lock_guard lock(mutex_);
    interrupted_ = true;
    buffer_.clear();
    readPos_ = 0;
  }
  drained_.notify_all();
}

void AudioPlayer::end() {
  if (deviceInit_ && ma_device_get_state(device_.get()) == ma_device_state_started) {
    ma_device_stop(device_.get());
  }
  playing_ = false;
  level_ = 0.0f;
}

void AudioPlayer::dataCallback(ma_device* device, void* out, const void*, unsigned int frameCount) {
  static_cast<AudioPlayer*>(device->pUserData)->fill(static_cast<float*>(out), frameCount);
}

void AudioPlayer::fill(float* out, unsigned int frames) {
  const float vol = volume_.load();
  float peak = 0.0f;
  bool nowDrained = false;
  {
    std::lock_guard lock(mutex_);
    size_t avail = buffer_.size() - readPos_;
    size_t n = std::min<size_t>(avail, frames);
    for (size_t i = 0; i < n; ++i) {
      float s = std::clamp(buffer_[readPos_ + i] * vol, -1.0f, 1.0f);
      out[i] = s;
      peak = std::max(peak, std::fabs(s));
    }
    if (n < frames) std::memset(out + n, 0, (frames - n) * sizeof(float));
    readPos_ += n;
    nowDrained = n > 0 && readPos_ >= buffer_.size();
  }
  level_.store(peak);
  if (nowDrained) drained_.notify_all();
}

}  // namespace vt
