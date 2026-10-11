#include "core/SecureStore.h"

#include "core/Log.h"

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#include "core/StringUtil.h"
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(VT_HAVE_LIBSECRET)
#include <libsecret/secret.h>
#else
#include <sys/stat.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include "core/Paths.h"
#include "core/Platform.h"
#endif

namespace vt::secure {

namespace {
constexpr const char* kService = "VoiceTwitch";
}

#ifdef _WIN32

static std::wstring targetName(const std::string& key) { return widen(std::string(kService) + "/" + key); }

bool store(const std::string& key, const std::string& secret) {
  auto target = targetName(key);
  CREDENTIALW cred{};
  cred.Type = CRED_TYPE_GENERIC;
  cred.TargetName = target.data();
  cred.CredentialBlobSize = static_cast<DWORD>(secret.size());
  cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
  cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
  std::wstring user = L"VoiceTwitch";
  cred.UserName = user.data();
  if (!CredWriteW(&cred, 0)) {
    log::error("CredWrite failed ({})", GetLastError());
    return false;
  }
  return true;
}

std::optional<std::string> load(const std::string& key) {
  PCREDENTIALW cred = nullptr;
  if (!CredReadW(targetName(key).c_str(), CRED_TYPE_GENERIC, 0, &cred)) return std::nullopt;
  std::string out(reinterpret_cast<const char*>(cred->CredentialBlob), cred->CredentialBlobSize);
  CredFree(cred);
  return out;
}

bool remove(const std::string& key) { return CredDeleteW(targetName(key).c_str(), CRED_TYPE_GENERIC, 0) != 0; }

const char* backendName() { return "Windows Credential Manager"; }

#elif defined(__APPLE__)

static CFStringRef cfstr(const std::string& s) {
  return CFStringCreateWithCString(kCFAllocatorDefault, s.c_str(), kCFStringEncodingUTF8);
}

static CFMutableDictionaryRef baseQuery(const std::string& key) {
  CFMutableDictionaryRef q = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                       &kCFTypeDictionaryValueCallBacks);
  CFStringRef svc = cfstr(kService);
  CFStringRef acct = cfstr(key);
  CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(q, kSecAttrService, svc);
  CFDictionarySetValue(q, kSecAttrAccount, acct);
  CFRelease(svc);
  CFRelease(acct);
  return q;
}

bool store(const std::string& key, const std::string& secret) {
  remove(key);
  CFMutableDictionaryRef q = baseQuery(key);
  CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(secret.data()),
                                static_cast<CFIndex>(secret.size()));
  CFDictionarySetValue(q, kSecValueData, data);
  OSStatus st = SecItemAdd(q, nullptr);
  CFRelease(data);
  CFRelease(q);
  if (st != errSecSuccess) {
    log::error("SecItemAdd failed ({})", static_cast<int>(st));
    return false;
  }
  return true;
}

std::optional<std::string> load(const std::string& key) {
  CFMutableDictionaryRef q = baseQuery(key);
  CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
  CFTypeRef result = nullptr;
  OSStatus st = SecItemCopyMatching(q, &result);
  CFRelease(q);
  if (st != errSecSuccess || !result) return std::nullopt;
  auto data = static_cast<CFDataRef>(result);
  std::string out(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), static_cast<size_t>(CFDataGetLength(data)));
  CFRelease(result);
  return out;
}

bool remove(const std::string& key) {
  CFMutableDictionaryRef q = baseQuery(key);
  OSStatus st = SecItemDelete(q);
  CFRelease(q);
  return st == errSecSuccess;
}

const char* backendName() { return "macOS Keychain"; }

#elif defined(VT_HAVE_LIBSECRET)

static const SecretSchema* schema() {
  static const SecretSchema s = {"com.voicetwitch.Secret",
                                 SECRET_SCHEMA_NONE,
                                 {{"key", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
  return &s;
}

bool store(const std::string& key, const std::string& secret) {
  GError* err = nullptr;
  std::string label = std::string(kService) + " " + key;
  gboolean ok = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, label.c_str(), secret.c_str(),
                                           nullptr, &err, "key", key.c_str(), nullptr);
  if (err) {
    log::error("libsecret store failed: {}", err->message);
    g_error_free(err);
  }
  return ok;
}

std::optional<std::string> load(const std::string& key) {
  GError* err = nullptr;
  gchar* pw = secret_password_lookup_sync(schema(), nullptr, &err, "key", key.c_str(), nullptr);
  if (err) {
    log::warn("libsecret lookup failed: {}", err->message);
    g_error_free(err);
  }
  if (!pw) return std::nullopt;
  std::string out(pw);
  secret_password_free(pw);
  return out;
}

bool remove(const std::string& key) {
  GError* err = nullptr;
  gboolean ok = secret_password_clear_sync(schema(), nullptr, &err, "key", key.c_str(), nullptr);
  if (err) g_error_free(err);
  return ok;
}

const char* backendName() { return "Secret Service (libsecret)"; }

#else  // plain-file fallback

static std::filesystem::path secretsFile() { return paths::dataDir() / "secrets.json"; }

static nlohmann::json readAll() {
  std::string text;
  if (!platform::readFile(secretsFile(), text)) return nlohmann::json::object();
  try {
    return nlohmann::json::parse(text);
  } catch (...) {
    return nlohmann::json::object();
  }
}

static bool writeAll(const nlohmann::json& j) {
  bool ok = platform::writeFileAtomic(secretsFile(), j.dump());
  chmod(secretsFile().c_str(), S_IRUSR | S_IWUSR);
  return ok;
}

bool store(const std::string& key, const std::string& secret) {
  static bool warned = false;
  if (!warned) {
    log::warn("No OS keychain available (built without libsecret); token stored in a 0600 file");
    warned = true;
  }
  auto j = readAll();
  j[key] = secret;
  return writeAll(j);
}

std::optional<std::string> load(const std::string& key) {
  auto j = readAll();
  if (auto it = j.find(key); it != j.end() && it->is_string()) return it->get<std::string>();
  return std::nullopt;
}

bool remove(const std::string& key) {
  auto j = readAll();
  j.erase(key);
  return writeAll(j);
}

const char* backendName() { return "file (0600) - libsecret unavailable"; }

#endif

}  // namespace vt::secure
