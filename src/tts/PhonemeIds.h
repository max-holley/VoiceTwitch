#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "tts/VoiceConfig.h"

namespace vt {

// Converts one sentence of phonemes (IPA code points) to Piper model input IDs:
//   BOS, PAD, (phoneme, PAD)*, EOS
// Language-switch markers like "(en)" emitted by espeak are skipped, as are
// phonemes absent from the map (counted in `missing` if non-null).
std::vector<int64_t> phonemesToIds(std::u32string_view phonemes, const PhonemeIdMap& map, size_t* missing = nullptr);

}  // namespace vt
