#include "audio/Wav.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

#include <miniaudio.h>

namespace vt::wav {

namespace {

void put16(std::vector<unsigned char>& b, uint16_t v) {
  b.push_back(static_cast<unsigned char>(v & 0xFF));
  b.push_back(static_cast<unsigned char>(v >> 8));
}
void put32(std::vector<unsigned char>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}
void putTag(std::vector<unsigned char>& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }

}  // namespace

std::vector<unsigned char> encodeMono16(std::span<const float> samples, int sampleRate) {
  const auto dataBytes = static_cast<uint32_t>(samples.size() * 2);
  std::vector<unsigned char> b;
  b.reserve(44 + dataBytes);
  putTag(b, "RIFF");
  put32(b, 36 + dataBytes);
  putTag(b, "WAVE");
  putTag(b, "fmt ");
  put32(b, 16);
  put16(b, 1);  // PCM
  put16(b, 1);  // mono
  put32(b, static_cast<uint32_t>(sampleRate));
  put32(b, static_cast<uint32_t>(sampleRate) * 2);
  put16(b, 2);
  put16(b, 16);
  putTag(b, "data");
  put32(b, dataBytes);
  for (float s : samples) {
    auto v = static_cast<int16_t>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
    put16(b, static_cast<uint16_t>(v));
  }
  return b;
}

bool writeMono16(const std::filesystem::path& path, std::span<const float> samples, int sampleRate) {
  auto bytes = encodeMono16(samples, sampleRate);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(f);
}

bool readMono(const std::filesystem::path& path, int targetRate, std::vector<float>& out, std::string& error) {
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, static_cast<ma_uint32>(targetRate));
  ma_decoder dec;
#ifdef _WIN32
  ma_result r = ma_decoder_init_file_w(path.wstring().c_str(), &cfg, &dec);
#else
  ma_result r = ma_decoder_init_file(path.string().c_str(), &cfg, &dec);
#endif
  if (r != MA_SUCCESS) {
    error = std::string("cannot decode audio: ") + ma_result_description(r);
    return false;
  }
  out.clear();
  float buf[4096];
  while (true) {
    ma_uint64 read = 0;
    r = ma_decoder_read_pcm_frames(&dec, buf, std::size(buf), &read);
    out.insert(out.end(), buf, buf + read);
    if (r != MA_SUCCESS || read == 0) break;
  }
  ma_decoder_uninit(&dec);
  return true;
}

}  // namespace vt::wav
