#pragma once

#include <optional>
#include <string>

namespace vt::secure {

// Stores secrets in the OS credential store:
//   Windows: Credential Manager (generic credential)
//   macOS:   Keychain (generic password)
//   Linux:   Secret Service via libsecret when available at build time,
//            otherwise a 0600 file in the data dir (logged as a warning).
bool store(const std::string& key, const std::string& secret);
std::optional<std::string> load(const std::string& key);
bool remove(const std::string& key);

// Human-readable name of the backend in use (shown in the UI).
const char* backendName();

}  // namespace vt::secure
