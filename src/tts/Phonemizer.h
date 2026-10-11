#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vt::phonemizer {

// Initializes espeak-ng with the given espeak-ng-data directory. Safe to call
// more than once; only the first successful call does work.
bool init(const std::filesystem::path& espeakDataDir, std::string& error);

// Phonemizes text with espeak-ng (IPA), returning one string of phoneme code
// points per sentence, with clause punctuation preserved the same way Piper
// does it. Thread-safe (espeak-ng itself is not, so calls are serialized).
bool phonemize(std::string_view text, const std::string& espeakVoice, std::vector<std::u32string>& sentences,
               std::string& error);

}  // namespace vt::phonemizer
