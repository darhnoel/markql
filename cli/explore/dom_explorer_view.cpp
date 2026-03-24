#include "explore/dom_explorer_internal.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

#include "cli_utils.h"
#include "repl/input/text_util.h"

namespace markql::cli::dom_explorer_internal {

namespace {

constexpr const char* kSelectedRowStyle = "\033[7m";
constexpr const char* kMatchHighlightStyle = "\033[1;33;4m";
constexpr const char* kAnsiReset = "\033[0m";

std::string ascii_lower(std::string_view text);

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

std::string base64_encode(std::string_view input) {
  static constexpr char kBase64Table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((input.size() + 2) / 3) * 4);

  size_t i = 0;
  while (i + 2 < input.size()) {
    unsigned char b0 = static_cast<unsigned char>(input[i++]);
    unsigned char b1 = static_cast<unsigned char>(input[i++]);
    unsigned char b2 = static_cast<unsigned char>(input[i++]);
    out.push_back(kBase64Table[b0 >> 2]);
    out.push_back(kBase64Table[((b0 & 0x03) << 4) | (b1 >> 4)]);
    out.push_back(kBase64Table[((b1 & 0x0F) << 2) | (b2 >> 6)]);
    out.push_back(kBase64Table[b2 & 0x3F]);
  }

  if (i < input.size()) {
    unsigned char b0 = static_cast<unsigned char>(input[i++]);
    out.push_back(kBase64Table[b0 >> 2]);
    if (i < input.size()) {
      unsigned char b1 = static_cast<unsigned char>(input[i++]);
      out.push_back(kBase64Table[((b0 & 0x03) << 4) | (b1 >> 4)]);
      out.push_back(kBase64Table[(b1 & 0x0F) << 2]);
      out.push_back('=');
    } else {
      out.push_back(kBase64Table[(b0 & 0x03) << 4]);
      out.push_back('=');
      out.push_back('=');
    }
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
      std::string token =
          (end == std::string::npos) ? inner_html.substr(i) : inner_html.substr(i, end - i + 1);
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
    std::string token =
        (end == std::string::npos) ? inner_html.substr(i) : inner_html.substr(i, end - i);
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

std::string ascii_lower(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

}  // namespace

CursorVisibilityGuard::CursorVisibilityGuard() {
  std::cout << "\033[?25l" << std::flush;
}

CursorVisibilityGuard::~CursorVisibilityGuard() {
  std::cout << "\033[?25h" << std::flush;
}

const char* search_mode_name(InnerHtmlSearchMode mode) {
  return mode == InnerHtmlSearchMode::Fuzzy ? "fuzzy" : "exact";
}

const char* suggestion_strategy_name(MarkqlSuggestionStrategy strategy) {
  if (strategy == MarkqlSuggestionStrategy::Project) return "PROJECT";
  if (strategy == MarkqlSuggestionStrategy::Flatten) return "FLATTEN";
  return "none";
}

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

int terminal_height() {
  winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
    return ws.ws_row;
  }
  return 24;
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

std::vector<std::string> boxed_panel_lines(const std::string& title,
                                           const std::vector<std::string>& content_lines,
                                           size_t pane_width, size_t pane_rows) {
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

std::vector<std::string> render_explorer_header_lines(size_t term_width,
                                                      InnerHtmlSearchMode search_mode) {
  if (term_width < 8) return {};

  std::vector<std::string> out;
  out.push_back(truncate_display_width(
      "MarkQL DOM Explorer  |  mode: " + std::string(search_mode_name(search_mode)) + " search",
      term_width));

  struct HeaderItem {
    std::string title;
    std::string content;
  };

  std::vector<HeaderItem> items = {
      {"Search", "/ text | m mode"},      {"Navigate", "n/N hits | arrows | Enter"},
      {"Suggest", "G generate | y copy"}, {"View", "j/k scroll | +/- zoom"},
      {"Reset", "C collapse-all"},        {"Quit", "q"},
  };

  struct PackedBox {
    size_t width = 0;
    std::vector<std::string> lines;
  };

  std::vector<PackedBox> boxes;
  boxes.reserve(items.size());
  for (const auto& item : items) {
    size_t title_w = column_width(item.title, 0, item.title.size());
    size_t content_w = column_width(item.content, 0, item.content.size());
    size_t box_width = std::max<size_t>(14, std::max(title_w, content_w) + 4);
    box_width = std::min(box_width, term_width);
    boxes.push_back({box_width, boxed_panel_lines(item.title, {item.content}, box_width, 3)});
  }

  constexpr size_t kGap = 1;
  std::vector<size_t> row_indices;
  size_t row_used = 0;
  auto flush_row = [&]() {
    if (row_indices.empty()) return;
    for (size_t line_idx = 0; line_idx < 3; ++line_idx) {
      std::string line;
      for (size_t i = 0; i < row_indices.size(); ++i) {
        if (i > 0) line.append(kGap, ' ');
        const auto& box = boxes[row_indices[i]];
        if (line_idx < box.lines.size()) line += box.lines[line_idx];
      }
      out.push_back(truncate_display_width(line, term_width));
    }
    row_indices.clear();
    row_used = 0;
  };

  for (size_t i = 0; i < boxes.size(); ++i) {
    size_t need = boxes[i].width + (row_indices.empty() ? 0 : kGap);
    if (!row_indices.empty() && row_used + need > term_width) {
      flush_row();
    }
    row_indices.push_back(i);
    row_used += boxes[i].width + (row_indices.size() > 1 ? kGap : 0);
  }
  flush_row();

  return out;
}

std::vector<std::string> render_right_pane_lines(
    const HtmlNode& node, size_t pane_width, size_t pane_rows, int inner_zoom_steps,
    size_t inner_html_scroll, size_t* max_inner_html_scroll, bool auto_focus_match,
    size_t* applied_inner_html_scroll, const std::optional<size_t>& match_position,
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

  size_t inner_line_budget =
      match_position.has_value() ? 2000 : std::max<size_t>(800, pane_rows * 20);
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
    size_t desired =
        (focus_line > inner_content_rows / 2) ? (focus_line - inner_content_rows / 2) : 0;
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
    if (highlight_visible_row.has_value() && highlight_query != nullptr &&
        !highlight_query->empty() && rows_inner >= 3) {
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

std::string format_tree_row(const HtmlNode& node, int depth, bool has_children, bool expanded,
                            bool selected, size_t max_width) {
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

void copy_to_clipboard_osc52(std::string_view text) {
  std::string payload = base64_encode(text);
  std::cout << "\033]52;c;" << payload << '\a';
}

}  // namespace markql::cli::dom_explorer_internal

namespace markql::cli {

using namespace dom_explorer_internal;

std::vector<std::string> render_attribute_lines(const markql::HtmlNode& node) {
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
  std::vector<std::pair<std::string, std::string>> attrs(node.attributes.begin(),
                                                         node.attributes.end());
  std::sort(attrs.begin(), attrs.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });
  for (const auto& [key, value] : attrs) {
    lines.push_back(key + " = " + value);
  }
  return lines;
}

}  // namespace markql::cli
