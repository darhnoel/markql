#include "vim_normal.h"

#include <algorithm>

#include "vim_edit.h"
#include "repl/input/text_util.h"

namespace xsql::cli {

namespace {

size_t effective_count(size_t raw_count) {
  return raw_count == 0 ? 1 : raw_count;
}

bool delete_vim_lines(std::string& buffer,
                      size_t& cursor,
                      size_t count,
                      size_t current_line_start) {
  if (buffer.empty()) return false;
  size_t start = std::min(current_line_start, buffer.size());
  size_t end = start;
  size_t lines_to_delete = effective_count(count);
  for (size_t i = 0; i < lines_to_delete && end < buffer.size(); ++i) {
    size_t line_break = buffer.find('\n', end);
    if (line_break == std::string::npos) {
      end = buffer.size();
      break;
    }
    end = line_break + 1;
  }
  if (end <= start) return false;

  // WHY: deleting the last line should not leave a dangling separator-only tail.
  if (end == buffer.size() && start > 0 && buffer[start - 1] == '\n') {
    --start;
  }

  buffer.erase(start, end - start);
  if (buffer.empty()) {
    cursor = 0;
    return true;
  }

  size_t anchor = std::min(start, buffer.size());
  size_t prev_break = (anchor == 0) ? std::string::npos : buffer.rfind('\n', anchor - 1);
  cursor = (prev_break == std::string::npos) ? 0 : prev_break + 1;
  return true;
}

}  // namespace

void VimNormalState::clear() {
  prefix_count = 0;
  delete_pending = false;
  delete_count = 0;
  motion_count = 0;
}

bool handle_vim_normal_key(char key, VimNormalState& state, VimNormalContext& ctx) {
  if (key >= '0' && key <= '9') {
    if (state.delete_pending) {
      state.motion_count = state.motion_count * 10 + static_cast<size_t>(key - '0');
      return true;
    }
    if (key == '0' && state.prefix_count == 0) {
      ctx.cursor = ctx.current_line_start();
      ctx.redraw();
      return true;
    }
    state.prefix_count = state.prefix_count * 10 + static_cast<size_t>(key - '0');
    return true;
  }

  if (state.delete_pending) {
    size_t total = effective_count(state.delete_count) * effective_count(state.motion_count);
    std::string prev_buffer = ctx.buffer;
    size_t prev_cursor = ctx.cursor;
    bool changed = false;
    if (key == 'd') {
      changed = delete_vim_lines(ctx.buffer, ctx.cursor, total, ctx.current_line_start());
    } else {
      changed = delete_vim_motion(ctx.buffer, ctx.cursor, total, key, ctx.current_line_end());
    }
    if (changed) {
      ctx.push_undo_snapshot(prev_buffer, prev_cursor);
      ctx.redraw();
    }
    state.clear();
    return true;
  }

  size_t count = effective_count(state.prefix_count);
  switch (key) {
    case 'u':
      ctx.apply_undo();
      break;
    case 'h':
      for (size_t i = 0; i < count && ctx.cursor > 0; ++i) {
        ctx.cursor = prev_codepoint_start(ctx.buffer, ctx.cursor);
      }
      ctx.redraw();
      break;
    case 'l':
      for (size_t i = 0; i < count && ctx.cursor < ctx.buffer.size(); ++i) {
        ctx.cursor = next_codepoint_start(ctx.buffer, ctx.cursor);
      }
      ctx.redraw();
      break;
    case 'x': {
      std::string prev_buffer = ctx.buffer;
      size_t prev_cursor = ctx.cursor;
      if (delete_vim_chars_under_cursor(ctx.buffer, ctx.cursor, count)) {
        ctx.push_undo_snapshot(prev_buffer, prev_cursor);
        ctx.redraw();
      }
      break;
    }
    case 'k':
      for (size_t i = 0; i < count; ++i) {
        if (ctx.buffer.find('\n') != std::string::npos) {
          ctx.move_up(false);
        } else {
          ctx.move_up(true);
        }
      }
      break;
    case 'j':
      for (size_t i = 0; i < count; ++i) {
        if (ctx.buffer.find('\n') != std::string::npos) {
          ctx.move_down(false);
        } else {
          ctx.move_down(true);
        }
      }
      break;
    case 'w':
    case 'W':
      ctx.cursor = move_vim_word_forward_n(ctx.buffer, ctx.cursor, count, key == 'W');
      ctx.redraw();
      break;
    case 'b':
    case 'B':
      ctx.cursor = move_vim_word_backward_n(ctx.buffer, ctx.cursor, count, key == 'B');
      ctx.redraw();
      break;
    case 16:  // Ctrl-P
      ctx.move_up(true);
      break;
    case 14:  // Ctrl-N
      ctx.move_down(true);
      break;
    case 'd':
      state.delete_pending = true;
      state.delete_count = count;
      state.motion_count = 0;
      state.prefix_count = 0;
      return true;
    case '0':
      ctx.cursor = ctx.current_line_start();
      ctx.redraw();
      break;
    case '$':
      ctx.cursor = ctx.current_line_end();
      ctx.redraw();
      break;
    case 'i':
      ctx.enter_insert_mode();
      break;
    case 'a':
      if (ctx.cursor < ctx.buffer.size()) {
        ctx.cursor = next_codepoint_start(ctx.buffer, ctx.cursor);
      }
      ctx.enter_insert_mode();
      break;
    case 'I':
      ctx.cursor = ctx.current_line_start();
      ctx.enter_insert_mode();
      break;
    case 'A':
      ctx.cursor = ctx.current_line_end();
      ctx.enter_insert_mode();
      break;
    case 'o': {
      if (ctx.buffer.empty()) {
        ctx.enter_insert_mode();
        break;
      }
      std::string prev_buffer = ctx.buffer;
      size_t prev_cursor = ctx.cursor;
      size_t line_end = ctx.current_line_end();
      size_t insert_pos = (line_end < ctx.buffer.size()) ? line_end + 1 : line_end;
      ctx.buffer.insert(ctx.buffer.begin() + static_cast<long>(insert_pos), '\n');
      ctx.cursor = insert_pos + 1;
      ctx.push_undo_snapshot(prev_buffer, prev_cursor);
      ctx.enter_insert_mode();
      break;
    }
    case 'O': {
      if (ctx.buffer.empty()) {
        ctx.enter_insert_mode();
        break;
      }
      std::string prev_buffer = ctx.buffer;
      size_t prev_cursor = ctx.cursor;
      size_t line_start = ctx.current_line_start();
      ctx.buffer.insert(ctx.buffer.begin() + static_cast<long>(line_start), '\n');
      ctx.cursor = line_start;
      ctx.push_undo_snapshot(prev_buffer, prev_cursor);
      ctx.enter_insert_mode();
      break;
    }
    default:
      break;
  }
  state.prefix_count = 0;
  return true;
}

}  // namespace xsql::cli
