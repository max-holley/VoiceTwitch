#include "tts/PhonemeIds.h"

namespace vt {

namespace {

constexpr char32_t kPad = U'_';
constexpr char32_t kBos = U'^';
constexpr char32_t kEos = U'$';

void appendIds(std::vector<int64_t>& out, const PhonemeIdMap& map, char32_t cp, int64_t fallback) {
  if (auto it = map.find(cp); it != map.end()) {
    out.insert(out.end(), it->second.begin(), it->second.end());
  } else {
    out.push_back(fallback);
  }
}

}  // namespace

std::vector<int64_t> phonemesToIds(std::u32string_view phonemes, const PhonemeIdMap& map, size_t* missing) {
  std::vector<int64_t> ids;
  ids.reserve(phonemes.size() * 2 + 4);
  std::vector<int64_t> pad;
  appendIds(pad, map, kPad, 0);

  appendIds(ids, map, kBos, 1);
  ids.insert(ids.end(), pad.begin(), pad.end());

  bool inLangFlag = false;
  for (char32_t cp : phonemes) {
    if (inLangFlag) {
      if (cp == U')') inLangFlag = false;
      continue;
    }
    if (cp == U'(') {
      inLangFlag = true;
      continue;
    }
    auto it = map.find(cp);
    if (it == map.end()) {
      if (missing) ++*missing;
      continue;
    }
    for (int64_t id : it->second) {
      ids.push_back(id);
      ids.insert(ids.end(), pad.begin(), pad.end());
    }
  }

  appendIds(ids, map, kEos, 2);
  return ids;
}

}  // namespace vt
