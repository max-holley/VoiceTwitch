#include "pipeline/BadWordFilter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/Platform.h"
#include "core/StringUtil.h"

namespace vt {

namespace {

// Keeps '*' so wildcard entries survive normalization.
std::string normalizeEntry(std::string_view entry) {
  std::string out;
  bool lastSpace = true;
  for (unsigned char c : entry) {
    if (c == '*' || std::isalnum(c) || c >= 0x80) {
      out.push_back(static_cast<char>(c < 0x80 ? std::tolower(c) : c));
      lastSpace = false;
    } else if (!lastSpace) {
      out.push_back(' ');
      lastSpace = true;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

bool endsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

}  // namespace

std::string BadWordFilter::normalize(std::string_view text) {
  std::string out;
  bool lastSpace = true;
  for (unsigned char c : text) {
    // Non-ASCII bytes are kept verbatim so UTF-8 words can still be matched.
    if (std::isalnum(c) || c >= 0x80) {
      out.push_back(static_cast<char>(c < 0x80 ? std::tolower(c) : c));
      lastSpace = false;
    } else if (!lastSpace) {
      out.push_back(' ');
      lastSpace = true;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

void BadWordFilter::setWords(std::vector<std::string> words) {
  std::vector<std::string> norm;
  for (auto& w : words) {
    auto n = normalizeEntry(w);
    if (!n.empty() && n.find_first_not_of('*') != std::string::npos) norm.push_back(std::move(n));
  }
  std::sort(norm.begin(), norm.end());
  norm.erase(std::unique(norm.begin(), norm.end()), norm.end());
  std::unique_lock lock(mutex_);
  words_ = std::move(norm);
}

std::vector<std::string> BadWordFilter::words() const {
  std::shared_lock lock(mutex_);
  return words_;
}

bool BadWordFilter::add(std::string_view word) {
  auto n = normalizeEntry(word);
  if (n.empty() || n.find_first_not_of('*') == std::string::npos) return false;
  std::unique_lock lock(mutex_);
  auto it = std::lower_bound(words_.begin(), words_.end(), n);
  if (it != words_.end() && *it == n) return false;
  words_.insert(it, std::move(n));
  return true;
}

bool BadWordFilter::remove(std::string_view word) {
  std::unique_lock lock(mutex_);
  auto it = std::find(words_.begin(), words_.end(), word);
  if (it == words_.end()) return false;
  words_.erase(it);
  return true;
}

void BadWordFilter::clear() {
  std::unique_lock lock(mutex_);
  words_.clear();
}

std::optional<std::string> BadWordFilter::match(std::string_view text) const {
  const std::string norm = normalize(text);
  if (norm.empty()) return std::nullopt;
  const std::string padded = " " + norm + " ";
  const auto tokens = split(norm, ' ');

  std::shared_lock lock(mutex_);
  for (const auto& entry : words_) {
    const bool lead = entry.front() == '*';
    const bool trail = entry.back() == '*';
    if (!lead && !trail) {
      if (padded.find(" " + entry + " ") != std::string::npos) return entry;
      continue;
    }
    std::string core = entry.substr(lead ? 1 : 0);
    if (trail && !core.empty()) core.pop_back();
    if (core.empty()) continue;
    if (lead && trail) {
      if (norm.find(core) != std::string::npos) return entry;
    } else if (core.find(' ') != std::string::npos) {
      // Wildcard on a phrase: treat as a substring match.
      if (norm.find(core) != std::string::npos) return entry;
    } else {
      for (const auto& t : tokens) {
        if ((trail && startsWith(t, core)) || (lead && endsWith(t, core))) return entry;
      }
    }
  }
  return std::nullopt;
}

bool BadWordFilter::load(const std::filesystem::path& path) {
  std::string text;
  if (!platform::readFile(path, text)) return false;
  try {
    auto j = nlohmann::json::parse(text);
    std::vector<std::string> words;
    if (auto it = j.find("words"); it != j.end() && it->is_array()) {
      for (auto& w : *it) {
        if (w.is_string()) words.push_back(w.get<std::string>());
      }
    }
    setWords(std::move(words));
    return true;
  } catch (...) {
    return false;
  }
}

bool BadWordFilter::save(const std::filesystem::path& path) const {
  nlohmann::json j;
  j["words"] = words();
  return platform::writeFileAtomic(path, j.dump(2));
}

size_t BadWordFilter::importFile(const std::filesystem::path& path, std::string& error) {
  std::ifstream f(path);
  if (!f) {
    error = "cannot open " + path.string();
    return 0;
  }
  size_t added = 0;
  std::string line;
  while (std::getline(f, line)) {
    if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
    // Accept comma-separated lists too.
    for (auto& part : split(line, ',')) {
      auto t = trim(part);
      if (!t.empty() && add(t)) ++added;
    }
  }
  return added;
}

}  // namespace vt
