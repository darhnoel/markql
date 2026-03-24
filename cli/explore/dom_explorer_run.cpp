#include "explore/dom_explorer_internal.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>

#include "cli_utils.h"
#include "repl/input/terminal.h"

namespace markql::cli {

using namespace dom_explorer_internal;

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
  InnerHtmlSearchMode search_match_mode =
      (cached_state != nullptr) ? cached_state->search_match_mode : InnerHtmlSearchMode::Exact;
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
  std::optional<MarkqlSuggestion> markql_suggestion = std::nullopt;
  std::string markql_suggestion_status;

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
    auto cache_key_for = [&](const std::string& query_key) {
      return std::string(search_mode_name(search_match_mode)) + "|" + query_key;
    };

    auto cache_put = [&](const std::string& query_key, std::vector<InnerHtmlSearchMatch>&& value) {
      constexpr size_t kSearchCacheMaxEntries = 12;
      std::string cache_key = cache_key_for(query_key);
      auto it = search_cache.find(cache_key);
      if (it == search_cache.end()) {
        search_cache.emplace(cache_key, std::move(value));
        search_cache_order.push_back(cache_key);
        if (search_cache_order.size() > kSearchCacheMaxEntries) {
          std::string evict = search_cache_order.front();
          search_cache_order.erase(search_cache_order.begin());
          search_cache.erase(evict);
        }
      } else {
        it->second = std::move(value);
      }
    };

    auto cache_hit = search_cache.find(cache_key_for(search_query));
    if (cache_hit != search_cache.end()) {
      search_matches = cache_hit->second;
    } else {
      const std::vector<InnerHtmlSearchMatch>* prefix_matches = nullptr;
      if (search_query.size() > 1) {
        for (size_t len = search_query.size() - 1; len > 0; --len) {
          auto pit = search_cache.find(cache_key_for(search_query.substr(0, len)));
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

      if (search_match_mode == InnerHtmlSearchMode::Fuzzy) {
        search_matches = fuzzy_search_inner_html(doc, search_query, doc.nodes.size(), false, false,
                                                 candidate_ptr);
      } else {
        search_matches = exact_search_inner_html(doc, search_query, doc.nodes.size(), false, false,
                                                 candidate_ptr);
      }
      cache_put(search_query, std::vector<InnerHtmlSearchMatch>(search_matches));
    }

    for (const auto& match : search_matches) {
      search_match_ids.insert(match.node_id);
      if (match.position_in_inner_html) {
        search_match_positions[match.node_id] = match.position;
      }
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

  auto nearest_visible_ancestor = [&](int64_t node_id) {
    int64_t current = node_id;
    size_t guard_counter = 0;
    while (current >= 0 && static_cast<size_t>(current) < doc.nodes.size() &&
           guard_counter < doc.nodes.size()) {
      const auto& node = doc.nodes[static_cast<size_t>(current)];
      if (!node.parent_id.has_value()) return current;
      int64_t parent_id = *node.parent_id;
      if (parent_id < 0 || static_cast<size_t>(parent_id) >= doc.nodes.size()) return current;
      current = parent_id;
      ++guard_counter;
    }
    if (!roots.empty()) return roots.front();
    return int64_t{0};
  };

  auto collapse_all_after_search = [&]() {
    int64_t current_id = selected_node_id;
    if (!visible.empty() && selected < visible.size()) {
      current_id = visible[selected].node_id;
    }

    search_mode = false;
    search_query.clear();
    search_dirty = false;
    refresh_search();

    expanded.clear();
    rebuild_visible();
    if (!visible.empty()) {
      int64_t anchor_id = nearest_visible_ancestor(current_id);
      selected = find_visible_index_by_node_id(visible, anchor_id);
      selected_node_id = visible[selected].node_id;
    }
    inner_html_scroll = 0;
    inner_html_scroll_user_adjusted = false;
  };

  auto expand_ancestors_for_node = [&](int64_t node_id) {
    int64_t current = node_id;
    size_t guard_counter = 0;
    while (current >= 0 && static_cast<size_t>(current) < doc.nodes.size() &&
           guard_counter < doc.nodes.size()) {
      const auto& node = doc.nodes[static_cast<size_t>(current)];
      if (!node.parent_id.has_value()) break;
      int64_t parent_id = *node.parent_id;
      if (parent_id < 0 || static_cast<size_t>(parent_id) >= children.size()) break;
      expanded.insert(parent_id);
      current = parent_id;
      ++guard_counter;
    }
  };

  auto toggle_search_match_mode = [&]() {
    search_match_mode = (search_match_mode == InnerHtmlSearchMode::Exact)
                            ? InnerHtmlSearchMode::Fuzzy
                            : InnerHtmlSearchMode::Exact;
    mark_search_dirty();
    apply_search_now(true);
  };

  auto generate_markql_suggestion = [&]() {
    if (visible.empty() || selected >= visible.size()) {
      markql_suggestion = std::nullopt;
      markql_suggestion_status = "No node selected.";
      return;
    }
    int64_t target_id = visible[selected].node_id;
    markql_suggestion = suggest_markql_statement(doc, target_id);
    markql_suggestion_status = "Generated. Press y to copy.";
  };

  auto copy_markql_suggestion = [&]() {
    if (!markql_suggestion.has_value() || markql_suggestion->statement.empty()) {
      markql_suggestion_status = "No suggestion yet. Press G first.";
      return;
    }
    copy_to_clipboard_osc52(markql_suggestion->statement);
    markql_suggestion_status = "Copied to clipboard.";
  };

  auto render = [&]() {
    int width = terminal_width();
    int height = terminal_height();
    if (width < 40) width = 40;
    if (height < 8) height = 8;
    std::vector<std::string> header_lines =
        render_explorer_header_lines(static_cast<size_t>(width), search_match_mode);
    const size_t kHeaderRows = header_lines.size();
    constexpr size_t kSearchBarRows = 3;
    const size_t chrome_rows = kHeaderRows + kSearchBarRows;
    const size_t content_width = static_cast<size_t>(std::max(1, width - 3));
    const size_t left_width = content_width / 2;
    const size_t right_width = content_width - left_width;
    const size_t body_rows =
        static_cast<size_t>(std::max(1, height - static_cast<int>(chrome_rows)));
    size_t suggest_rows = 0;
    if (body_rows >= 9) {
      suggest_rows = std::max<size_t>(3, body_rows / 3);
      constexpr size_t kMinTreeRows = 5;
      if (body_rows > kMinTreeRows && body_rows - suggest_rows < kMinTreeRows) {
        suggest_rows = body_rows - kMinTreeRows;
      }
    }
    size_t tree_rows = body_rows - suggest_rows;
    if (tree_rows == 0) {
      tree_rows = 1;
      suggest_rows = (body_rows > 1) ? (body_rows - 1) : 0;
    }
    ensure_selection_visible(tree_rows);

    std::vector<std::string> right_lines;
    size_t max_inner_html_scroll = 0;
    if (!visible.empty()) {
      const HtmlNode& selected_node = doc.nodes[static_cast<size_t>(visible[selected].node_id)];
      std::optional<size_t> match_position = std::nullopt;
      auto pos_it = search_match_positions.find(selected_node.id);
      if (pos_it != search_match_positions.end()) match_position = pos_it->second;
      bool auto_focus_match = match_position.has_value() && !inner_html_scroll_user_adjusted;
      size_t applied_scroll = inner_html_scroll;
      right_lines = render_right_pane_lines(
          selected_node, right_width, body_rows, inner_html_zoom_steps, inner_html_scroll,
          &max_inner_html_scroll, auto_focus_match, &applied_scroll, match_position,
          search_query.empty() ? nullptr : &search_query);
      inner_html_scroll = applied_scroll;
      if (inner_html_scroll > max_inner_html_scroll) {
        inner_html_scroll = max_inner_html_scroll;
      }
    } else {
      std::vector<std::string> no_result_lines;
      no_result_lines.push_back("mode: " + std::string(search_mode_name(search_match_mode)));
      no_result_lines.push_back("query: " +
                                (search_query.empty() ? std::string("(empty)") : search_query));
      no_result_lines.push_back("no matches");
      right_lines = boxed_panel_lines("Search", no_result_lines, right_width, body_rows);
    }

    std::vector<std::string> suggestion_box;
    if (suggest_rows > 0) {
      std::vector<std::string> suggest_lines;
      if (!markql_suggestion.has_value()) {
        suggest_lines.push_back("Press G to generate MarkQL suggestion");
        suggest_lines.push_back("for current selected node (y to copy)");
      } else {
        suggest_lines.push_back(
            "strategy: " + std::string(suggestion_strategy_name(markql_suggestion->strategy)) +
            "  confidence: " + std::to_string(markql_suggestion->confidence));
        if (!markql_suggestion->reason.empty()) {
          suggest_lines.push_back("reason: " + markql_suggestion->reason);
        }
        if (!markql_suggestion_status.empty()) {
          suggest_lines.push_back("status: " + markql_suggestion_status);
        }
        std::istringstream iss(markql_suggestion->statement);
        std::string line;
        while (std::getline(iss, line)) {
          suggest_lines.push_back(line);
        }
      }
      suggestion_box = boxed_panel_lines("Suggest", suggest_lines, left_width, suggest_rows);
    }

    std::cout << "\033[2J\033[H";
    for (const auto& line : header_lines) {
      std::cout << line << '\n';
    }
    std::string search_line = "/" + search_query;
    search_line += "  [" + std::string(search_mode_name(search_match_mode)) + "]";
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
    for (const auto& line : search_box) {
      std::cout << line << '\n';
    }

    for (size_t row = 0; row < body_rows; ++row) {
      std::string left_line;
      bool selected_row = false;
      if (row < tree_rows) {
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
      } else if (suggest_rows > 0 && row - tree_rows < suggestion_box.size()) {
        left_line = suggestion_box[row - tree_rows];
      }
      std::string right_line;
      if (row < right_lines.size()) {
        right_line = right_lines[row];
      }
      std::string left_cell = pad_display_width(left_line, left_width);
      if (selected_row) {
        left_cell = std::string("\033[7m") + left_cell + "\033[0m";
      }
      std::cout << left_cell << " | " << right_line;
      if (row + 1 < body_rows) std::cout << '\n';
    }
    std::cout << std::flush;
  };

  auto jump_search_result = [&](bool forward) {
    if (search_matches.empty()) return;

    int64_t current_id = selected_node_id;
    if (!visible.empty() && selected < visible.size()) {
      current_id = visible[selected].node_id;
    }

    size_t target_idx = forward ? 0 : (search_matches.size() - 1);
    auto it = std::find_if(
        search_matches.begin(), search_matches.end(),
        [&](const InnerHtmlSearchMatch& match) { return match.node_id == current_id; });
    if (it != search_matches.end()) {
      size_t current_idx = static_cast<size_t>(it - search_matches.begin());
      if (forward) {
        target_idx = (current_idx + 1) % search_matches.size();
      } else {
        target_idx = (current_idx == 0) ? (search_matches.size() - 1) : (current_idx - 1);
      }
    }

    int64_t target_id = search_matches[target_idx].node_id;
    expand_ancestors_for_node(target_id);
    rebuild_visible();
    if (visible.empty()) return;
    selected = find_visible_index_by_node_id(visible, target_id);
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
        if (node_id >= 0 && static_cast<size_t>(node_id) < children.size() &&
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
        } else if (key.ch == 'g' || key.ch == 'G') {
          generate_markql_suggestion();
        } else if (key.ch == 'y' || key.ch == 'Y') {
          copy_markql_suggestion();
        } else if (key.ch == 'C') {
          collapse_all_after_search();
        } else if (key.ch == 'm' || key.ch == 'M') {
          toggle_search_match_mode();
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
          if (node_id >= 0 && static_cast<size_t>(node_id) < children.size() &&
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
  saved_state.search_match_mode = search_match_mode;
  session_cache[session_key] = std::move(saved_state);

  std::cout << "\033[2J\033[H" << std::flush;
  return 0;
}

}  // namespace markql::cli
