#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace vt::paths {

namespace fs = std::filesystem;

// Platform app-data root:
//   Windows: %APPDATA%\VoiceTwitch
//   macOS:   ~/Library/Application Support/VoiceTwitch
//   Linux:   $XDG_DATA_HOME/VoiceTwitch or ~/.local/share/VoiceTwitch
// Can be overridden with the VOICETWITCH_DATA_DIR environment variable.
fs::path dataDir();
fs::path modelsDir();        // downloaded Piper voices
fs::path clonedVoicesDir();  // fine-tuned voices + recordings
fs::path configFile();       // config.json
fs::path filtersFile();      // filters.json
fs::path trainingEnvDir();   // Python venv for voice cloning (created on demand)

// Directory containing the running executable (where espeak-ng-data,
// training/ and assets/ are deployed).
fs::path executableDir();
fs::path espeakDataDir();
fs::path trainingScriptsDir();
fs::path assetsDir();

// Creates the data directory tree if needed.
void ensureDataDirs();

// UTF-8 <-> path conversions (generic '/' separators on output).
std::string toUtf8(const fs::path& p);
fs::path fromUtf8(std::string_view s);

}  // namespace vt::paths
