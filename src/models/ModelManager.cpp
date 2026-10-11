#include "models/ModelManager.h"

#include <algorithm>
#include <system_error>

#include "core/Http.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Platform.h"

namespace vt {

namespace fs = std::filesystem;

namespace {

uint64_t dirSize(const fs::path& dir) {
  uint64_t total = 0;
  std::error_code ec;
  for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    if (it->is_regular_file(ec)) total += it->file_size(ec);
  }
  return total;
}

// Finds `<dir>/*.onnx` that has a sibling `.onnx.json`.
std::optional<fs::path> findModel(const fs::path& dir) {
  std::error_code ec;
  for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    const auto& p = it->path();
    if (p.extension() == ".onnx") {
      auto cfg = p;
      cfg += ".json";
      if (fs::exists(cfg, ec)) return p;
    }
  }
  return std::nullopt;
}

std::string fileName(const std::string& repoPath) {
  auto slash = repoPath.rfind('/');
  return slash == std::string::npos ? repoPath : repoPath.substr(slash + 1);
}

}  // namespace

ModelManager::ModelManager() = default;

ModelManager::~ModelManager() {
  shuttingDown_ = true;
  {
    std::lock_guard lock(mutex_);
    for (auto& [k, flag] : cancelFlags_) *flag = true;
  }
  std::lock_guard lock(threadsMutex_);
  for (auto& t : threads_) {
    if (t.joinable()) t.join();
  }
}

void ModelManager::spawn(std::function<void()> fn) {
  std::lock_guard lock(threadsMutex_);
  // Reap finished threads opportunistically is not possible portably without
  // extra state; the count stays small (one per user action).
  threads_.emplace_back(std::move(fn));
}

void ModelManager::notify() {
  if (onChange_) onChange_();
}

fs::path ModelManager::resolveVoiceId(const std::string& id) {
  if (id.empty()) return {};
  fs::path p = paths::fromUtf8(id);
  return p.is_absolute() ? p : paths::dataDir() / p;
}

std::string ModelManager::voiceIdFor(const fs::path& onnx) {
  std::error_code ec;
  auto rel = fs::relative(onnx, paths::dataDir(), ec);
  if (ec || rel.empty() || *rel.begin() == "..") return paths::toUtf8(onnx);
  return paths::toUtf8(rel);
}

void ModelManager::refreshInstalled() {
  std::vector<InstalledVoice> list;
  std::error_code ec;
  auto scan = [&](const fs::path& root, bool cloned) {
    for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
      if (!it->is_directory(ec)) continue;
      auto name = paths::toUtf8(it->path().filename());
      if (name.ends_with(".partial")) continue;
      auto model = findModel(it->path());
      if (!model) continue;
      InstalledVoice v;
      v.onnxPath = *model;
      v.id = voiceIdFor(*model);
      v.name = paths::toUtf8(model->stem());
      v.cloned = cloned;
      v.sizeBytes = dirSize(it->path());
      if (auto card = it->path() / "MODEL_CARD"; fs::exists(card, ec)) v.modelCardPath = card;
      list.push_back(std::move(v));
    }
  };
  scan(paths::modelsDir(), false);
  scan(paths::clonedVoicesDir(), true);
  std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
    if (a.cloned != b.cloned) return a.cloned;  // cloned voices first
    return a.name < b.name;
  });
  {
    std::lock_guard lock(mutex_);
    installed_ = std::move(list);
  }
  notify();
}

std::vector<InstalledVoice> ModelManager::installed() const {
  std::lock_guard lock(mutex_);
  return installed_;
}

std::optional<InstalledVoice> ModelManager::findInstalled(const std::string& id) const {
  std::lock_guard lock(mutex_);
  for (const auto& v : installed_) {
    if (v.id == id) return v;
  }
  return std::nullopt;
}

bool ModelManager::isInstalled(const std::string& key) const {
  std::lock_guard lock(mutex_);
  return std::any_of(installed_.begin(), installed_.end(),
                     [&](const InstalledVoice& v) { return !v.cloned && v.name == key; });
}

std::string ModelManager::catalogError() const {
  std::lock_guard lock(mutex_);
  return catalogError_;
}

std::vector<CatalogVoice> ModelManager::catalog() const {
  std::lock_guard lock(mutex_);
  return catalog_;
}

void ModelManager::loadCachedCatalog() {
  std::string text;
  if (!platform::readFile(paths::modelsDir() / "voices.json", text)) return;
  std::vector<CatalogVoice> voices;
  std::string err;
  if (parseCatalog(text, voices, err)) {
    std::lock_guard lock(mutex_);
    catalog_ = std::move(voices);
  }
}

void ModelManager::refreshCatalog() {
  if (catalogLoading_.exchange(true)) return;
  notify();
  spawn([this] {
    auto r = http::get(std::string(kPiperVoicesBaseUrl) + kPiperVoicesIndex);
    std::vector<CatalogVoice> voices;
    std::string err;
    if (!r.ok()) {
      err = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
    } else if (parseCatalog(r.body, voices, err)) {
      platform::writeFileAtomic(paths::modelsDir() / "voices.json", r.body);
    }
    {
      std::lock_guard lock(mutex_);
      if (err.empty()) {
        catalog_ = std::move(voices);
        catalogError_.clear();
        log::info("Voice catalog loaded ({} voices)", catalog_.size());
      } else {
        catalogError_ = "Could not load voice list: " + err;
        log::error("{}", catalogError_);
      }
    }
    catalogLoading_ = false;
    notify();
  });
}

void ModelManager::download(const CatalogVoice& voice) {
  {
    std::lock_guard lock(mutex_);
    auto it = downloads_.find(voice.key);
    if (it != downloads_.end() && it->second.active) return;
    DownloadProgress p;
    p.key = voice.key;
    p.total = voice.totalSize();
    p.active = true;
    downloads_[voice.key] = p;
    cancelFlags_[voice.key] = std::make_shared<std::atomic<bool>>(false);
  }
  notify();
  spawn([this, voice] { downloadJob(voice); });
}

void ModelManager::downloadJob(CatalogVoice voice) {
  std::shared_ptr<std::atomic<bool>> cancel;
  {
    std::lock_guard lock(mutex_);
    cancel = cancelFlags_[voice.key];
  }
  const fs::path finalDir = paths::modelsDir() / voice.key;
  fs::path tmpDir = finalDir;
  tmpDir += ".partial";
  std::error_code ec;
  fs::remove_all(tmpDir, ec);
  fs::create_directories(tmpDir, ec);

  std::string error;
  uint64_t completedBytes = 0;
  for (const auto& file : voice.files) {
    const auto dest = tmpDir / fileName(file.path);
    const std::string url = std::string(kPiperVoicesBaseUrl) + file.path;
    bool ok = http::downloadFile(
        url, dest,
        [&](uint64_t now, uint64_t) {
          {
            std::lock_guard lock(mutex_);
            downloads_[voice.key].done = completedBytes + now;
          }
          notify();
          return !*cancel && !shuttingDown_;
        },
        error);
    if (!ok) break;
    if (file.sizeBytes > 0 && fs::file_size(dest, ec) != file.sizeBytes) {
      error = "size mismatch for " + fileName(file.path);
      break;
    }
    completedBytes += file.sizeBytes;
  }

  if (error.empty()) {
    fs::remove_all(finalDir, ec);
    fs::rename(tmpDir, finalDir, ec);
    if (ec) error = "could not move voice into place: " + ec.message();
  }
  if (!error.empty()) fs::remove_all(tmpDir, ec);

  {
    std::lock_guard lock(mutex_);
    auto& p = downloads_[voice.key];
    p.active = false;
    p.error = error;
    if (error.empty()) p.done = p.total;
  }
  if (error.empty()) {
    log::info("Downloaded voice {}", voice.key);
  } else {
    log::error("Download of {} failed: {}", voice.key, error);
  }
  if (!shuttingDown_) refreshInstalled();
  notify();
}

void ModelManager::cancelDownload(const std::string& key) {
  std::lock_guard lock(mutex_);
  if (auto it = cancelFlags_.find(key); it != cancelFlags_.end()) *it->second = true;
}

std::vector<DownloadProgress> ModelManager::downloads() const {
  std::lock_guard lock(mutex_);
  std::vector<DownloadProgress> out;
  for (const auto& [k, p] : downloads_) out.push_back(p);
  return out;
}

void ModelManager::clearFinishedDownloads() {
  std::lock_guard lock(mutex_);
  for (auto it = downloads_.begin(); it != downloads_.end();) {
    it = it->second.active ? std::next(it) : downloads_.erase(it);
  }
}

bool ModelManager::remove(const InstalledVoice& voice, std::string& error) {
  std::error_code ec;
  auto dir = voice.onnxPath.parent_path();
  if (voice.cloned) {
    // Keep the recordings; delete only the trained model files.
    auto cfg = voice.onnxPath;
    cfg += ".json";
    fs::remove(voice.onnxPath, ec);
    fs::remove(cfg, ec);
  } else {
    // Safety: only delete directories inside the models folder.
    auto rel = fs::relative(dir, paths::modelsDir(), ec);
    if (ec || rel.empty() || *rel.begin() == ".." || rel == ".") {
      error = "refusing to delete outside the models folder";
      return false;
    }
    fs::remove_all(dir, ec);
  }
  if (ec) {
    error = ec.message();
    return false;
  }
  refreshInstalled();
  return true;
}

void ModelManager::fetchModelCard(const CatalogVoice& voice) {
  const auto* card = voice.modelCard();
  {
    std::lock_guard lock(mutex_);
    if (modelCards_.count(voice.key)) return;
    modelCards_[voice.key] = card ? "" : "This voice has no MODEL_CARD in the catalog.";
  }
  if (!card) return;
  std::string key = voice.key;
  std::string url = std::string(kPiperVoicesBaseUrl) + card->path;
  spawn([this, key, url] {
    auto r = http::get(url);
    {
      std::lock_guard lock(mutex_);
      modelCards_[key] = r.ok() ? r.body : "Could not fetch model card: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
    }
    notify();
  });
}

std::optional<std::string> ModelManager::modelCard(const std::string& key) const {
  std::lock_guard lock(mutex_);
  auto it = modelCards_.find(key);
  if (it == modelCards_.end()) return std::nullopt;
  return it->second;
}

}  // namespace vt
