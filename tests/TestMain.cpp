// Dependency-free unit tests. Run via `ctest` or the voicetwitch_tests binary.
// Set VT_TEST_MODEL=/path/to/voice.onnx to also run an end-to-end synthesis.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "audio/AudioProcessing.h"
#include "audio/Wav.h"
#include "core/Config.h"
#include "core/Paths.h"
#include "core/StringUtil.h"
#include "models/VoiceCatalog.h"
#include "pipeline/BadWordFilter.h"
#include "pipeline/SpeechPipeline.h"
#include "tts/PhonemeIds.h"
#include "tts/Phonemizer.h"
#include "tts/PiperEngine.h"
#include "tts/VoiceConfig.h"
#include "twitch/IrcParser.h"
#include "twitch/TwitchAuth.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    ++g_checks;                                                              \
    if (!(cond)) {                                                           \
      ++g_failures;                                                          \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

struct TestCase {
  const char* name;
  std::function<void()> fn;
};

std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

struct Register {
  Register(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

#define TEST(name)                                  \
  static void name();                               \
  static Register reg_##name(#name, name);          \
  static void name()

using namespace vt;

TEST(utf8_roundtrip) {
  std::string s = "h\xC3\xA9llo \xC9\x99 \xF0\x9F\x98\x80";  // héllo ə 😀
  auto u = utf8ToUtf32(s);
  CHECK_EQ(u.size(), 9u);
  CHECK_EQ(u[1], U'é');
  CHECK_EQ(u[6], U'ə');
  CHECK_EQ(u[8], U'\U0001F600');
  CHECK_EQ(utf32ToUtf8(u), s);
  auto bad = utf8ToUtf32("a\xFF" "b");
  CHECK_EQ(bad.size(), 3u);
  CHECK_EQ(bad[1], U'�');
}

TEST(url_and_query) {
  CHECK_EQ(urlEncode("chat:read a/b"), "chat%3Aread%20a%2Fb");
  CHECK_EQ(urlDecode("a%20b+c%2"), "a b c%2");
  auto q = parseQuery("access_token=abc123&scope=chat%3Aread&state=xyz&token_type=bearer");
  CHECK_EQ(q["access_token"], "abc123");
  CHECK_EQ(q["scope"], "chat:read");
  CHECK_EQ(q["state"], "xyz");
  CHECK_EQ(randomHex(8).size(), 16u);
}

TEST(authorize_url) {
  auto url = TwitchAuth::buildAuthorizeUrl("cid", 17563, "st");
  CHECK(url.find("response_type=token") != std::string::npos);
  CHECK(url.find("client_id=cid") != std::string::npos);
  CHECK(url.find("redirect_uri=http%3A%2F%2Flocalhost%3A17563") != std::string::npos);
  CHECK(url.find("scope=chat%3Aread") != std::string::npos);
  CHECK(url.find("state=st") != std::string::npos);
}

TEST(irc_privmsg_with_tags) {
  auto m = irc::parseLine(
      "@badge-info=;color=#FF0000;display-name=Cool\\sUser;emotes=;id=1 "
      ":cooluser!cooluser@cooluser.tmi.twitch.tv PRIVMSG #streamer :hello there: friend\r\n");
  CHECK(m.has_value());
  CHECK_EQ(m->command, "PRIVMSG");
  CHECK_EQ(m->nick(), "cooluser");
  CHECK_EQ(m->tag("display-name"), "Cool User");
  CHECK_EQ(m->tag("color"), "#FF0000");
  CHECK_EQ(m->params.size(), 2u);
  CHECK_EQ(m->params[0], "#streamer");
  CHECK_EQ(m->trailing(), "hello there: friend");
}

TEST(irc_misc_commands) {
  auto ping = irc::parseLine("PING :tmi.twitch.tv");
  CHECK(ping && ping->command == "PING" && ping->trailing() == "tmi.twitch.tv");
  auto welcome = irc::parseLine(":tmi.twitch.tv 001 bot :Welcome, GLHF!");
  CHECK(welcome && welcome->command == "001" && welcome->params.size() == 2);
  auto notice = irc::parseLine(":tmi.twitch.tv NOTICE * :Login authentication failed");
  CHECK(notice && notice->trailing() == "Login authentication failed");
  CHECK(!irc::parseLine("").has_value());
  CHECK_EQ(irc::unescapeTagValue("a\\:b\\sc\\\\"), "a;b c\\");
}

TEST(filter_matching) {
  BadWordFilter f;
  f.setWords({"darn", "heck*", "*bad", "*mid*", "two words", "  DARN "});
  CHECK_EQ(f.words().size(), 5u);
  CHECK(f.match("well DARN it!").has_value());
  CHECK(!f.match("darnation").has_value());
  CHECK(f.match("what the heckin").has_value());
  CHECK(!f.match("checkmate").has_value());
  CHECK(f.match("that is superbad").has_value());
  CHECK(!f.match("badge").has_value());
  CHECK(f.match("amidst").has_value());
  CHECK(f.match("I said TWO, words.").has_value());
  CHECK(!f.match("two-wordsmith").has_value());
  CHECK(!f.match("perfectly fine message").has_value());
  CHECK_EQ(*f.match("Darn."), "darn");
  CHECK(f.add("new"));
  CHECK(!f.add("NEW"));
  CHECK(f.remove("new"));
  CHECK(!f.add("***"));
}

TEST(filter_persistence) {
  auto dir = std::filesystem::temp_directory_path() / "vt_test_filters";
  std::filesystem::create_directories(dir);
  BadWordFilter a;
  a.setWords({"alpha", "beta*"});
  CHECK(a.save(dir / "filters.json"));
  BadWordFilter b;
  CHECK(b.load(dir / "filters.json"));
  CHECK_EQ(b.words().size(), 2u);
  {
    FILE* f = std::fopen((dir / "import.txt").string().c_str(), "w");
    std::fputs("# comment\ngamma\ndelta, epsilon\n\nalpha\n", f);
    std::fclose(f);
  }
  std::string err;
  CHECK_EQ(b.importFile(dir / "import.txt", err), 3u);
  std::filesystem::remove_all(dir);
}

TEST(compose_speech) {
  ChatMessage m;
  m.userLogin = "viewer1";
  m.displayName = "Viewer1";
  m.text = "  hi there  ";
  PipelineSettings s;
  CHECK_EQ(SpeechPipeline::composeSpeech(m, s), "Viewer1 says hi there");
  s.announceUsername = false;
  CHECK_EQ(SpeechPipeline::composeSpeech(m, s), "hi there");
  s.announceUsername = true;
  m.text = "\x01" "ACTION waves\x01";
  CHECK_EQ(SpeechPipeline::composeSpeech(m, s), "Viewer1 waves");
  s.announceUsername = false;
  s.maxMessageChars = 20;
  m.text = "one two three four five six seven";
  auto out = SpeechPipeline::composeSpeech(m, s);
  CHECK(out.size() <= 20);
  CHECK_EQ(out, "one two three four");
}

TEST(phoneme_ids) {
  PhonemeIdMap map{{U'_', {0}}, {U'^', {1}}, {U'$', {2}}, {U'h', {20}}, {U'ə', {59}}, {U' ', {3}}};
  size_t missing = 0;
  auto ids = phonemesToIds(U"hə(en)x h", map, &missing);
  // ^ _ h _ ə _ ␠ _ h _ $   ('x' unknown, "(en)" skipped)
  std::vector<int64_t> expected{1, 0, 20, 0, 59, 0, 3, 0, 20, 0, 2};
  CHECK(ids == expected);
  CHECK_EQ(missing, 1u);
}

TEST(voice_config_parse) {
  std::string json = R"({
    "audio": {"sample_rate": 16000},
    "espeak": {"voice": "en-gb"},
    "inference": {"noise_scale": 0.5, "length_scale": 1.2, "noise_w": 0.7},
    "num_speakers": 3,
    "speaker_id_map": {"a": 0, "b": 1, "c": 2},
    "phoneme_id_map": {"_": [0], "^": [1], "$": [2], "ə": [59]}
  })";
  VoiceConfig c;
  std::string err;
  CHECK(VoiceConfig::parse(json, c, err));
  CHECK_EQ(c.sampleRate, 16000);
  CHECK_EQ(c.espeakVoice, "en-gb");
  CHECK(std::fabs(c.lengthScale - 1.2f) < 1e-6f);
  CHECK_EQ(c.numSpeakers, 3);
  CHECK_EQ(c.speakerIdMap.size(), 3u);
  CHECK_EQ(c.phonemeIdMap.at(U'ə')[0], 59);
  CHECK(!VoiceConfig::parse("{}", c, err));
}

TEST(config_roundtrip) {
  AppConfig a;
  a.twitchChannel = "somechannel";
  a.activeVoice = "models/x/x.onnx";
  a.volume = 0.3f;
  a.rate = 1.5f;
  a.announceUsername = false;
  auto b = AppConfig::fromJson(a.toJson());
  CHECK_EQ(b.twitchChannel, "somechannel");
  CHECK_EQ(b.activeVoice, "models/x/x.onnx");
  CHECK(std::fabs(b.volume - 0.3f) < 1e-6f);
  CHECK(!b.announceUsername);
  auto clamped = AppConfig::fromJson(nlohmann::json{{"playback", {{"volume", 5.0}, {"rate", 0.01}}}});
  CHECK_EQ(clamped.volume, 1.0f);
  CHECK_EQ(clamped.rate, 0.5f);
}

TEST(catalog_parse) {
  std::string json = R"({
    "en_US-test-medium": {
      "key": "en_US-test-medium", "name": "test", "quality": "medium", "num_speakers": 1,
      "language": {"code": "en_US", "name_english": "English", "country_english": "United States"},
      "files": {
        "en/en_US/test/medium/en_US-test-medium.onnx": {"size_bytes": 1000, "md5_digest": "x"},
        "en/en_US/test/medium/en_US-test-medium.onnx.json": {"size_bytes": 10, "md5_digest": "y"},
        "en/en_US/test/medium/MODEL_CARD": {"size_bytes": 5, "md5_digest": "z"}
      }
    },
    "broken": {"key": "broken", "files": {}}
  })";
  std::vector<CatalogVoice> v;
  std::string err;
  CHECK(parseCatalog(json, v, err));
  CHECK_EQ(v.size(), 1u);
  CHECK_EQ(v[0].totalSize(), 1015u);
  CHECK(v[0].onnx() && v[0].onnx()->path.ends_with("medium.onnx"));
  CHECK(v[0].config() != nullptr);
  CHECK(v[0].modelCard() != nullptr);
}

TEST(dsp_trim_normalize_resample) {
  const int rate = 16000;
  std::vector<float> s(rate * 2, 0.0f);
  for (int i = rate / 2; i < rate; ++i) s[i] = 0.25f * std::sin(i * 0.1f);
  auto t = dsp::trimSilence(s, rate, -40.0f, 100);
  CHECK(t.size() > static_cast<size_t>(rate / 2));
  CHECK(t.size() < static_cast<size_t>(rate * 0.75));
  dsp::normalizePeak(t, -1.0f);
  CHECK(std::fabs(dsp::peak(t) - std::pow(10.0f, -1.0f / 20.0f)) < 1e-3f);
  auto r = dsp::resample(t, rate, 22050);
  double ratio = static_cast<double>(r.size()) / t.size();
  CHECK(std::fabs(ratio - 22050.0 / 16000.0) < 0.01);
  CHECK(dsp::trimSilence(std::vector<float>(rate, 0.0f), rate).empty());
}

TEST(wav_encode) {
  std::vector<float> s{0.0f, 1.0f, -1.0f, 0.5f};
  auto b = wav::encodeMono16(s, 22050);
  CHECK_EQ(b.size(), 44u + 8u);
  CHECK(std::string(b.begin(), b.begin() + 4) == "RIFF");
  CHECK(std::string(b.begin() + 8, b.begin() + 12) == "WAVE");
  int16_t second = static_cast<int16_t>(b[46] | (b[47] << 8));
  CHECK_EQ(second, 32767);
}

TEST(espeak_phonemize) {
  std::string err;
  auto dataDir = paths::espeakDataDir();
  if (!phonemizer::init(dataDir, err)) {
    std::printf("  (espeak-ng-data not deployed next to the test binary: %s)\n", err.c_str());
    CHECK(false);
    return;
  }
  std::vector<std::u32string> sentences;
  CHECK(phonemizer::phonemize("Hello world. How are you?", "en-us", sentences, err));
  CHECK_EQ(sentences.size(), 2u);
  if (sentences.size() == 2) {
    auto first = utf32ToUtf8(sentences[0]);
    std::printf("  phonemes: %s | %s\n", first.c_str(), utf32ToUtf8(sentences[1]).c_str());
    CHECK(first.find("h") != std::string::npos);
    CHECK(first.back() == '.');
    CHECK(sentences[1].back() == U'?');
  }
}

TEST(piper_end_to_end) {
  const char* model = std::getenv("VT_TEST_MODEL");
  if (!model) {
    std::printf("  (skipped: set VT_TEST_MODEL to a .onnx voice)\n");
    return;
  }
  PiperEngine engine;
  std::string err;
  CHECK(engine.load(model, err));
  size_t samples = 0;
  CHECK(engine.synthesize("Testing one two three. This is VoiceTwitch.", SynthesisParams{},
                          [&](std::span<const float> c) {
                            samples += c.size();
                            return true;
                          },
                          err));
  std::printf("  synthesized %.2f s\n", static_cast<double>(samples) / engine.sampleRate());
  CHECK(samples > static_cast<size_t>(engine.sampleRate()));
}

}  // namespace

int main() {
  for (const auto& t : registry()) {
    int before = g_failures;
    std::printf("[ RUN  ] %s\n", t.name);
    t.fn();
    std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
  }
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
