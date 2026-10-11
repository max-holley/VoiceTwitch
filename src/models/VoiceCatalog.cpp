#include "models/VoiceCatalog.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace vt {

namespace {

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

const CatalogFile* findFile(const std::vector<CatalogFile>& files, const std::string& suffix) {
  for (const auto& f : files) {
    if (endsWith(f.path, suffix)) return &f;
  }
  return nullptr;
}

}  // namespace

uint64_t CatalogVoice::totalSize() const {
  uint64_t t = 0;
  for (const auto& f : files) t += f.sizeBytes;
  return t;
}

const CatalogFile* CatalogVoice::onnx() const { return findFile(files, ".onnx"); }
const CatalogFile* CatalogVoice::config() const { return findFile(files, ".onnx.json"); }
const CatalogFile* CatalogVoice::modelCard() const { return findFile(files, "MODEL_CARD"); }

bool parseCatalog(const std::string& jsonText, std::vector<CatalogVoice>& out, std::string& error) {
  try {
    auto j = nlohmann::json::parse(jsonText);
    if (!j.is_object()) {
      error = "voices.json is not an object";
      return false;
    }
    std::vector<CatalogVoice> voices;
    for (auto& [key, v] : j.items()) {
      if (!v.is_object()) continue;
      CatalogVoice cv;
      cv.key = v.value("key", key);
      cv.name = v.value("name", "");
      cv.quality = v.value("quality", "");
      cv.numSpeakers = v.value("num_speakers", 1);
      if (auto lang = v.find("language"); lang != v.end() && lang->is_object()) {
        cv.languageCode = lang->value("code", "");
        cv.languageName = lang->value("name_english", "");
        cv.countryName = lang->value("country_english", "");
      }
      if (auto files = v.find("files"); files != v.end() && files->is_object()) {
        for (auto& [path, info] : files->items()) {
          CatalogFile f;
          f.path = path;
          f.sizeBytes = info.value("size_bytes", uint64_t{0});
          f.md5 = info.value("md5_digest", "");
          cv.files.push_back(std::move(f));
        }
      }
      if (cv.onnx() && cv.config()) voices.push_back(std::move(cv));
    }
    std::sort(voices.begin(), voices.end(), [](const CatalogVoice& a, const CatalogVoice& b) {
      if (a.languageCode != b.languageCode) return a.languageCode < b.languageCode;
      return a.key < b.key;
    });
    out = std::move(voices);
    return true;
  } catch (const std::exception& e) {
    error = std::string("invalid voices.json: ") + e.what();
    return false;
  }
}

}  // namespace vt
