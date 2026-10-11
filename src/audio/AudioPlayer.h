#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

struct ma_device;

namespace vt {

// Streams mono float PCM to the default output device.
//
// One utterance at a time: begin() -> push()* -> waitUntilDrained() -> end().
// The device is stopped between utterances so idle CPU stays near zero.
class AudioPlayer {
 public:
  AudioPlayer();
  ~AudioPlayer();
  AudioPlayer(const AudioPlayer&) = delete;
  AudioPlayer& operator=(const AudioPlayer&) = delete;

  void setVolume(float v) { volume_.store(v); }
  float volume() const { return volume_.load(); }

  bool begin(int sampleRate, std::string& error);
  void push(std::span<const float> samples);
  // Blocks until everything pushed so far has been played, or stop() was
  // called. Returns false if interrupted.
  bool waitUntilDrained();
  // Drops queued audio immediately (skip / emergency stop).
  void stop();
  // Stops the device. Call after waitUntilDrained().
  void end();

  bool isPlaying() const { return playing_.load(); }
  // Peak level of the most recent output buffer (0..1), for the UI meter.
  float outputLevel() const { return level_.load(); }

 private:
  static void dataCallback(ma_device* device, void* out, const void* in, unsigned int frameCount);
  void fill(float* out, unsigned int frames);
  void closeDevice();

  std::unique_ptr<ma_device> device_;
  bool deviceInit_ = false;
  int deviceRate_ = 0;

  std::mutex mutex_;
  std::condition_variable drained_;
  std::vector<float> buffer_;
  size_t readPos_ = 0;
  bool interrupted_ = false;

  std::atomic<float> volume_{0.8f};
  std::atomic<float> level_{0.0f};
  std::atomic<bool> playing_{false};
};

}  // namespace vt
