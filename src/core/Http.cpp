#include "core/Http.h"

#include <cstdio>
#include <system_error>

#include <curl/curl.h>

namespace vt::http {

namespace {

constexpr const char* kUserAgent = "VoiceTwitch/" VT_VERSION;

size_t appendToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
  static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
  return size * nmemb;
}

size_t writeToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
  return std::fwrite(ptr, size, nmemb, static_cast<FILE*>(userdata)) * size;
}

int progressThunk(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
  auto* fn = static_cast<const ProgressFn*>(clientp);
  if (fn && *fn && !(*fn)(static_cast<uint64_t>(dlnow), static_cast<uint64_t>(dltotal))) return 1;
  return 0;
}

void commonOptions(CURL* c, const std::string& url) {
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  // Abort if the transfer stalls below 1 byte/s for 60s.
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
#ifdef _WIN32
  // Use the Windows certificate store via Schannel defaults.
  curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
#endif
}

}  // namespace

void globalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void globalCleanup() { curl_global_cleanup(); }

Response get(const std::string& url, const std::vector<std::string>& headers) {
  Response r;
  CURL* c = curl_easy_init();
  if (!c) {
    r.error = "curl_easy_init failed";
    return r;
  }
  commonOptions(c, url);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
  curl_slist* hl = nullptr;
  for (const auto& h : headers) hl = curl_slist_append(hl, h.c_str());
  if (hl) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, appendToString);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
  CURLcode rc = curl_easy_perform(c);
  if (rc == CURLE_OK) {
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
  } else {
    r.error = curl_easy_strerror(rc);
  }
  curl_slist_free_all(hl);
  curl_easy_cleanup(c);
  return r;
}

Response postForm(const std::string& url, const std::string& formBody) {
  Response r;
  CURL* c = curl_easy_init();
  if (!c) {
    r.error = "curl_easy_init failed";
    return r;
  }
  commonOptions(c, url);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(c, CURLOPT_POSTFIELDS, formBody.c_str());
  curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(formBody.size()));
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, appendToString);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
  CURLcode rc = curl_easy_perform(c);
  if (rc == CURLE_OK) {
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
  } else {
    r.error = curl_easy_strerror(rc);
  }
  curl_easy_cleanup(c);
  return r;
}

bool downloadFile(const std::string& url, const std::filesystem::path& dest, const ProgressFn& progress,
                  std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(dest.parent_path(), ec);
  auto part = dest;
  part += ".part";

#ifdef _WIN32
  FILE* f = _wfopen(part.wstring().c_str(), L"wb");
#else
  FILE* f = std::fopen(part.string().c_str(), "wb");
#endif
  if (!f) {
    error = "cannot open " + part.string() + " for writing";
    return false;
  }

  CURL* c = curl_easy_init();
  if (!c) {
    std::fclose(f);
    error = "curl_easy_init failed";
    return false;
  }
  commonOptions(c, url);
  curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeToFile);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
  curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progressThunk);
  curl_easy_setopt(c, CURLOPT_XFERINFODATA, &progress);

  CURLcode rc = curl_easy_perform(c);
  curl_easy_cleanup(c);
  std::fclose(f);

  if (rc != CURLE_OK) {
    error = rc == CURLE_ABORTED_BY_CALLBACK ? "cancelled" : curl_easy_strerror(rc);
    std::filesystem::remove(part, ec);
    return false;
  }
  std::filesystem::remove(dest, ec);
  std::filesystem::rename(part, dest, ec);
  if (ec) {
    error = "rename failed: " + ec.message();
    return false;
  }
  return true;
}

}  // namespace vt::http
