#include "cloning/RecordingScript.h"

#include <fstream>

#include "core/StringUtil.h"

namespace vt {

std::vector<std::string> loadRecordingScript(const std::filesystem::path& path) {
  std::vector<std::string> lines;
  std::ifstream f(path);
  std::string line;
  while (f && std::getline(f, line)) {
    auto t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    // Strip a UTF-8 BOM on the first line.
    if (lines.empty() && t.size() >= 3 && static_cast<unsigned char>(t[0]) == 0xEF) t = t.substr(3);
    // '|' is the metadata.csv delimiter.
    for (auto& c : t) {
      if (c == '|') c = ',';
    }
    lines.push_back(std::move(t));
  }
  if (lines.empty()) {
    lines = {
        "The quick brown fox jumps over the lazy dog.",
        "Thanks for the follow, welcome to the stream!",
        "How are you doing today?",
        "That was an incredible play, wasn't it?",
        "Please remember to stay hydrated.",
    };
  }
  return lines;
}

}  // namespace vt
