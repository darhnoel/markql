#include "explore/dom_explorer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#include "cli_utils.h"
#include "explore/inner_html_search.h"
#include "repl/input/terminal.h"
#include "repl/input/text_util.h"

namespace xsql::cli {

namespace {

constexpr const char* kSelectedRowStyle = "\033[7m";
constexpr const char* kMatchHighlightStyle = "\033[1;33;4m";
constexpr const char* kAnsiReset = "\033[0m";

struct ExplorerSessionState {
  std::unordered_set<int64_t> expanded_node_ids;
  int64_t selected_node_id = 0;
  int inner_html_zoom_steps = 0;
  size_t inner_html_scroll = 0;
  std::string search_query;
};

std::unordered_map<std::string, ExplorerSessionState>& explorer_session_cache() {
  static std::unordered_map<std::string, ExplorerSessionState> cache;
  return cache;
}

std::string make_explorer_cache_key(const std::string& input) {
  if (is_url(input)) return "url:" + input;

  std::error_code ec;
  std::filesystem::path path(input);
  std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
  if (ec) {
    ec.clear();
    canonical = std::filesystem::absolute(path, ec);
    if (ec) {
      return "file:" + input;
    }
  }
  return "file:" + canonical.string();
}

struct CursorVisibilityGuard {
  CursorVisibilityGuard() { std::cout << "\033[?25l" << std::flush; }
  ~CursorVisibilityGuard() { std::cout << "\033[?25h" << std::flush; }
};

int terminal_height() {
  winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
    return ws.ws_row;
  }
  return 24;
}

bool read_byte_with_timeout(char* out, int timeout_ms) {
  if (!out) return false;
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  int ready = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
  if (ready <= 0) return false;
  return ::read(STDIN_FILENO, out, 1) > 0;
}

bool wait_input_ready(int timeout_ms) {
  if (timeout_ms < 0) timeout_ms = 0;
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  int ready = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
  return ready > 0;
}

std::string truncate_display_width(const std::string& text, size_t width) {
  if (width == 0) return "";
  if (column_width(text, 0, text.size()) <= width) return text;
  if (width <= 3) return std::string(width, '.');
  const size_t content_limit = width - 3;
  size_t i = 0;
  size_t used = 0;
  while (i < text.size()) {
    size_t bytes = 0;
    uint32_t cp = decode_utf8(text, i, &bytes);
    int cp_width = display_width(cp);
    if (used + static_cast<size_t>(std::max(cp_width, 0)) > content_limit) break;
    used += static_cast<size_t>(std::max(cp_width, 0));
    i += bytes ? bytes : 1;
  }
  return text.substr(0, i) + "...";
}

std::string pad_display_width(const std::string& text, size_t width) {
  std::string out = truncate_display_width(text, width);
  size_t used = column_width(out, 0, out.size());
  if (used < width) out.append(width - used, ' ');
  return out;
}

std::string first_class_token(const std::string& value) {
  size_t i = 0;
  while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i]))) ++i;
  size_t start = i;
  while (i < value.size() && !std::isspace(static_cast<unsigned char>(value[i]))) ++i;
  if (i <= start) return "";
  return value.substr(start, i - start);
}

std::string compact_whitespace(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool in_space = false;
  for (char c : text) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!in_space) {
        out.push_back(' ');
        in_space = true;
      }
      continue;
    }
    in_space = false;
    out.push_back(c);
  }
  size_t start = 0;
  while (start < out.size() && out[start] == ' ') ++start;
  size_t end = out.size();
  while (end > start && out[end - 1] == ' ') --end;
  return out.substr(start, end - start);
}

std::string safe_attr(const HtmlNode& node, std::string_view key) {
  auto it = node.attributes.find(std::string(key));
  return it == node.attributes.end() ? std::string() : it->second;
}

std::vector<std::pair<std::string, std::string>> sorted_attributes(const HtmlNode& node) {
  std::vector<std::pair<std::string, std::string>> attrs(node.attributes.begin(),
                                                          node.attributes.end());
  std::sort(attrs.begin(), attrs.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });
  return attrs;
}

std::string format_kv(const std::string& key, const std::string& value, size_t width) {
  std::string line = key + ": " + value;
  return truncate_display_width(line, width);
}

std::string repeat_utf8(const std::string& token, size_t count) {
  std::string out;
  out.reserve(token.size() * count);
  for (size_t i = 0; i < count; ++i) out += token;
  return out;
}

std::string ascii_lower(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

size_t find_ci_substr_ascii(std::string_view haystack, std::string_view needle) {
  if (needle.empty() || haystack.empty()) return std::string::npos;
  std::string lower_hay = ascii_lower(haystack);
  std::string lower_needle = ascii_lower(needle);
  return lower_hay.find(lower_needle);
}

std::string highlight_first_match_ascii(const std::string& line, const std::string& needle) {
  size_t pos = find_ci_substr_ascii(line, needle);
  if (needle.empty()) return line;
  size_t len = 0;
  if (pos != std::string::npos) {
    len = std::min(needle.size(), line.size() - pos);
  } else {
    std::string one(1, needle[0]);
    pos = find_ci_substr_ascii(line, one);
    if (pos == std::string::npos) return line;
    len = 1;
  }
  return line.substr(0, pos) + kMatchHighlightStyle + line.substr(pos, len) + kAnsiReset +
         line.substr(pos + len);
}

std::string highlight_match_in_box_row(const std::string& box_row, const std::string& needle) {
  if (needle.empty()) return box_row;
  size_t left = box_row.find("│ ");
  size_t right = box_row.rfind(" │");
  if (left == std::string::npos || right == std::string::npos || right <= left + 2) {
    return box_row;
  }
  size_t content_start = left + 2;
  size_t content_len = right - content_start;
  std::string content = box_row.substr(content_start, content_len);
  std::string highlighted = highlight_first_match_ascii(content, needle);
  return box_row.substr(0, content_start) + highlighted + box_row.substr(right);
}

bool is_html_self_closing_token(const std::string& token) {
  if (token.size() < 2) return false;
  if (token[token.size() - 2] == '/' && token.back() == '>') return true;
  if (token.rfind("<!", 0) == 0 || token.rfind("<?", 0) == 0) return true;
  return false;
}

struct PrettyInnerHtmlLine {
  std::string text;
  size_t source_offset = 0;
};

std::vector<PrettyInnerHtmlLine> pretty_inner_html_lines(const std::string& inner_html,
                                                         size_t max_lines) {
  std::vector<PrettyInnerHtmlLine> lines;
  lines.reserve(std::min<size_t>(max_lines, 64));
  if (inner_html.empty() || max_lines == 0) {
    lines.push_back({"(empty)", 0});
    return lines;
  }

  int indent = 0;
  size_t i = 0;
  while (i < inner_html.size() && lines.size() < max_lines) {
    if (inner_html[i] == '<') {
      size_t token_start = i;
      size_t end = inner_html.find('>', i);
      std::string token = (end == std::string::npos)
                              ? inner_html.substr(i)
                              : inner_html.substr(i, end - i + 1);
      i = (end == std::string::npos) ? inner_html.size() : end + 1;

      token = compact_whitespace(token);
      if (token.empty()) continue;
      bool is_close = token.rfind("</", 0) == 0;
      if (is_close && indent > 0) --indent;
      lines.push_back(
          {std::string(static_cast<size_t>(std::max(indent, 0)) * 2, ' ') + token, token_start});
      if (!is_close && !is_html_self_closing_token(token)) ++indent;
      continue;
    }

    size_t token_start = i;
    size_t end = inner_html.find('<', i);
    std::string token = (end == std::string::npos)
                            ? inner_html.substr(i)
                            : inner_html.substr(i, end - i);
    i = (end == std::string::npos) ? inner_html.size() : end;
    token = compact_whitespace(token);
    if (token.empty()) continue;
    lines.push_back(
        {std::string(static_cast<size_t>(std::max(indent, 0)) * 2, ' ') + token, token_start});
  }

  if (lines.empty()) {
    lines.push_back({"(empty)", 0});
  } else if (i < inner_html.size()) {
    lines.push_back({"...", i});
  }
  return lines;
}

std::vector<std::string> boxed_panel_lines(const std::string& title,
                                           const std::vector<std::string>& content_lines,
                                           size_t pane_width,
                                           size_t pane_rows) {
  std::vector<std::string> out;
  if (pane_rows == 0) return out;
  if (pane_width < 4) {
    out.reserve(pane_rows);
    for (size_t i = 0; i < pane_rows; ++i) {
      std::string line = i < content_lines.size() ? content_lines[i] : "";
      out.push_back(truncate_display_width(line, pane_width));
    }
    return out;
  }

  const size_t middle_width = pane_width - 2;
  const size_t inner_width = pane_width - 4;

  auto build_top_border = [&]() {
    std::string label = " " + title + " ";
    label = truncate_display_width(label, middle_width);
    size_t label_w = column_width(label, 0, label.size());
    size_t left = (middle_width > label_w) ? (middle_width - label_w) / 2 : 0;
    size_t right = (middle_width > label_w) ? (middle_width - label_w - left) : 0;
    std::string top = "┌";
    top += repeat_utf8("─", left);
    top += label;
    top += repeat_utf8("─", right);
    top += "┐";
    return top;
  };

  std::string bottom = "└";
  bottom += repeat_utf8("─", middle_width);
  bottom += "┘";

  if (pane_rows == 1) {
    out.push_back(build_top_border());
    return out;
  }
  if (pane_rows == 2) {
    out.push_back(build_top_border());
    out.push_back(bottom);
    return out;
  }

  out.reserve(pane_rows);
  out.push_back(build_top_border());
  const size_t body_rows = pane_rows - 2;
  for (size_t i = 0; i < body_rows; ++i) {
    std::string line = i < content_lines.size() ? content_lines[i] : "";
    line = truncate_display_width(line, inner_width);
    out.push_back("│ " + pad_display_width(line, inner_width) + " │");
  }
  out.push_back(bottom);
  return out;
}

std::vector<std::string> render_right_pane_lines(const HtmlNode& node,
                                                 size_t pane_width,
                                                 size_t pane_rows,
                                                 int inner_zoom_steps,
                                                 size_t inner_html_scroll,
                                                 size_t* max_inner_html_scroll,
                                                 bool auto_focus_match,
                                                 size_t* applied_inner_html_scroll,
                                                 const std::optional<size_t>& match_position,
                                                 const std::string* highlight_query) {
  if (pane_rows == 0) return {};

  std::vector<std::string> node_lines;
  node_lines.reserve(2);
  node_lines.push_back(format_kv("node_id", std::to_string(node.id), pane_width));
  node_lines.push_back(format_kv("tag", node.tag, pane_width));

  std::string inner_source = node.inner_html;
  std::optional<size_t> local_match_position = match_position;
  bool window_prefixed = false;
  bool window_suffixed = false;
  if (match_position.has_value() && !node.inner_html.empty()) {
    constexpr size_t kMatchWindowChars = 24000;
    size_t half_window = kMatchWindowChars / 2;
    size_t start = (*match_position > half_window) ? (*match_position - half_window) : 0;
    size_t end = std::min(node.inner_html.size(), *match_position + half_window);
    if (end > start && (start > 0 || end < node.inner_html.size())) {
      window_prefixed = start > 0;
      window_suffixed = end < node.inner_html.size();
      inner_source = node.inner_html.substr(start, end - start);
      local_match_position = *match_position - start;
    }
  }

  size_t inner_line_budget = match_position.has_value() ? 2000 : std::max<size_t>(800, pane_rows * 20);
  std::vector<PrettyInnerHtmlLine> inner_entries =
      pretty_inner_html_lines(inner_source, inner_line_budget);

  std::vector<std::string> attribute_lines;
  auto attrs = sorted_attributes(node);
  if (attrs.empty()) {
    attribute_lines.push_back("(no attributes)");
  } else {
    for (const auto& [key, value] : attrs) {
      attribute_lines.push_back(format_kv(key, value, pane_width));
    }
  }

  // Allocate rows so Inner HTML Head uses most available height by default.
  size_t rows_node = pane_rows >= 8 ? 4 : (pane_rows >= 5 ? 2 : 1);
  size_t rows_attr = pane_rows >= 10 ? 3 : (pane_rows >= 6 ? 2 : 1);
  if (rows_node + rows_attr >= pane_rows) {
    rows_attr = 1;
    if (rows_node + rows_attr >= pane_rows) rows_node = 1;
  }
  size_t rows_inner = pane_rows - rows_node - rows_attr;
  if (rows_inner == 0) {
    rows_inner = 1;
    if (rows_attr > 1) {
      --rows_attr;
    } else if (rows_node > 1) {
      --rows_node;
    }
  }

  size_t attr_target = std::min<size_t>(attribute_lines.size() + 2, std::max<size_t>(rows_attr, 6));
  while (rows_attr < attr_target && rows_inner > 2) {
    ++rows_attr;
    --rows_inner;
  }

  // Zoom changes vertical budget for Inner HTML by borrowing/giving lines.
  int target_delta = inner_zoom_steps * 2;
  size_t min_node_rows = pane_rows >= 8 ? 3 : 1;
  size_t min_attr_rows = pane_rows >= 8 ? 2 : 1;
  size_t min_inner_rows = pane_rows >= 8 ? 3 : 1;
  int max_grow = static_cast<int>((rows_node > min_node_rows ? rows_node - min_node_rows : 0) +
                                  (rows_attr > min_attr_rows ? rows_attr - min_attr_rows : 0));
  int max_shrink = static_cast<int>(rows_inner) - static_cast<int>(min_inner_rows);
  if (target_delta > max_grow) target_delta = max_grow;
  if (-target_delta > max_shrink) target_delta = -max_shrink;

  if (target_delta > 0) {
    int remaining = target_delta;
    while (remaining > 0) {
      bool progressed = false;
      if (rows_attr > min_attr_rows) {
        --rows_attr;
        --remaining;
        progressed = true;
      }
      if (remaining > 0 && rows_node > min_node_rows) {
        --rows_node;
        --remaining;
        progressed = true;
      }
      if (!progressed) break;
    }
    rows_inner += static_cast<size_t>(target_delta - remaining);
  } else if (target_delta < 0) {
    int give = -target_delta;
    while (give > 0 && rows_inner > min_inner_rows) {
      ++rows_attr;
      --rows_inner;
      --give;
      if (give > 0 && rows_inner > min_inner_rows) {
        ++rows_node;
        --rows_inner;
        --give;
      }
    }
  }

  size_t inner_content_rows = rows_inner > 2 ? rows_inner - 2 : 0;
  size_t max_scroll = 0;
  if (inner_content_rows > 0 && inner_entries.size() > inner_content_rows) {
    max_scroll = inner_entries.size() - inner_content_rows;
  }
  if (max_inner_html_scroll) *max_inner_html_scroll = max_scroll;
  size_t focus_line = 0;
  bool has_focus_line = false;
  if (local_match_position.has_value() && !inner_entries.empty()) {
    for (size_t idx = 0; idx < inner_entries.size(); ++idx) {
      if (inner_entries[idx].source_offset <= *local_match_position) {
        focus_line = idx;
      } else {
        break;
      }
    }
    has_focus_line = true;
  }

  size_t clamped_scroll = std::min(inner_html_scroll, max_scroll);
  if (auto_focus_match && has_focus_line && inner_content_rows > 0) {
    size_t desired = (focus_line > inner_content_rows / 2) ? (focus_line - inner_content_rows / 2) : 0;
    clamped_scroll = std::min(desired, max_scroll);
  }
  if (applied_inner_html_scroll) *applied_inner_html_scroll = clamped_scroll;

  std::vector<std::string> inner_html_lines;
  inner_html_lines.reserve(inner_content_rows > 0 ? inner_content_rows : 1);
  std::optional<size_t> highlight_visible_row = std::nullopt;
  if (inner_content_rows == 0 || inner_entries.empty()) {
    inner_html_lines.push_back("(empty)");
  } else {
    size_t end_idx = std::min(inner_entries.size(), clamped_scroll + inner_content_rows);
    for (size_t idx = clamped_scroll; idx < end_idx; ++idx) {
      std::string line = inner_entries[idx].text;
      if (has_focus_line && idx == focus_line) {
        highlight_visible_row = idx - clamped_scroll;
      }
      inner_html_lines.push_back(std::move(line));
    }
    if (!inner_html_lines.empty() && (clamped_scroll > 0 || window_prefixed)) {
      inner_html_lines.front() = "..." + inner_html_lines.front();
    }
    if (!inner_html_lines.empty() && (end_idx < inner_entries.size() || window_suffixed)) {
      inner_html_lines.back() = inner_html_lines.back() + "...";
    }
  }

  std::vector<std::string> out;
  out.reserve(pane_rows);
  if (rows_node > 0) {
    auto node_box = boxed_panel_lines("Node", node_lines, pane_width, rows_node);
    out.insert(out.end(), node_box.begin(), node_box.end());
  }
  if (rows_inner > 0) {
    auto inner_box = boxed_panel_lines("Inner HTML Head", inner_html_lines, pane_width, rows_inner);
    if (highlight_visible_row.has_value() &&
        highlight_query != nullptr &&
        !highlight_query->empty() &&
        rows_inner >= 3) {
      size_t row_in_box = 1 + *highlight_visible_row;
      if (row_in_box + 1 < inner_box.size()) {
        inner_box[row_in_box] = highlight_match_in_box_row(inner_box[row_in_box], *highlight_query);
      }
    }
    out.insert(out.end(), inner_box.begin(), inner_box.end());
  }
  if (rows_attr > 0) {
    auto attr_box = boxed_panel_lines("Attributes", attribute_lines, pane_width, rows_attr);
    out.insert(out.end(), attr_box.begin(), attr_box.end());
  }
  if (out.size() > pane_rows) {
    out.resize(pane_rows);
  } else if (out.size() < pane_rows) {
    out.resize(pane_rows, "");
  }
  return out;
}

std::string format_tree_row(const HtmlNode& node,
                            int depth,
                            bool has_children,
                            bool expanded,
                            bool selected,
                            size_t max_width) {
  std::string out;
  out.reserve(96);
  out += selected ? "> " : "  ";
  out.append(static_cast<size_t>(std::max(depth, 0)) * 2, ' ');
  if (has_children) {
    out += expanded ? "- " : "+ ";
  } else {
    out += "  ";
  }
  out += std::to_string(node.id);
  out.push_back(' ');
  out += node.tag;

  std::string id_attr = safe_attr(node, "id");
  if (!id_attr.empty()) {
    out += " #";
    out += id_attr;
  }
  std::string class_attr = first_class_token(safe_attr(node, "class"));
  if (!class_attr.empty()) {
    out += " .";
    out += class_attr;
  }
  std::string test_id = safe_attr(node, "data-testid");
  if (!test_id.empty()) {
    out += " data-testid=";
    out += truncate_display_width(test_id, 24);
  }
  return truncate_display_width(out, max_width);
}

enum class KeyEvent {
  None,
  Up,
  Down,
  Left,
  Right,
  Enter,
  ZoomIn,
  ZoomOut,
  SearchStart,
  SearchNext,
  SearchPrev,
  Backspace,
  Character,
  CancelSearch,
  Quit,
};

struct KeyInput {
  KeyEvent event = KeyEvent::None;
  char ch = 0;
};

KeyInput read_key_event() {
  char c = 0;
  if (::read(STDIN_FILENO, &c, 1) <= 0) return {KeyEvent::Quit, 0};
  if (c == '+' || c == '=') return {KeyEvent::ZoomIn, c};
  if (c == '-' || c == '_') return {KeyEvent::ZoomOut, c};
  if (c == '/') return {KeyEvent::SearchStart, c};
  if (c == 'n') return {KeyEvent::SearchNext, 0};
  if (c == 'N') return {KeyEvent::SearchPrev, 0};
  if (c == 127 || c == 8) return {KeyEvent::Backspace, 0};
  if (c == 'q' || c == 'Q') return {KeyEvent::Quit, c};
  if (c == '\n' || c == '\r') return {KeyEvent::Enter, 0};
  if (c == 27) {
    char seq0 = 0;
    char seq1 = 0;
    if (!read_byte_with_timeout(&seq0, 25) || !read_byte_with_timeout(&seq1, 25)) {
      return {KeyEvent::CancelSearch, 0};
    }
    if (seq0 == '[') {
      if (seq1 == 'A') return {KeyEvent::Up, 0};
      if (seq1 == 'B') return {KeyEvent::Down, 0};
      if (seq1 == 'C') return {KeyEvent::Right, 0};
      if (seq1 == 'D') return {KeyEvent::Left, 0};
    }
    return {KeyEvent::None, 0};
  }
  unsigned char uc = static_cast<unsigned char>(c);
  if (uc >= 0x20 && uc != 0x7F) {
    return {KeyEvent::Character, c};
  }
  return {KeyEvent::None, 0};
}

size_t find_visible_index_by_node_id(const std::vector<VisibleTreeRow>& rows, int64_t node_id) {
  for (size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].node_id == node_id) return i;
  }
  return rows.empty() ? 0 : rows.size() - 1;
}

}  // namespace

std::vector<std::vector<int64_t>> build_dom_children_index(const xsql::HtmlDocument& doc) {
  std::vector<std::vector<int64_t>> children(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (!node.parent_id.has_value()) continue;
    if (*node.parent_id < 0) continue;
    if (static_cast<size_t>(*node.parent_id) >= children.size()) continue;
    children[static_cast<size_t>(*node.parent_id)].push_back(node.id);
  }
  return children;
}

std::vector<int64_t> collect_dom_root_ids(const xsql::HtmlDocument& doc) {
  std::vector<int64_t> roots;
  roots.reserve(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (!node.parent_id.has_value() ||
        *node.parent_id < 0 ||
        static_cast<size_t>(*node.parent_id) >= doc.nodes.size()) {
      roots.push_back(node.id);
    }
  }
  return roots;
}

std::vector<VisibleTreeRow> flatten_visible_tree(
    const std::vector<int64_t>& roots,
    const std::vector<std::vector<int64_t>>& children,
    const std::unordered_set<int64_t>& expanded_node_ids) {
  std::vector<VisibleTreeRow> out;
  out.reserve(roots.size() * 2);
  std::vector<std::pair<int64_t, int>> stack;
  for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
    stack.push_back({*it, 0});
  }
  while (!stack.empty()) {
    auto [node_id, depth] = stack.back();
    stack.pop_back();
    out.push_back({node_id, depth});
    if (expanded_node_ids.find(node_id) == expanded_node_ids.end()) continue;
    if (node_id < 0 || static_cast<size_t>(node_id) >= children.size()) continue;
    const auto& kids = children[static_cast<size_t>(node_id)];
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
      stack.push_back({*it, depth + 1});
    }
  }
  return out;
}

std::vector<std::string> render_attribute_lines(const xsql::HtmlNode& node) {
  std::vector<std::string> lines;
  lines.push_back("node_id=" + std::to_string(node.id) + " tag=" + node.tag);
  std::string head = compact_whitespace(node.inner_html);
  if (head.empty()) {
    lines.push_back("inner_html_head = (empty)");
  } else {
    lines.push_back("inner_html_head = " + truncate_display_width(head, 96));
  }
  if (node.attributes.empty()) {
    lines.push_back("(no attributes)");
    return lines;
  }
  auto attrs = sorted_attributes(node);
  for (const auto& [key, value] : attrs) {
    lines.push_back(key + " = " + value);
  }
  return lines;
}

int run_dom_explorer_from_input(const std::string& input, std::ostream& err) {
  constexpr int kExploreLoadTimeoutMs = 5000;
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    err << "Error: explore mode requires an interactive terminal." << std::endl;
    return 2;
  }

  std::string html;
  try {
    html = load_html_input(input, kExploreLoadTimeoutMs);
  } catch (const std::exception& ex) {
    err << "Error: " << ex.what() << std::endl;
    return 2;
  }

  HtmlDocument doc = parse_html(html);
  if (doc.nodes.empty()) {
    err << "Error: no DOM nodes parsed from input: " << input << std::endl;
    return 1;
  }

  const std::string session_key = make_explorer_cache_key(input);
  auto& session_cache = explorer_session_cache();
  const auto cached_it = session_cache.find(session_key);
  const bool has_cached_state = cached_it != session_cache.end();
  const ExplorerSessionState* cached_state = has_cached_state ? &cached_it->second : nullptr;

  std::vector<std::vector<int64_t>> children = build_dom_children_index(doc);
  std::vector<int64_t> roots = collect_dom_root_ids(doc);
  std::unordered_set<int64_t> expanded;
  if (cached_state != nullptr) {
    for (int64_t node_id : cached_state->expanded_node_ids) {
      if (node_id < 0 || static_cast<size_t>(node_id) >= children.size()) continue;
      expanded.insert(node_id);
    }
  }
  std::vector<VisibleTreeRow> visible = flatten_visible_tree(roots, children, expanded);
  if (visible.empty()) {
    err << "Error: no visible nodes to render." << std::endl;
    return 1;
  }

  TermiosGuard guard;
  if (!guard.ok()) {
    err << "Error: failed to initialize terminal raw mode." << std::endl;
    return 1;
  }
  CursorVisibilityGuard cursor_guard;

  size_t selected = 0;
  if (cached_state != nullptr) {
    selected = find_visible_index_by_node_id(visible, cached_state->selected_node_id);
  }
  size_t scroll_top = 0;
  int inner_html_zoom_steps = 0;
  if (cached_state != nullptr) {
    inner_html_zoom_steps = std::clamp(cached_state->inner_html_zoom_steps, -8, 8);
  }
  size_t inner_html_scroll = 0;
  if (cached_state != nullptr) {
    inner_html_scroll = cached_state->inner_html_scroll;
  }
  bool search_mode = false;
  std::string search_query = (cached_state != nullptr) ? cached_state->search_query : "";
  std::vector<InnerHtmlSearchMatch> search_matches;
  std::unordered_set<int64_t> search_match_ids;
  std::unordered_map<int64_t, size_t> search_match_positions;
  std::unordered_map<std::string, std::vector<InnerHtmlSearchMatch>> search_cache;
  std::vector<std::string> search_cache_order;
  bool search_dirty = false;
  auto search_last_edit_at = std::chrono::steady_clock::now();
  constexpr size_t kAutoSearchMinChars = 2;
  bool inner_html_scroll_user_adjusted = false;
  bool running = true;
  int64_t selected_node_id = visible[selected].node_id;

  auto ensure_selection_visible = [&](size_t body_rows) {
    if (visible.empty()) {
      selected = 0;
      scroll_top = 0;
      return;
    }
    if (selected >= visible.size()) selected = visible.size() - 1;
    if (selected < scroll_top) {
      scroll_top = selected;
    } else if (selected >= scroll_top + body_rows) {
      scroll_top = selected - body_rows + 1;
    }
  };

  auto build_visible_rows = [&]() {
    std::vector<VisibleTreeRow> base = flatten_visible_tree(roots, children, expanded);
    if (search_query.empty() || search_query.size() < kAutoSearchMinChars) return base;
    std::vector<VisibleTreeRow> filtered;
    filtered.reserve(base.size());
    for (const auto& row : base) {
      if (search_match_ids.find(row.node_id) != search_match_ids.end()) {
        filtered.push_back(row);
      }
    }
    return filtered;
  };

  auto jump_to_best_match = [&]() {
    if (search_matches.empty() || visible.empty()) return;
    int64_t best_node_id = search_matches.front().node_id;
    selected = find_visible_index_by_node_id(visible, best_node_id);
  };

  auto rebuild_visible = [&]() {
    int64_t selected_id = selected_node_id;
    if (!visible.empty()) {
      selected_id = visible[selected].node_id;
    }
    visible = build_visible_rows();
    if (visible.empty()) return;
    selected = find_visible_index_by_node_id(visible, selected_id);
    selected_node_id = visible[selected].node_id;
  };

  auto refresh_search = [&]() {
    search_matches.clear();
    search_match_ids.clear();
    search_match_positions.clear();
    if (search_query.empty()) return;

    auto cache_put = [&](const std::string& query_key, std::vector<InnerHtmlSearchMatch>&& value) {
      constexpr size_t kSearchCacheMaxEntries = 12;
      auto it = search_cache.find(query_key);
      if (it == search_cache.end()) {
        search_cache.emplace(query_key, std::move(value));
        search_cache_order.push_back(query_key);
        if (search_cache_order.size() > kSearchCacheMaxEntries) {
          std::string evict = search_cache_order.front();
          search_cache_order.erase(search_cache_order.begin());
          search_cache.erase(evict);
        }
      } else {
        it->second = std::move(value);
      }
    };

    auto cache_hit = search_cache.find(search_query);
    if (cache_hit != search_cache.end()) {
      search_matches = cache_hit->second;
    } else {
      const std::vector<InnerHtmlSearchMatch>* prefix_matches = nullptr;
      if (search_query.size() > 1) {
        for (size_t len = search_query.size() - 1; len > 0; --len) {
          auto pit = search_cache.find(search_query.substr(0, len));
          if (pit != search_cache.end()) {
            prefix_matches = &pit->second;
            break;
          }
        }
      }

      std::vector<int64_t> candidate_node_ids;
      const std::vector<int64_t>* candidate_ptr = nullptr;
      if (prefix_matches != nullptr) {
        if (prefix_matches->empty()) {
          cache_put(search_query, {});
          return;
        }
        candidate_node_ids.reserve(prefix_matches->size());
        for (const auto& match : *prefix_matches) {
          candidate_node_ids.push_back(match.node_id);
        }
        candidate_ptr = &candidate_node_ids;
      }

      search_matches = fuzzy_search_inner_html(doc,
                                               search_query,
                                               doc.nodes.size(),
                                               false,
                                               false,
                                               candidate_ptr);
      cache_put(search_query, std::vector<InnerHtmlSearchMatch>(search_matches));
    }

    for (const auto& match : search_matches) {
      search_match_ids.insert(match.node_id);
      search_match_positions[match.node_id] = match.position;
    }
  };

  auto apply_search_now = [&](bool force) {
    if (!search_dirty) return;
    if (search_query.size() < kAutoSearchMinChars && !force) {
      search_matches.clear();
      search_match_ids.clear();
      search_match_positions.clear();
      rebuild_visible();
      search_dirty = false;
      return;
    }
    refresh_search();
    rebuild_visible();
    jump_to_best_match();
    inner_html_scroll = 0;
    inner_html_scroll_user_adjusted = false;
    search_dirty = false;
  };

  auto mark_search_dirty = [&]() {
    search_dirty = true;
    search_last_edit_at = std::chrono::steady_clock::now();
  };

  auto append_search_char = [&](char ch) {
    if (ch == 0) return;
    search_query.push_back(ch);
    mark_search_dirty();
  };

  auto render = [&]() {
    int width = terminal_width();
    int height = terminal_height();
    if (width < 40) width = 40;
    if (height < 8) height = 8;
    constexpr size_t kHeaderRows = 1;
    constexpr size_t kSearchBarRows = 3;
    const size_t chrome_rows = kHeaderRows + kSearchBarRows;
    const size_t content_width = static_cast<size_t>(std::max(1, width - 3));
    const size_t left_width = content_width / 2;
    const size_t right_width = content_width - left_width;
    const size_t body_rows = static_cast<size_t>(std::max(1, height - static_cast<int>(chrome_rows)));
    ensure_selection_visible(body_rows);

    std::vector<std::string> right_lines;
    size_t max_inner_html_scroll = 0;
    if (!visible.empty()) {
      const HtmlNode& selected_node = doc.nodes[static_cast<size_t>(visible[selected].node_id)];
      std::optional<size_t> match_position = std::nullopt;
      auto pos_it = search_match_positions.find(selected_node.id);
      if (pos_it != search_match_positions.end()) match_position = pos_it->second;
      bool auto_focus_match = match_position.has_value() && !inner_html_scroll_user_adjusted;
      size_t applied_scroll = inner_html_scroll;
      right_lines = render_right_pane_lines(selected_node, right_width, body_rows,
                                            inner_html_zoom_steps, inner_html_scroll,
                                            &max_inner_html_scroll, auto_focus_match,
                                            &applied_scroll, match_position,
                                            search_query.empty() ? nullptr : &search_query);
      inner_html_scroll = applied_scroll;
      if (inner_html_scroll > max_inner_html_scroll) {
        inner_html_scroll = max_inner_html_scroll;
      }
    } else {
      std::vector<std::string> no_result_lines;
      no_result_lines.push_back("query: " + (search_query.empty() ? std::string("(empty)") : search_query));
      no_result_lines.push_back("no matches");
      right_lines = boxed_panel_lines("Search", no_result_lines, right_width, body_rows);
    }

    std::cout << "\033[2J\033[H";
    std::string header =
        "MarkQL DOM Explorer | / search | n/N next/prev | j/k scroll inner_html | +/- zoom | q quit";
    std::cout << truncate_display_width(header, static_cast<size_t>(width)) << "\n";
    std::string search_line = "/" + search_query;
    if (search_mode) search_line += " _";
    if (search_query.empty()) {
      search_line += "  (type / then text)";
    } else {
      search_line += "  [" + std::to_string(search_matches.size()) + " match";
      if (search_matches.size() != 1) search_line += "es";
      search_line += "]";
    }
    if (search_mode && search_dirty) {
      if (search_query.size() < kAutoSearchMinChars) {
        search_line += "  (type >=2 chars or Enter)";
      } else {
        search_line += "  (pending)";
      }
    }
    auto search_box =
        boxed_panel_lines("Search", {search_line}, static_cast<size_t>(width), kSearchBarRows);
    for (size_t i = 0; i < search_box.size(); ++i) {
      std::cout << search_box[i];
      std::cout << '\n';
    }

    for (size_t row = 0; row < body_rows; ++row) {
      std::string left_line;
      bool selected_row = false;
      size_t idx = scroll_top + row;
      if (idx < visible.size()) {
        const auto& vr = visible[idx];
        const HtmlNode& node = doc.nodes[static_cast<size_t>(vr.node_id)];
        bool has_children = vr.node_id >= 0 &&
                            static_cast<size_t>(vr.node_id) < children.size() &&
                            !children[static_cast<size_t>(vr.node_id)].empty();
        bool is_expanded = expanded.find(vr.node_id) != expanded.end();
        selected_row = (idx == selected);
        left_line = format_tree_row(node, vr.depth, has_children, is_expanded, idx == selected,
                                    left_width);
      } else if (row == 0 && !search_query.empty()) {
        left_line = "(no matches)";
      }
      std::string right_line;
      if (row < right_lines.size()) {
        right_line = right_lines[row];
      }
      std::string left_cell = pad_display_width(left_line, left_width);
      if (selected_row) {
        left_cell = std::string(kSelectedRowStyle) + left_cell + "\033[0m";
      }
      std::cout << left_cell
                << " | "
                << right_line;
      if (row + 1 < body_rows) std::cout << '\n';
    }
    std::cout << std::flush;
  };

  auto jump_search_result = [&](bool forward) {
    if (visible.empty()) return;
    if (forward) {
      selected = (selected + 1) % visible.size();
    } else {
      selected = (selected == 0) ? (visible.size() - 1) : (selected - 1);
    }
    selected_node_id = visible[selected].node_id;
    inner_html_scroll = 0;
    inner_html_scroll_user_adjusted = false;
  };

  if (!search_query.empty()) {
    refresh_search();
    rebuild_visible();
  }
  if (!visible.empty()) {
    selected_node_id = visible[selected].node_id;
  }

  render();
  while (running) {
    if (search_mode && search_dirty) {
      constexpr int kSearchDebounceMs = 220;
      constexpr int kSearchPollMs = 30;
      auto now = std::chrono::steady_clock::now();
      int elapsed_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now - search_last_edit_at).count());
      if (elapsed_ms >= kSearchDebounceMs) {
        // If another key is already buffered, keep typing smooth and defer search.
        if (!wait_input_ready(0)) {
          apply_search_now(false);
          render();
          continue;
        }
      }
      int wait_ms = std::min(kSearchPollMs, kSearchDebounceMs - elapsed_ms);
      if (!wait_input_ready(wait_ms)) {
        continue;
      }
    }

    int64_t selected_before = selected_node_id;
    KeyInput key = read_key_event();
    switch (key.event) {
      case KeyEvent::Quit:
        if (search_mode) {
          append_search_char(key.ch);
        } else {
          running = false;
        }
        break;
      case KeyEvent::Up:
        if (!visible.empty() && selected > 0) --selected;
        break;
      case KeyEvent::Down:
        if (!visible.empty() && selected + 1 < visible.size()) ++selected;
        break;
      case KeyEvent::Right: {
        if (visible.empty()) break;
        int64_t node_id = visible[selected].node_id;
        if (node_id >= 0 &&
            static_cast<size_t>(node_id) < children.size() &&
            !children[static_cast<size_t>(node_id)].empty()) {
          if (expanded.insert(node_id).second) {
            rebuild_visible();
          }
        }
        break;
      }
      case KeyEvent::Left: {
        if (visible.empty()) break;
        int64_t node_id = visible[selected].node_id;
        auto it = expanded.find(node_id);
        if (it != expanded.end()) {
          expanded.erase(it);
          rebuild_visible();
        }
        break;
      }
      case KeyEvent::ZoomIn:
        if (search_mode) {
          append_search_char(key.ch);
        } else {
          if (inner_html_zoom_steps < 8) ++inner_html_zoom_steps;
          inner_html_scroll_user_adjusted = false;
        }
        break;
      case KeyEvent::ZoomOut:
        if (search_mode) {
          append_search_char(key.ch);
        } else {
          if (inner_html_zoom_steps > -8) --inner_html_zoom_steps;
          inner_html_scroll_user_adjusted = false;
        }
        break;
      case KeyEvent::SearchStart:
        if (search_mode) {
          append_search_char(key.ch);
        } else {
          search_mode = true;
        }
        break;
      case KeyEvent::SearchNext:
        if (search_mode) {
          append_search_char('n');
        } else {
          apply_search_now(true);
          jump_search_result(true);
        }
        break;
      case KeyEvent::SearchPrev:
        if (search_mode) {
          append_search_char('N');
        } else {
          apply_search_now(true);
          jump_search_result(false);
        }
        break;
      case KeyEvent::Backspace:
        if (search_mode && !search_query.empty()) {
          search_query.pop_back();
          mark_search_dirty();
        }
        break;
      case KeyEvent::Character:
        if (search_mode) {
          append_search_char(key.ch);
        } else if (key.ch == 'j') {
          ++inner_html_scroll;
          inner_html_scroll_user_adjusted = true;
        } else if (key.ch == 'k') {
          if (inner_html_scroll > 0) --inner_html_scroll;
          inner_html_scroll_user_adjusted = true;
        }
        break;
      case KeyEvent::CancelSearch:
        if (search_mode) {
          search_mode = false;
          search_query.clear();
          search_dirty = false;
          refresh_search();
          rebuild_visible();
          inner_html_scroll = 0;
          inner_html_scroll_user_adjusted = false;
        }
        break;
      case KeyEvent::Enter:
        if (search_mode) {
          apply_search_now(true);
          search_mode = false;
        } else {
          if (visible.empty()) break;
          int64_t node_id = visible[selected].node_id;
          if (node_id >= 0 &&
              static_cast<size_t>(node_id) < children.size() &&
              !children[static_cast<size_t>(node_id)].empty()) {
            if (expanded.insert(node_id).second) {
              rebuild_visible();
            }
          }
        }
        break;
      case KeyEvent::None:
        break;
    }
    if (!visible.empty()) {
      selected_node_id = visible[selected].node_id;
      if (selected_node_id != selected_before) {
        inner_html_scroll = 0;
        inner_html_scroll_user_adjusted = false;
      }
    }
    if (running) render();
  }

  ExplorerSessionState saved_state;
  saved_state.expanded_node_ids = expanded;
  saved_state.selected_node_id = selected_node_id;
  saved_state.inner_html_zoom_steps = inner_html_zoom_steps;
  saved_state.inner_html_scroll = inner_html_scroll;
  saved_state.search_query = search_query;
  session_cache[session_key] = std::move(saved_state);

  std::cout << "\033[2J\033[H" << std::flush;
  return 0;
}

}  // namespace xsql::cli
