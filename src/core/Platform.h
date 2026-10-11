#pragma once

#include <filesystem>
#include <string>

namespace vt::platform {

// Opens a URL in the user's default browser.
bool openUrl(const std::string& url);
// Reveals a folder in the OS file manager.
bool openFolder(const std::filesystem::path& dir);

// Writes text to a file via a temp file + rename so a crash never leaves a
// half-written config behind.
bool writeFileAtomic(const std::filesystem::path& path, const std::string& contents);
bool readFile(const std::filesystem::path& path, std::string& out);

}  // namespace vt::platform
