#include "line_editor.h"

#include <iostream>
#include <sys/select.h>
#include <unistd.h>
#include <vector>
#include "repl/ui/autocomplete.h"
#include "repl/ui/render.h"
#include "repl/input/terminal.h"
#include "repl/input/text_util.h"

namespace xsql::cli {

namespace {

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

}  // namespace

LineEditor::LineEditor(size_t max_history, std::string prompt, size_t prompt_len)
    : history_(max_history),
      prompt_(std::move(prompt)),
      prompt_len_(prompt_len),
      normal_prompt_(prompt_),
      normal_prompt_len_(prompt_len_),
      cont_prompt_(""),
      cont_prompt_len_(0),
      completer_(std::make_unique<AutoCompleter>()) {}

LineEditor::~LineEditor() = default;

void LineEditor::set_prompt(std::string prompt, size_t prompt_len) {
  normal_prompt_ = std::move(prompt);
  normal_prompt_len_ = prompt_len;
  if (editor_mode_ == EditorMode::Normal) {
    prompt_ = normal_prompt_;
    prompt_len_ = normal_prompt_len_;
  }
}

void LineEditor::set_cont_prompt(std::string prompt, size_t prompt_len) {
  cont_prompt_ = std::move(prompt);
  cont_prompt_len_ = prompt_len;
}

void LineEditor::set_keyword_color(bool enabled) {
  keyword_color_ = enabled;
}

void LineEditor::set_mode_prompts(std::string vim_normal_prompt,
                                  size_t vim_normal_prompt_len,
                                  std::string vim_insert_prompt,
                                  size_t vim_insert_prompt_len) {
  vim_normal_prompt_ = std::move(vim_normal_prompt);
  vim_normal_prompt_len_ = vim_normal_prompt_len;
  vim_insert_prompt_ = std::move(vim_insert_prompt);
  vim_insert_prompt_len_ = vim_insert_prompt_len;
}

void LineEditor::set_editor_mode(EditorMode mode) {
  editor_mode_ = mode;
  vim_insert_mode_ = true;
  if (editor_mode_ == EditorMode::Normal) {
    prompt_ = normal_prompt_;
    prompt_len_ = normal_prompt_len_;
    return;
  }
  if (!vim_insert_prompt_.empty()) {
    prompt_ = vim_insert_prompt_;
    prompt_len_ = vim_insert_prompt_len_;
    return;
  }
  prompt_ = normal_prompt_;
  prompt_len_ = normal_prompt_len_;
}

void LineEditor::reset_render_state() {
  last_render_lines_ = 1;
  last_cursor_line_ = 0;
}

bool LineEditor::read_line(std::string& out, const std::string& initial) {
  out.clear();
  if (!isatty(fileno(stdin))) {
    return static_cast<bool>(std::getline(std::cin, out));
  }

  TermiosGuard guard;
  if (!guard.ok()) {
    return static_cast<bool>(std::getline(std::cin, out));
  }

  std::string buffer = initial;
  size_t cursor = buffer.size();
  history_.reset_navigation();

  bool paste_mode = false;
  std::string paste_buffer;
  std::string utf8_pending;
  size_t utf8_expected = 0;
  if (editor_mode_ == EditorMode::Vim) {
    vim_insert_mode_ = true;
  } else {
    vim_insert_mode_ = false;
  }

  auto current_line_start = [&]() -> size_t {
    size_t line_start = buffer.rfind('\n', cursor > 0 ? cursor - 1 : 0);
    return (line_start == std::string::npos) ? 0 : line_start + 1;
  };

  auto current_line_end = [&]() -> size_t {
    size_t line_start = current_line_start();
    size_t line_end = buffer.find('\n', line_start);
    return (line_end == std::string::npos) ? buffer.size() : line_end;
  };

  struct UndoState {
    std::string buffer;
    size_t cursor = 0;
  };
  std::vector<UndoState> undo_stack;
  constexpr size_t kMaxUndoStates = 256;

  auto push_undo_snapshot = [&](const std::string& prev_buffer, size_t prev_cursor) {
    if (!undo_stack.empty() &&
        undo_stack.back().buffer == prev_buffer &&
        undo_stack.back().cursor == prev_cursor) {
      return;
    }
    if (undo_stack.size() >= kMaxUndoStates) {
      undo_stack.erase(undo_stack.begin());
    }
    undo_stack.push_back(UndoState{prev_buffer, prev_cursor});
  };

  auto push_undo_current = [&]() {
    push_undo_snapshot(buffer, cursor);
  };

  auto apply_undo = [&]() {
    if (undo_stack.empty()) return;
    UndoState state = std::move(undo_stack.back());
    undo_stack.pop_back();
    buffer = std::move(state.buffer);
    cursor = std::min(state.cursor, buffer.size());
    redraw_line(buffer, cursor);
  };

  size_t vim_prefix_count = 0;
  bool vim_delete_pending = false;
  size_t vim_delete_count = 0;
  size_t vim_motion_count = 0;

  auto clear_vim_command_state = [&]() {
    vim_prefix_count = 0;
    vim_delete_pending = false;
    vim_delete_count = 0;
    vim_motion_count = 0;
  };

  auto effective_count = [](size_t raw_count) -> size_t {
    return raw_count == 0 ? 1 : raw_count;
  };

  enum class WordClass { Space, Keyword, Other };

  auto is_space_cp = [](uint32_t cp) -> bool {
    if (cp <= 0x7F) {
      return std::isspace(static_cast<unsigned char>(cp)) != 0;
    }
    return cp == 0x00A0 || cp == 0x3000;
  };

  auto classify_cp = [&](uint32_t cp, bool big_word) -> WordClass {
    if (is_space_cp(cp)) return WordClass::Space;
    if (big_word) return WordClass::Keyword;
    if (cp <= 0x7F) {
      unsigned char c = static_cast<unsigned char>(cp);
      if (std::isalnum(c) || c == '_') return WordClass::Keyword;
      return WordClass::Other;
    }
    // WHY: treat non-ASCII scripts (e.g., Japanese) as word characters for Vim motions.
    return WordClass::Keyword;
  };

  auto move_word_forward_once = [&](size_t pos, bool big_word) -> size_t {
    if (pos >= buffer.size()) return buffer.size();
    size_t i = pos;
    size_t bytes = 0;
    uint32_t cp = decode_utf8(buffer, i, &bytes);
    WordClass cls = classify_cp(cp, big_word);
    if (cls == WordClass::Space) {
      while (i < buffer.size()) {
        cp = decode_utf8(buffer, i, &bytes);
        if (classify_cp(cp, big_word) != WordClass::Space) break;
        i += bytes ? bytes : 1;
      }
      return i;
    }
    while (i < buffer.size()) {
      cp = decode_utf8(buffer, i, &bytes);
      if (classify_cp(cp, big_word) != cls) break;
      i += bytes ? bytes : 1;
    }
    if (i < buffer.size()) {
      cp = decode_utf8(buffer, i, &bytes);
      if (classify_cp(cp, big_word) == WordClass::Space) {
        while (i < buffer.size()) {
          cp = decode_utf8(buffer, i, &bytes);
          if (classify_cp(cp, big_word) != WordClass::Space) break;
          i += bytes ? bytes : 1;
        }
      }
    }
    return i;
  };

  auto move_word_backward_once = [&](size_t pos, bool big_word) -> size_t {
    if (pos == 0) return 0;
    size_t i = prev_codepoint_start(buffer, pos);
    size_t bytes = 0;
    uint32_t cp = 0;
    while (true) {
      cp = decode_utf8(buffer, i, &bytes);
      if (!is_space_cp(cp)) break;
      if (i == 0) return 0;
      i = prev_codepoint_start(buffer, i);
    }
    WordClass cls = classify_cp(cp, big_word);
    while (i > 0) {
      size_t prev = prev_codepoint_start(buffer, i);
      uint32_t prev_cp = decode_utf8(buffer, prev, &bytes);
      WordClass prev_cls = classify_cp(prev_cp, big_word);
      if (prev_cls == WordClass::Space || prev_cls != cls) break;
      i = prev;
    }
    return i;
  };

  auto move_word_forward_n = [&](size_t pos, size_t count, bool big_word) -> size_t {
    size_t out = pos;
    for (size_t i = 0; i < count; ++i) {
      size_t next = move_word_forward_once(out, big_word);
      if (next == out) break;
      out = next;
    }
    return out;
  };

  auto move_word_backward_n = [&](size_t pos, size_t count, bool big_word) -> size_t {
    size_t out = pos;
    for (size_t i = 0; i < count; ++i) {
      size_t prev = move_word_backward_once(out, big_word);
      if (prev == out) break;
      out = prev;
    }
    return out;
  };

  auto erase_range = [&](size_t start, size_t end) {
    if (start >= end || start >= buffer.size()) return;
    push_undo_current();
    end = std::min(end, buffer.size());
    buffer.erase(start, end - start);
    cursor = start;
    redraw_line(buffer, cursor);
  };

  auto apply_mode_prompt = [&]() {
    if (editor_mode_ == EditorMode::Normal) {
      prompt_ = normal_prompt_;
      prompt_len_ = normal_prompt_len_;
      return;
    }
    if (vim_insert_mode_) {
      if (!vim_insert_prompt_.empty()) {
        prompt_ = vim_insert_prompt_;
        prompt_len_ = vim_insert_prompt_len_;
      } else {
        prompt_ = normal_prompt_;
        prompt_len_ = normal_prompt_len_;
      }
      return;
    }
    if (!vim_normal_prompt_.empty()) {
      prompt_ = vim_normal_prompt_;
      prompt_len_ = vim_normal_prompt_len_;
    } else {
      prompt_ = normal_prompt_;
      prompt_len_ = normal_prompt_len_;
    }
  };

  auto move_up = [&](bool allow_history_fallback = true) {
    if (buffer.find('\n') != std::string::npos) {
      size_t line_start = buffer.rfind('\n', cursor > 0 ? cursor - 1 : 0);
      size_t current_line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
      if (current_line_start == 0) {
        if (allow_history_fallback && !history_.empty()) {
          std::string prev_buffer = buffer;
          size_t prev_cursor = cursor;
          if (history_.prev(buffer)) {
            push_undo_snapshot(prev_buffer, prev_cursor);
            cursor = buffer.size();
            redraw_line(buffer, cursor);
          }
        }
        return;
      }
      size_t prev_line_end = current_line_start - 1;
      size_t prev_line_start = buffer.rfind('\n', prev_line_end > 0 ? prev_line_end - 1 : 0);
      prev_line_start = (prev_line_start == std::string::npos) ? 0 : prev_line_start + 1;
      size_t col = column_width(buffer, current_line_start, cursor);
      size_t current_line_end_pos = buffer.find('\n', current_line_start);
      if (current_line_end_pos == std::string::npos) current_line_end_pos = buffer.size();
      size_t current_len = column_width(buffer, current_line_start, current_line_end_pos);
      size_t prev_len = column_width(buffer, prev_line_start, prev_line_end);
      cursor = column_to_index(buffer,
                               prev_line_start,
                               prev_line_end,
                               proportional_column(col, current_len, prev_len));
      redraw_line(buffer, cursor);
    } else if (allow_history_fallback && !history_.empty()) {
      std::string prev_buffer = buffer;
      size_t prev_cursor = cursor;
      if (history_.prev(buffer)) {
        push_undo_snapshot(prev_buffer, prev_cursor);
        cursor = buffer.size();
        redraw_line(buffer, cursor);
      }
    }
  };

  auto move_down = [&](bool allow_history_fallback = true) {
    if (buffer.find('\n') != std::string::npos) {
      size_t line_start = buffer.rfind('\n', cursor > 0 ? cursor - 1 : 0);
      size_t current_line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
      size_t line_end = buffer.find('\n', current_line_start);
      if (line_end == std::string::npos) {
        if (allow_history_fallback) {
          std::string prev_buffer = buffer;
          size_t prev_cursor = cursor;
          if (history_.next(buffer)) {
            push_undo_snapshot(prev_buffer, prev_cursor);
            cursor = buffer.size();
            redraw_line(buffer, cursor);
          }
        }
        return;
      }
      size_t next_line_start = line_end + 1;
      size_t next_line_end = buffer.find('\n', next_line_start);
      if (next_line_end == std::string::npos) {
        next_line_end = buffer.size();
      }
      size_t col = column_width(buffer, current_line_start, cursor);
      size_t current_len = column_width(buffer, current_line_start, line_end);
      size_t next_len = column_width(buffer, next_line_start, next_line_end);
      cursor = column_to_index(buffer,
                               next_line_start,
                               next_line_end,
                               proportional_column(col, current_len, next_len));
      redraw_line(buffer, cursor);
    } else if (allow_history_fallback && !history_.empty()) {
      std::string prev_buffer = buffer;
      size_t prev_cursor = cursor;
      if (history_.next(buffer)) {
        push_undo_snapshot(prev_buffer, prev_cursor);
        cursor = buffer.size();
        redraw_line(buffer, cursor);
      }
    }
  };

  auto enter_insert_mode = [&]() {
    editor_mode_ = EditorMode::Vim;
    vim_insert_mode_ = true;
    clear_vim_command_state();
    apply_mode_prompt();
    redraw_line(buffer, cursor);
  };

  auto enter_vim_normal_mode = [&]() {
    editor_mode_ = EditorMode::Vim;
    vim_insert_mode_ = false;
    clear_vim_command_state();
    apply_mode_prompt();
    redraw_line(buffer, cursor);
  };

  auto enter_normal_mode = [&]() {
    editor_mode_ = EditorMode::Normal;
    vim_insert_mode_ = false;
    clear_vim_command_state();
    apply_mode_prompt();
    redraw_line(buffer, cursor);
  };

  auto handle_vim_normal_key = [&](char key) {
    if (key >= '0' && key <= '9') {
      if (vim_delete_pending) {
        vim_motion_count = vim_motion_count * 10 + static_cast<size_t>(key - '0');
        return;
      }
      if (key == '0' && vim_prefix_count == 0) {
        cursor = current_line_start();
        redraw_line(buffer, cursor);
        return;
      }
      vim_prefix_count = vim_prefix_count * 10 + static_cast<size_t>(key - '0');
      return;
    }

    if (vim_delete_pending) {
      size_t total = effective_count(vim_delete_count) * effective_count(vim_motion_count);
      switch (key) {
        case 'w':
        case 'W': {
          size_t end = move_word_forward_n(cursor, total, key == 'W');
          erase_range(cursor, end);
          break;
        }
        case 'b':
        case 'B': {
          size_t start = move_word_backward_n(cursor, total, key == 'B');
          erase_range(start, cursor);
          break;
        }
        case '$': {
          erase_range(cursor, current_line_end());
          break;
        }
        default:
          break;
      }
      clear_vim_command_state();
      return;
    }

    size_t count = effective_count(vim_prefix_count);
    switch (key) {
      case 'u':
        apply_undo();
        break;
      case 'h':
        for (size_t i = 0; i < count && cursor > 0; ++i) {
          cursor = prev_codepoint_start(buffer, cursor);
        }
        redraw_line(buffer, cursor);
        break;
      case 'l':
        for (size_t i = 0; i < count && cursor < buffer.size(); ++i) {
          cursor = next_codepoint_start(buffer, cursor);
        }
        redraw_line(buffer, cursor);
        break;
      case 'k':
        for (size_t i = 0; i < count; ++i) {
          if (buffer.find('\n') != std::string::npos) {
            move_up(false);
          } else {
            move_up(true);
          }
        }
        break;
      case 'j':
        for (size_t i = 0; i < count; ++i) {
          if (buffer.find('\n') != std::string::npos) {
            move_down(false);
          } else {
            move_down(true);
          }
        }
        break;
      case 'w':
      case 'W':
        cursor = move_word_forward_n(cursor, count, key == 'W');
        redraw_line(buffer, cursor);
        break;
      case 'b':
      case 'B':
        cursor = move_word_backward_n(cursor, count, key == 'B');
        redraw_line(buffer, cursor);
        break;
      case 16:  // Ctrl-P
        move_up(true);
        break;
      case 14:  // Ctrl-N
        move_down(true);
        break;
      case 'd':
        vim_delete_pending = true;
        vim_delete_count = count;
        vim_motion_count = 0;
        vim_prefix_count = 0;
        return;
      case '0':
        cursor = current_line_start();
        redraw_line(buffer, cursor);
        break;
      case '$':
        cursor = current_line_end();
        redraw_line(buffer, cursor);
        break;
      case 'i':
        enter_insert_mode();
        break;
      case 'a':
        if (cursor < buffer.size()) {
          cursor = next_codepoint_start(buffer, cursor);
        }
        enter_insert_mode();
        break;
      case 'I':
        cursor = current_line_start();
        enter_insert_mode();
        break;
      case 'A':
        cursor = current_line_end();
        enter_insert_mode();
        break;
      case 'o': {
        if (buffer.empty()) {
          enter_insert_mode();
          break;
        }
        push_undo_current();
        size_t line_end = current_line_end();
        size_t insert_pos = (line_end < buffer.size()) ? line_end + 1 : line_end;
        buffer.insert(buffer.begin() + static_cast<long>(insert_pos), '\n');
        cursor = insert_pos + 1;
        enter_insert_mode();
        break;
      }
      case 'O': {
        if (buffer.empty()) {
          enter_insert_mode();
          break;
        }
        push_undo_current();
        size_t line_start = current_line_start();
        buffer.insert(buffer.begin() + static_cast<long>(line_start), '\n');
        cursor = line_start;
        enter_insert_mode();
        break;
      }
      default:
        break;
    }
    vim_prefix_count = 0;
  };

  apply_mode_prompt();
  redraw_line(buffer, cursor);

  while (true) {
    char c = 0;
    ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if (n <= 0) return false;

    auto flush_utf8_pending = [&]() {
      if (!utf8_pending.empty()) {
        buffer.insert(buffer.begin() + static_cast<long>(cursor),
                      utf8_pending.begin(),
                      utf8_pending.end());
        cursor += utf8_pending.size();
        utf8_pending.clear();
        utf8_expected = 0;
      }
    };

    if (!paste_mode && (c == '\n' || c == '\r')) {
      flush_utf8_pending();
      bool is_command = !buffer.empty() && (buffer[0] == '.' || buffer[0] == ':');
      if (!buffer.empty() && buffer.find(';') == std::string::npos && !is_command) {
        size_t line_start = buffer.rfind('\n', cursor > 0 ? cursor - 1 : 0);
        line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
        size_t indent_end = line_start;
        while (indent_end < buffer.size() &&
               (buffer[indent_end] == ' ' || buffer[indent_end] == '\t')) {
          ++indent_end;
        }
        std::string indent = buffer.substr(line_start, indent_end - line_start);
        push_undo_current();
        buffer.insert(buffer.begin() + static_cast<long>(cursor), '\n');
        ++cursor;
        if (!indent.empty()) {
          buffer.insert(buffer.begin() + static_cast<long>(cursor),
                        indent.begin(),
                        indent.end());
          cursor += indent.size();
        }
        redraw_line(buffer, cursor);
        continue;
      }
      std::cout << std::endl;
      out = buffer;
      return true;
    }

    if (editor_mode_ == EditorMode::Vim && !vim_insert_mode_ && c != 27) {
      flush_utf8_pending();
      handle_vim_normal_key(c);
      continue;
    }

    if (c == 127 || c == 8) {
      flush_utf8_pending();
      if (cursor > 0) {
        push_undo_current();
        size_t prev = prev_codepoint_start(buffer, cursor);
        buffer.erase(prev, cursor - prev);
        cursor = prev;
        redraw_line(buffer, cursor);
      }
      continue;
    }

    if (c == 12) {
      flush_utf8_pending();
      std::cout << "\033[2J\033[H" << std::flush;
      redraw_line(buffer, cursor);
      continue;
    }

    if (c == 9) {
      flush_utf8_pending();
      std::vector<std::string> suggestions;
      bool changed = completer_->complete(buffer, cursor, suggestions);
      if (suggestions.empty() && !changed) {
        size_t line_start = buffer.rfind('\n', cursor > 0 ? cursor - 1 : 0);
        line_start = (line_start == std::string::npos) ? 0 : line_start + 1;
        bool only_ws = true;
        for (size_t i = line_start; i < cursor; ++i) {
          if (buffer[i] != ' ' && buffer[i] != '\t') {
            only_ws = false;
            break;
          }
        }
        if (only_ws) {
          push_undo_current();
          buffer.insert(buffer.begin() + static_cast<long>(cursor), ' ');
          buffer.insert(buffer.begin() + static_cast<long>(cursor), ' ');
          cursor += 2;
        }
      } else if (!suggestions.empty() && !changed) {
        std::cout << "\n";
        for (size_t i = 0; i < suggestions.size(); ++i) {
          if (i > 0) std::cout << " ";
          std::cout << suggestions[i];
        }
        std::cout << "\n";
      }
      redraw_line(buffer, cursor);
      continue;
    }

    if (c == 27) {
      flush_utf8_pending();
      char seq[6] = {0, 0, 0, 0, 0, 0};
      if (!read_byte_with_timeout(&seq[0], 25) || !read_byte_with_timeout(&seq[1], 25)) {
        if (editor_mode_ == EditorMode::Normal) {
          enter_vim_normal_mode();
        } else if (vim_insert_mode_) {
          enter_vim_normal_mode();
        } else {
          enter_normal_mode();
        }
        continue;
      }
      if (seq[0] == '[') {
        if (seq[1] == '3') {
          if (!read_byte_with_timeout(&seq[2], 25)) continue;
          if (seq[2] == '~') {
            if (cursor < buffer.size()) {
              push_undo_current();
              size_t next = next_codepoint_start(buffer, cursor);
              buffer.erase(cursor, next - cursor);
              redraw_line(buffer, cursor);
            }
            continue;
          }
        }
        if (seq[1] == '2') {
          if (!read_byte_with_timeout(&seq[2], 25)) continue;
          if (!read_byte_with_timeout(&seq[3], 25)) continue;
          if (seq[2] == '0' && seq[3] == '0') {
            if (!read_byte_with_timeout(&seq[4], 25)) continue;
            if (seq[4] == '~') {
              paste_mode = true;
              paste_buffer.clear();
              continue;
            }
          }
          if (seq[2] == '0' && seq[3] == '1') {
            if (!read_byte_with_timeout(&seq[4], 25)) continue;
            if (seq[4] == '~') {
              paste_mode = false;
              if (!paste_buffer.empty()) {
                push_undo_current();
              }
              for (char pc : paste_buffer) {
                if (pc == '\r') continue;
                buffer.insert(buffer.begin() + static_cast<long>(cursor), pc);
                ++cursor;
              }
              redraw_line(buffer, cursor);
              continue;
            }
          }
        }
        if (seq[1] == 'A') {
          move_up();
          continue;
        }
        if (seq[1] == 'B') {
          move_down();
          continue;
        }
        if (seq[1] == 'C') {
          if (cursor < buffer.size()) {
            cursor = next_codepoint_start(buffer, cursor);
            redraw_line(buffer, cursor);
          }
          continue;
        }
        if (seq[1] == 'D') {
          if (cursor > 0) {
            cursor = prev_codepoint_start(buffer, cursor);
            redraw_line(buffer, cursor);
          }
          continue;
        }
      }
      continue;
    }

    if (paste_mode) {
      paste_buffer.push_back(c);
      continue;
    }

    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 0x20 && uc != 0x7F) {
      if ((uc & 0x80) == 0x80) {
        size_t expected = utf8_expected;
        if (utf8_pending.empty()) {
          expected = utf8_sequence_length(uc);
          if (expected == 1) {
            push_undo_current();
            buffer.insert(buffer.begin() + static_cast<long>(cursor), c);
            ++cursor;
            redraw_line(buffer, cursor);
            continue;
          }
          utf8_expected = expected;
        } else if (expected == 0) {
          expected = utf8_sequence_length(uc);
          utf8_expected = expected;
        }
        utf8_pending.push_back(c);
        if (utf8_expected > 0 && utf8_pending.size() >= utf8_expected) {
          push_undo_current();
          buffer.insert(buffer.begin() + static_cast<long>(cursor),
                        utf8_pending.begin(),
                        utf8_pending.end());
          cursor += utf8_pending.size();
          utf8_pending.clear();
          utf8_expected = 0;
          redraw_line(buffer, cursor);
        }
        continue;
      }
      flush_utf8_pending();
      push_undo_current();
      buffer.insert(buffer.begin() + static_cast<long>(cursor), c);
      ++cursor;
      redraw_line(buffer, cursor);
    }
  }
}

void LineEditor::add_history(const std::string& line) {
  history_.add(line);
}

void LineEditor::set_history_size(size_t max_entries) {
  history_.set_max_entries(max_entries);
}

bool LineEditor::set_history_path(const std::string& path, std::string& error) {
  return history_.set_path(path, error);
}

void LineEditor::redraw_line(const std::string& buffer, size_t cursor) {
  int width = terminal_width();
  if (width <= 0) width = 80;
  if (last_render_lines_ <= 0) {
    std::cout << "\r\033[2K";
  } else {
    if (last_cursor_line_ > 0) {
      std::cout << "\033[" << last_cursor_line_ << "A";
    }
    for (int i = 0; i < last_render_lines_; ++i) {
      std::cout << "\r\033[2K";
      if (i + 1 < last_render_lines_) {
        std::cout << "\033[1B";
      }
    }
    if (last_render_lines_ > 1) {
      std::cout << "\033[" << (last_render_lines_ - 1) << "A";
    }
    std::cout << "\r";
  }

  std::cout << prompt_;
  render_buffer(buffer, keyword_color_, cont_prompt_);

  int end_line = compute_render_lines(buffer, prompt_, prompt_len_,
                                      cont_prompt_, cont_prompt_len_, width) - 1;
  int cursor_line = compute_cursor_line(buffer, cursor, prompt_, prompt_len_,
                                        cont_prompt_, cont_prompt_len_, width);

  int cursor_col = 0;
  if (cursor_line > 0) {
    size_t line_start = 0;
    int line = 0;
    while (line < cursor_line && line_start < buffer.size()) {
      size_t next = buffer.find('\n', line_start);
      if (next == std::string::npos) {
        line_start = buffer.size();
        break;
      }
      line_start = next + 1;
      ++line;
    }
    cursor_col = static_cast<int>(cont_prompt_len_ + column_width(buffer, line_start, cursor));
  } else {
    cursor_col = static_cast<int>(prompt_len_ + column_width(buffer, 0, cursor));
  }

  last_render_lines_ = end_line + 1;
  last_cursor_line_ = cursor_line;

  int up = end_line - cursor_line;
  if (up > 0) {
    std::cout << "\033[" << up << "A";
  }
  std::cout << "\r";
  if (cursor_col > 0) {
    std::cout << "\033[" << cursor_col << "C";
  }
  std::cout << std::flush;
}

}  // namespace xsql::cli
