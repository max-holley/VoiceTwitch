#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "models/VoiceCatalog.h"

namespace vt {

struct InstalledVoice {
  std::string id;  // path of the .onnx relative to the data dir ("models/x/x.onnx")
  std::string name;
  bool cloned = false;
  std::filesystem::path onnxPath;
  std::filesystem::path modelCardPath;  // empty if none
  uint64_t sizeBytes = 0;
};

struct DownloadProgress {
  std::string key;
  uint64_t done = 0;
  uint64_t total = 0;
  bool active = false;
  std::string error;
};

// Manages Piper voices on disk: lists installed voices (downloaded and
// cloned), fetches the public catalog, downloads/deletes voices and exposes
// each voice's MODEL_CARD so its license is visible before and after download.
class ModelManager {
 public:
  ModelManager();
  ~ModelManager();

  void refreshInstalled();
  std::vector<InstalledVoice> installed() const;
  std::optional<InstalledVoice> findInstalled(const std::string& id) const;

  void loadCachedCatalog();
  void refreshCatalog();  // async
  bool catalogLoading() const { return catalogLoading_.load(); }
  std::string catalogError() const;
  std::vector<CatalogVoice> catalog() const;
  bool isInstalled(const std::string& key) const;

  void download(const CatalogVoice& voice);  // async
  void cancelDownload(const std::string& key);
  std::vector<DownloadProgress> downloads() const;
  void clearFinishedDownloads();

  bool remove(const InstalledVoice& voice, std::string& error);

  void fetchModelCard(const CatalogVoice& voice);  // async
  // nullopt = not fetched yet; "" = fetching.
  std::optional<std::string> modelCard(const std::string& key) const;

  static std::filesystem::path resolveVoiceId(const std::string& id);
  static std::string voiceIdFor(const std::filesystem::path& onnx);

  void setOnChange(std::function<void()> fn) { onChange_ = std::move(fn); }

 private:
  void spawn(std::function<void()> fn);
  void notify();
  void downloadJob(CatalogVoice voice);

  mutable std::mutex mutex_;
  std::vector<InstalledVoice> installed_;
  std::vector<CatalogVoice> catalog_;
  std::string catalogError_;
  std::atomic<bool> catalogLoading_{false};
  std::map<std::string, DownloadProgress> downloads_;
  std::map<std::string, std::shared_ptr<std::atomic<bool>>> cancelFlags_;
  std::map<std::string, std::string> modelCards_;
  std::atomic<bool> shuttingDown_{false};
  std::mutex threadsMutex_;
  std::vector<std::thread> threads_;
  std::function<void()> onChange_;
};

}  // namespace vt
