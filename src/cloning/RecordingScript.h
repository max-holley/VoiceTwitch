#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vt {

// Loads the guided-recording prompt list (one sentence per line, '#' starts a
// comment line). Falls back to a short built-in list if the file is missing.
std::vector<std::string> loadRecordingScript(const std::filesystem::path& path);

}  // namespace vt
