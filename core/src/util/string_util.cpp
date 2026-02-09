#include "string_util.h"

#include <cctype>
#include <optional>

namespace xsql::util {

namespace {

bool is_space(unsigned char c) {
  return std::isspace(c) != 0;
}

bool is_name_char(char c) {
  unsigned char uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) || c == '-' || c == '_' || c == ':';
}

bool is_tag_boundary_char(char c) {
  return std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '/' || c == '\0';
}

bool is_preserve_tag(const std::string& tag) {
  return tag == "pre" || tag == "code" || tag == "textarea" || tag == "script" || tag == "style";
}

size_t find_tag_end(std::string_view html, size_t start) {
  bool in_quote = false;
  char quote = '\0';
  for (size_t i = start; i < html.size(); ++i) {
    char c = html[i];
    if (in_quote) {
      if (c == quote) {
        in_quote = false;
      }
      continue;
    }
    if (c == '"' || c == '\'') {
      in_quote = true;
      quote = c;
      continue;
    }
    if (c == '>') return i;
  }
  return std::string_view::npos;
}

struct TagInfo {
  std::string name;
  bool is_end = false;
  bool self_closing = false;
};

std::optional<TagInfo> parse_tag_info(std::string_view html, size_t start, size_t end) {
  if (start >= html.size() || html[start] != '<' || end <= start) return std::nullopt;

  size_t i = start + 1;
  TagInfo info;
  if (i < end && html[i] == '/') {
    info.is_end = true;
    ++i;
  }

  while (i < end && std::isspace(static_cast<unsigned char>(html[i]))) {
    ++i;
  }

  size_t name_start = i;
  while (i < end && is_name_char(html[i])) {
    ++i;
  }
  if (name_start == i) return std::nullopt;

  info.name.assign(html.substr(name_start, i - name_start));
  info.name = to_lower(info.name);

  if (!info.is_end) {
    size_t j = end;
    while (j > start && std::isspace(static_cast<unsigned char>(html[j - 1]))) {
      --j;
    }
    if (j > start && html[j - 1] == '/') {
      info.self_closing = true;
    }
  }
  return info;
}

bool is_matching_close_tag(std::string_view html, size_t pos, std::string_view tag) {
  if (pos + 2 + tag.size() > html.size()) return false;
  if (html[pos] != '<' || html[pos + 1] != '/') return false;
  for (size_t i = 0; i < tag.size(); ++i) {
    char a = static_cast<char>(std::tolower(static_cast<unsigned char>(html[pos + 2 + i])));
    char b = static_cast<char>(std::tolower(static_cast<unsigned char>(tag[i])));
    if (a != b) return false;
  }
  char boundary = (pos + 2 + tag.size() < html.size()) ? html[pos + 2 + tag.size()] : '\0';
  return is_tag_boundary_char(boundary);
}

void append_compacted_text(std::string_view text,
                           bool adjacent_left_tag,
                           bool adjacent_right_tag,
                           std::string& out) {
  if (text.empty()) return;

  bool has_non_ws = false;
  for (char c : text) {
    if (!is_space(static_cast<unsigned char>(c))) {
      has_non_ws = true;
      break;
    }
  }

  if (!has_non_ws) {
    if (!(adjacent_left_tag || adjacent_right_tag)) {
      out.push_back(' ');
    }
    return;
  }

  bool leading_ws = is_space(static_cast<unsigned char>(text.front()));
  bool trailing_ws = is_space(static_cast<unsigned char>(text.back()));

  std::string compacted;
  compacted.reserve(text.size());
  bool in_space = false;
  for (char c : text) {
    if (is_space(static_cast<unsigned char>(c))) {
      if (!in_space) {
        compacted.push_back(' ');
        in_space = true;
      }
      continue;
    }
    in_space = false;
    compacted.push_back(c);
  }

  if (!leading_ws && !compacted.empty() && compacted.front() == ' ') {
    compacted.erase(compacted.begin());
  }
  if (!trailing_ws && !compacted.empty() && compacted.back() == ' ') {
    compacted.pop_back();
  }

  if (adjacent_left_tag && adjacent_right_tag && compacted == " ") {
    return;
  }
  out.append(compacted);
}

}  // namespace

std::string to_lower(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

std::string to_upper(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

std::string trim_ws(const std::string& s) {
  size_t start = 0;
  // WHY: trim only edges to preserve meaningful internal whitespace.
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

std::string minify_html(std::string_view html) {
  std::string out;
  out.reserve(html.size());

  bool prev_was_tag = false;
  std::optional<std::string> preserve_tag;
  size_t i = 0;
  while (i < html.size()) {
    if (preserve_tag.has_value()) {
      if (html[i] != '<' || !is_matching_close_tag(html, i, *preserve_tag)) {
        out.push_back(html[i]);
        ++i;
        continue;
      }
      size_t end = find_tag_end(html, i + 1);
      if (end == std::string_view::npos) {
        out.append(html.substr(i));
        break;
      }
      out.append(html.substr(i, end - i + 1));
      i = end + 1;
      preserve_tag.reset();
      prev_was_tag = true;
      continue;
    }

    if (html[i] == '<') {
      if (html.substr(i, 4) == "<!--") {
        size_t end = html.find("-->", i + 4);
        size_t stop = (end == std::string_view::npos) ? html.size() : end + 3;
        out.append(html.substr(i, stop - i));
        i = stop;
        prev_was_tag = true;
        continue;
      }

      size_t end = find_tag_end(html, i + 1);
      if (end == std::string_view::npos) {
        out.append(html.substr(i));
        break;
      }

      auto tag_info = parse_tag_info(html, i, end);
      out.append(html.substr(i, end - i + 1));
      if (tag_info.has_value() &&
          !tag_info->is_end &&
          !tag_info->self_closing &&
          is_preserve_tag(tag_info->name)) {
        preserve_tag = tag_info->name;
      }
      i = end + 1;
      prev_was_tag = true;
      continue;
    }

    size_t next = html.find('<', i);
    if (next == std::string_view::npos) next = html.size();
    size_t before = out.size();
    bool adjacent_left_tag = prev_was_tag;
    bool adjacent_right_tag = next < html.size();
    append_compacted_text(html.substr(i, next - i), adjacent_left_tag, adjacent_right_tag, out);
    if (out.size() > before) {
      prev_was_tag = false;
    }
    i = next;
  }
  return out;
}

}  // namespace xsql::util
