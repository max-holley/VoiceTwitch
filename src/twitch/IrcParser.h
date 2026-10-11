#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vt::irc {

// One IRCv3 line: [@tags] [:prefix] COMMAND [params...] [:trailing]
struct Message {
  std::map<std::string, std::string> tags;
  std::string prefix;
  std::string command;
  std::vector<std::string> params;  // trailing param (if any) is last

  // "nick" part of "nick!user@host".
  std::string nick() const;
  std::string tag(const std::string& key) const;
  const std::string& trailing() const;
};

std::optional<Message> parseLine(std::string_view line);
std::string unescapeTagValue(std::string_view v);

}  // namespace vt::irc
