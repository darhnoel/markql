#include "xsql/xsql.h"

#include <cctype>
#include <string>
#include <vector>

#include "../../util/string_util.h"

namespace xsql {

std::optional<int64_t> parse_int64_value(const std::string& value) {
  try {
    size_t idx = 0;
    int64_t out = std::stoll(value, &idx);
    if (idx != value.size()) return std::nullopt;
    return out;
  } catch (...) {
    return std::nullopt;
  }
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  std::string lower_haystack = util::to_lower(haystack);
  std::string lower_needle = util::to_lower(needle);
  return lower_haystack.find(lower_needle) != std::string::npos;
}

bool like_match_ci(const std::string& text, const std::string& pattern) {
  std::string s = util::to_lower(text);
  std::string p = util::to_lower(pattern);
  size_t si = 0;
  size_t pi = 0;
  size_t star = std::string::npos;
  size_t match = 0;
  while (si < s.size()) {
    if (pi < p.size() && (p[pi] == '_' || p[pi] == s[si])) {
      ++si;
      ++pi;
      continue;
    }
    if (pi < p.size() && p[pi] == '%') {
      star = pi++;
      match = si;
      continue;
    }
    if (star != std::string::npos) {
      pi = star + 1;
      si = ++match;
      continue;
    }
    return false;
  }
  while (pi < p.size() && p[pi] == '%') ++pi;
  return pi == p.size();
}

bool contains_all_ci(const std::string& haystack, const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (!contains_ci(haystack, token)) return false;
  }
  return true;
}

bool contains_any_ci(const std::string& haystack, const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (contains_ci(haystack, token)) return true;
  }
  return false;
}

std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
    }
    size_t start = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
    }
    if (start < i) out.push_back(s.substr(start, i - start));
  }
  return out;
}

}  // namespace xsql
