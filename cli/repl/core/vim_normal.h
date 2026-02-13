#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace xsql::cli {

struct VimNormalState {
  size_t prefix_count = 0;
  bool delete_pending = false;
  size_t delete_count = 0;
  size_t motion_count = 0;

  void clear();
};

struct VimNormalContext {
  std::string& buffer;
  size_t& cursor;

  std::function<size_t()> current_line_start;
  std::function<size_t()> current_line_end;
  std::function<void()> redraw;
  std::function<void(const std::string&, size_t)> push_undo_snapshot;
  std::function<void()> apply_undo;
  std::function<void(bool)> move_up;
  std::function<void(bool)> move_down;
  std::function<void()> enter_insert_mode;
};

/// Handles one keypress in Vim normal mode and mutates buffer/cursor state.
/// MUST consume digits/prefixes and support motions/edits needed by REPL.
bool handle_vim_normal_key(char key, VimNormalState& state, VimNormalContext& ctx);

}  // namespace xsql::cli
