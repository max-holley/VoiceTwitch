#include "tts/Phonemizer.h"

#include <mutex>

#ifdef _WIN32
// espeak-ng is linked statically; without this the header declares its API
// as __declspec(dllimport) and the static symbols don't resolve.
#define LIBESPEAK_NG_EXPORT
#endif
#include <espeak-ng/speak_lib.h>

#include "core/StringUtil.h"

namespace vt::phonemizer {

namespace {

// From espeak-ng's src/libespeak-ng/translate.h (not a public header).
constexpr int kClauseIntonationFullStop = 0x00000000;
constexpr int kClauseIntonationComma = 0x00001000;
constexpr int kClauseIntonationQuestion = 0x00002000;
constexpr int kClauseIntonationExclamation = 0x00003000;
constexpr int kClauseTypeClause = 0x00040000;
constexpr int kClauseTypeSentence = 0x00080000;
constexpr int kClausePeriod = 40 | kClauseIntonationFullStop | kClauseTypeSentence;
constexpr int kClauseComma = 20 | kClauseIntonationComma | kClauseTypeClause;
constexpr int kClauseQuestion = 40 | kClauseIntonationQuestion | kClauseTypeSentence;
constexpr int kClauseExclamation = 45 | kClauseIntonationExclamation | kClauseTypeSentence;
constexpr int kClauseColon = 30 | kClauseIntonationFullStop | kClauseTypeClause;
constexpr int kClauseSemicolon = 30 | kClauseIntonationComma | kClauseTypeClause;

std::mutex g_mutex;
bool g_initialized = false;
std::string g_currentVoice;

}  // namespace

bool init(const std::filesystem::path& espeakDataDir, std::string& error) {
  std::lock_guard lock(g_mutex);
  if (g_initialized) return true;
  std::error_code ec;
  if (!std::filesystem::exists(espeakDataDir / "phontab", ec)) {
    error = "espeak-ng-data not found at " + espeakDataDir.string();
    return false;
  }
  // espeak_Initialize takes the directory *containing* espeak-ng-data.
  std::string dataPath = espeakDataDir.parent_path().string();
  int rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, dataPath.c_str(), 0);
  if (rate <= 0) {
    error = "failed to initialize espeak-ng (data dir: " + espeakDataDir.string() + ")";
    return false;
  }
  g_initialized = true;
  return true;
}

bool phonemize(std::string_view text, const std::string& espeakVoice, std::vector<std::u32string>& sentences,
               std::string& error) {
  std::lock_guard lock(g_mutex);
  sentences.clear();
  if (!g_initialized) {
    error = "espeak-ng not initialized";
    return false;
  }
  if (g_currentVoice != espeakVoice) {
    if (espeak_SetVoiceByName(espeakVoice.c_str()) != EE_OK) {
      error = "espeak-ng has no voice '" + espeakVoice + "'";
      return false;
    }
    g_currentVoice = espeakVoice;
  }

  std::string input(text);
  std::vector<std::string> utf8Sentences{""};
  const void* ptr = input.c_str();
  while (ptr != nullptr) {
    int terminator = 0;
    const char* ph = espeak_TextToPhonemesWithTerminator(&ptr, espeakCHARS_AUTO, espeakPHONEMES_IPA, &terminator);
    if (ph) utf8Sentences.back() += ph;
    terminator &= 0x000FFFFF;
    if (terminator == kClausePeriod) {
      utf8Sentences.back() += ".";
    } else if (terminator == kClauseQuestion) {
      utf8Sentences.back() += "?";
    } else if (terminator == kClauseExclamation) {
      utf8Sentences.back() += "!";
    } else if (terminator == kClauseComma) {
      utf8Sentences.back() += ", ";
    } else if (terminator == kClauseColon) {
      utf8Sentences.back() += ": ";
    } else if (terminator == kClauseSemicolon) {
      utf8Sentences.back() += "; ";
    }
    if ((terminator & kClauseTypeSentence) == kClauseTypeSentence) utf8Sentences.emplace_back();
  }
  for (auto& s : utf8Sentences) {
    if (!s.empty()) sentences.push_back(utf8ToUtf32(s));
  }
  return true;
}

}  // namespace vt::phonemizer
