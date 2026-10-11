#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace vt::wav {

// Writes mono 16-bit PCM.
bool writeMono16(const std::filesystem::path& path, std::span<const float> samples, int sampleRate);

// Decodes any format miniaudio understands (wav/flac/mp3) to mono float at
// `targetRate` (resampling and down-mixing as needed).
bool readMono(const std::filesystem::path& path, int targetRate, std::vector<float>& out, std::string& error);

// Encodes mono 16-bit PCM WAV into memory.
std::vector<unsigned char> encodeMono16(std::span<const float> samples, int sampleRate);

}  // namespace vt::wav
