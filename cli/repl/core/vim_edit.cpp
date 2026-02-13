#include "vim_edit.h"

#include <algorithm>
#include <cctype>

#include "repl/input/text_util.h"

namespace xsql::cli {

namespace {

enum class WordClass { Space, Keyword, Other };

bool is_space_cp(uint32_t cp) {
  if (cp <= 0x7F) {
    return std::isspace(static_cast<unsigned char>(cp)) != 0;
  }
  return cp == 0x00A0 || cp == 0x3000;
}

WordClass classify_cp(uint32_t cp, bool big_word) {
  if (is_space_cp(cp)) return WordClass::Space;
  if (big_word) return WordClass::Keyword;
  if (cp <= 0x7F) {
    unsigned char c = static_cast<unsigned char>(cp);
    if (std::isalnum(c) || c == '_') return WordClass::Keyword;
    return WordClass::Other;
  }
  return WordClass::Keyword;
}

size_t move_word_forward_once(const std::string& buffer, size_t pos, bool big_word) {
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
}

size_t move_word_backward_once(const std::string& buffer, size_t pos, bool big_word) {
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
}

}  // namespace

size_t move_vim_word_forward_n(const std::string& buffer,
                               size_t pos,
                               size_t count,
                               bool big_word) {
  size_t steps = count == 0 ? 1 : count;
  size_t out = pos;
  for (size_t i = 0; i < steps; ++i) {
    size_t next = move_word_forward_once(buffer, out, big_word);
    if (next == out) break;
    out = next;
  }
  return out;
}

size_t move_vim_word_backward_n(const std::string& buffer,
                                size_t pos,
                                size_t count,
                                bool big_word) {
  size_t steps = count == 0 ? 1 : count;
  size_t out = pos;
  for (size_t i = 0; i < steps; ++i) {
    size_t prev = move_word_backward_once(buffer, out, big_word);
    if (prev == out) break;
    out = prev;
  }
  return out;
}

bool delete_vim_chars_under_cursor(std::string& buffer, size_t& cursor, size_t count) {
  if (cursor >= buffer.size()) return false;
  size_t n = count == 0 ? 1 : count;
  size_t end = cursor;
  for (size_t i = 0; i < n && end < buffer.size(); ++i) {
    end = next_codepoint_start(buffer, end);
  }
  if (end <= cursor) return false;
  buffer.erase(cursor, end - cursor);
  cursor = std::min(cursor, buffer.size());
  return true;
}

bool delete_vim_motion(std::string& buffer,
                       size_t& cursor,
                       size_t count,
                       char motion,
                       size_t line_end) {
  size_t n = count == 0 ? 1 : count;
  size_t start = cursor;
  size_t end = cursor;

  switch (motion) {
    case 'w':
      end = move_vim_word_forward_n(buffer, cursor, n, false);
      break;
    case 'W':
      end = move_vim_word_forward_n(buffer, cursor, n, true);
      break;
    case 'b':
      start = move_vim_word_backward_n(buffer, cursor, n, false);
      break;
    case 'B':
      start = move_vim_word_backward_n(buffer, cursor, n, true);
      break;
    case '$':
      end = std::min(line_end, buffer.size());
      break;
    default:
      return false;
  }

  if (end <= start) return false;
  buffer.erase(start, end - start);
  cursor = std::min(start, buffer.size());
  return true;
}

}  // namespace xsql::cli
