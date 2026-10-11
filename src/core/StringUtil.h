#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vt {

std::string toLowerAscii(std::string_view s);
std::string trim(std::string_view s);
std::vector<std::string> split(std::string_view s, char sep);
bool startsWith(std::string_view s, std::string_view prefix);

// Decodes UTF-8 into code points. Invalid sequences become U+FFFD.
std::u32string utf8ToUtf32(std::string_view s);
std::string utf32ToUtf8(std::u32string_view s);
void appendUtf8(std::string& out, char32_t cp);

std::string urlEncode(std::string_view s);
std::string urlDecode(std::string_view s);
// Parses "a=1&b=2" (values URL-decoded).
std::map<std::string, std::string> parseQuery(std::string_view query);

std::string randomHex(size_t bytes);

#ifdef _WIN32
std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);
#endif

}  // namespace vt
