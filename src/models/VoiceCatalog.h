#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vt {

// Piper's public voice index: https://huggingface.co/rhasspy/piper-voices
inline constexpr const char* kPiperVoicesBaseUrl = "https://huggingface.co/rhasspy/piper-voices/resolve/main/";
inline constexpr const char* kPiperVoicesIndex = "voices.json";

struct CatalogFile {
  std::string path;  // relative to the repository root
  uint64_t sizeBytes = 0;
  std::string md5;
};

struct CatalogVoice {
  std::string key;  // e.g. en_US-lessac-medium
  std::string name;
  std::string quality;
  std::string languageCode;     // en_US
  std::string languageName;     // English
  std::string countryName;      // United States
  int numSpeakers = 1;
  std::vector<CatalogFile> files;

  uint64_t totalSize() const;
  const CatalogFile* onnx() const;
  const CatalogFile* config() const;
  const CatalogFile* modelCard() const;
};

// Parses voices.json; voices are sorted by language then key.
bool parseCatalog(const std::string& jsonText, std::vector<CatalogVoice>& out, std::string& error);

}  // namespace vt
