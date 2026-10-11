#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vt::http {

struct Response {
  long status = 0;  // 0 = transport error
  std::string body;
  std::string error;
  bool ok() const { return status >= 200 && status < 300; }
};

// Call once at startup / shutdown (wraps curl_global_init/cleanup).
void globalInit();
void globalCleanup();

Response get(const std::string& url, const std::vector<std::string>& headers = {});
// POSTs application/x-www-form-urlencoded data.
Response postForm(const std::string& url, const std::string& formBody);

// Progress callback: (bytesDownloaded, bytesTotal or 0 if unknown).
// Return false to cancel.
using ProgressFn = std::function<bool(uint64_t, uint64_t)>;

// Downloads to `<dest>.part`, then renames to `dest` on success.
bool downloadFile(const std::string& url, const std::filesystem::path& dest, const ProgressFn& progress,
                  std::string& error);

}  // namespace vt::http
