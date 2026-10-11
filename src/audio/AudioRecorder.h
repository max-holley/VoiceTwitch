#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct ma_device;

namespace vt {

// Captures mono float PCM from the default input device.
class AudioRecorder {
 public:
  AudioRecorder();
  ~AudioRecorder();
  AudioRecorder(const AudioRecorder&) = delete;
  AudioRecorder& operator=(const AudioRecorder&) = delete;

  bool start(int sampleRate, std::string& error);
  // Stops capture and returns everything recorded since start().
  std::vector<float> stop();
  bool isRecording() const { return recording_.load(); }
  float inputLevel() const { return level_.load(); }
  double secondsRecorded() const;
  int sampleRate() const { return rate_; }

 private:
  static void dataCallback(ma_device* device, void* out, const void* in, unsigned int frameCount);

  std::unique_ptr<ma_device> device_;
  bool deviceInit_ = false;
  int rate_ = 22050;
  mutable std::mutex mutex_;
  std::vector<float> samples_;
  std::atomic<bool> recording_{false};
  std::atomic<float> level_{0.0f};
};

}  // namespace vt
