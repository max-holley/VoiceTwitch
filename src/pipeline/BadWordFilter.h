#pragma once

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace vt {

// User-editable blocklist. A message matching any entry is dropped before it
// reaches the queue.
//
// Matching is case-insensitive on normalized text (punctuation -> spaces):
//   "word"        whole word
//   "two words"   whole phrase
//   "word*"       any word starting with "word"
//   "*word"       any word ending with "word"
//   "*word*"      "word" anywhere, even inside other words
class BadWordFilter {
 public:
  void setWords(std::vector<std::string> words);
  std::vector<std::string> words() const;
  bool add(std::string_view word);
  bool remove(std::string_view word);
  void clear();

  // Returns the first blocklist entry that matches, if any.
  std::optional<std::string> match(std::string_view text) const;

  bool load(const std::filesystem::path& path);
  bool save(const std::filesystem::path& path) const;
  // Imports one entry per line ('#' starts a comment). Returns count added.
  size_t importFile(const std::filesystem::path& path, std::string& error);

  // Lowercases and replaces non-alphanumerics with single spaces.
  static std::string normalize(std::string_view text);

 private:
  mutable std::shared_mutex mutex_;
  std::vector<std::string> words_;  // normalized, unique, sorted
};

}  // namespace vt
