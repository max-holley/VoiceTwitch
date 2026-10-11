#include "twitch/IrcParser.h"

namespace vt::irc {

std::string Message::nick() const {
  auto bang = prefix.find('!');
  return bang == std::string::npos ? prefix : prefix.substr(0, bang);
}

std::string Message::tag(const std::string& key) const {
  auto it = tags.find(key);
  return it == tags.end() ? std::string{} : it->second;
}

const std::string& Message::trailing() const {
  static const std::string empty;
  return params.empty() ? empty : params.back();
}

std::string unescapeTagValue(std::string_view v) {
  std::string out;
  out.reserve(v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '\\' && i + 1 < v.size()) {
      char n = v[++i];
      switch (n) {
        case ':': out.push_back(';'); break;
        case 's': out.push_back(' '); break;
        case 'r': out.push_back('\r'); break;
        case 'n': out.push_back('\n'); break;
        case '\\': out.push_back('\\'); break;
        default: out.push_back(n); break;
      }
    } else if (v[i] != '\\') {
      out.push_back(v[i]);
    }
  }
  return out;
}

std::optional<Message> parseLine(std::string_view line) {
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
  if (line.empty()) return std::nullopt;

  Message m;
  size_t pos = 0;
  auto skipSpaces = [&] {
    while (pos < line.size() && line[pos] == ' ') ++pos;
  };

  if (line[pos] == '@') {
    size_t end = line.find(' ', pos);
    if (end == std::string_view::npos) return std::nullopt;
    std::string_view tags = line.substr(pos + 1, end - pos - 1);
    size_t start = 0;
    while (start <= tags.size()) {
      size_t semi = tags.find(';', start);
      std::string_view kv = tags.substr(start, semi == std::string_view::npos ? std::string_view::npos : semi - start);
      if (!kv.empty()) {
        size_t eq = kv.find('=');
        if (eq == std::string_view::npos) {
          m.tags[std::string(kv)] = "";
        } else {
          m.tags[std::string(kv.substr(0, eq))] = unescapeTagValue(kv.substr(eq + 1));
        }
      }
      if (semi == std::string_view::npos) break;
      start = semi + 1;
    }
    pos = end;
    skipSpaces();
  }

  if (pos < line.size() && line[pos] == ':') {
    size_t end = line.find(' ', pos);
    if (end == std::string_view::npos) return std::nullopt;
    m.prefix = std::string(line.substr(pos + 1, end - pos - 1));
    pos = end;
    skipSpaces();
  }

  size_t end = line.find(' ', pos);
  m.command = std::string(line.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos));
  if (m.command.empty()) return std::nullopt;
  pos = end == std::string_view::npos ? line.size() : end;

  while (pos < line.size()) {
    skipSpaces();
    if (pos >= line.size()) break;
    if (line[pos] == ':') {
      m.params.emplace_back(line.substr(pos + 1));
      break;
    }
    end = line.find(' ', pos);
    m.params.emplace_back(line.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos));
    pos = end == std::string_view::npos ? line.size() : end;
  }
  return m;
}

}  // namespace vt::irc
