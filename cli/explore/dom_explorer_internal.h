#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "explore/dom_explorer.h"
#include "explore/inner_html_search.h"
#include "explore/markql_suggestor.h"

namespace markql::cli::dom_explorer_internal {

struct ExplorerSessionState {
  std::unordered_set<int64_t> expanded_node_ids;
  int64_t selected_node_id = 0;
  int inner_html_zoom_steps = 0;
  size_t inner_html_scroll = 0;
  std::string search_query;
  InnerHtmlSearchMode search_match_mode = InnerHtmlSearchMode::Exact;
};

struct CursorVisibilityGuard {
  CursorVisibilityGuard();
  ~CursorVisibilityGuard();
};

const char* search_mode_name(InnerHtmlSearchMode mode);
const char* suggestion_strategy_name(MarkqlSuggestionStrategy strategy);
std::unordered_map<std::string, ExplorerSessionState>& explorer_session_cache();
std::string make_explorer_cache_key(const std::string& input);
int terminal_height();
bool wait_input_ready(int timeout_ms);
std::string truncate_display_width(const std::string& text, size_t width);
std::string pad_display_width(const std::string& text, size_t width);
std::string first_class_token(const std::string& value);
std::string compact_whitespace(std::string_view text);
std::vector<std::string> boxed_panel_lines(const std::string& title,
                                           const std::vector<std::string>& content_lines,
                                           size_t pane_width, size_t pane_rows);
std::vector<std::string> render_explorer_header_lines(size_t term_width,
                                                      InnerHtmlSearchMode search_mode);
std::vector<std::string> render_right_pane_lines(
    const HtmlNode& node, size_t pane_width, size_t pane_rows, int inner_zoom_steps,
    size_t inner_html_scroll, size_t* max_inner_html_scroll, bool auto_focus_match,
    size_t* applied_inner_html_scroll, const std::optional<size_t>& match_position,
    const std::string* highlight_query);
std::string format_tree_row(const HtmlNode& node, int depth, bool has_children, bool expanded,
                            bool selected, size_t max_width);

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

KeyInput read_key_event();
size_t find_visible_index_by_node_id(const std::vector<VisibleTreeRow>& rows, int64_t node_id);
void copy_to_clipboard_osc52(std::string_view text);

}  // namespace markql::cli::dom_explorer_internal
