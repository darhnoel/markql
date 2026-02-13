#include <string>
#include <vector>

#include "test_harness.h"

#include "repl/core/vim_normal.h"

namespace {

struct Harness {
  std::string buffer;
  size_t cursor = 0;
  size_t redraw_calls = 0;
  size_t undo_calls = 0;
  size_t move_up_calls = 0;
  size_t move_down_calls = 0;
  bool entered_insert = false;
  std::vector<std::pair<std::string, size_t>> undo_snapshots;
};

size_t line_start(const Harness& h) {
  size_t start = h.buffer.rfind('\n', h.cursor > 0 ? h.cursor - 1 : 0);
  return (start == std::string::npos) ? 0 : start + 1;
}

size_t line_end(const Harness& h) {
  size_t start = line_start(h);
  size_t end = h.buffer.find('\n', start);
  return (end == std::string::npos) ? h.buffer.size() : end;
}

xsql::cli::VimNormalContext make_ctx(Harness& h) {
  return xsql::cli::VimNormalContext{
      h.buffer,
      h.cursor,
      [&]() { return line_start(h); },
      [&]() { return line_end(h); },
      [&]() { ++h.redraw_calls; },
      [&](const std::string& prev_buffer, size_t prev_cursor) {
        h.undo_snapshots.push_back({prev_buffer, prev_cursor});
      },
      [&]() { ++h.undo_calls; },
      [&](bool) { ++h.move_up_calls; },
      [&](bool) { ++h.move_down_calls; },
      [&]() { h.entered_insert = true; },
  };
}

void test_vim_normal_sequence_count_then_x() {
  Harness h;
  h.buffer = "abcdef";
  h.cursor = 1;
  auto ctx = make_ctx(h);
  xsql::cli::VimNormalState state;
  xsql::cli::handle_vim_normal_key('3', state, ctx);
  xsql::cli::handle_vim_normal_key('x', state, ctx);
  expect_true(h.buffer == "aef", "3x deletes three chars");
  expect_eq(h.cursor, static_cast<size_t>(1), "cursor stable after 3x");
  expect_eq(h.undo_snapshots.size(), static_cast<size_t>(1), "3x records one undo snapshot");
}

void test_vim_normal_sequence_d2w() {
  Harness h;
  h.buffer = "alpha beta gamma";
  h.cursor = 0;
  auto ctx = make_ctx(h);
  xsql::cli::VimNormalState state;
  xsql::cli::handle_vim_normal_key('d', state, ctx);
  xsql::cli::handle_vim_normal_key('2', state, ctx);
  xsql::cli::handle_vim_normal_key('w', state, ctx);
  expect_true(h.buffer == "gamma", "d2w deletes first two words");
  expect_eq(h.cursor, static_cast<size_t>(0), "cursor moved to delete start");
  expect_eq(h.undo_snapshots.size(), static_cast<size_t>(1), "d2w records undo snapshot");
}

void test_vim_normal_zero_moves_to_line_start_without_prefix() {
  Harness h;
  h.buffer = "abc\ndef";
  h.cursor = 2;
  auto ctx = make_ctx(h);
  xsql::cli::VimNormalState state;
  xsql::cli::handle_vim_normal_key('0', state, ctx);
  expect_eq(h.cursor, static_cast<size_t>(0), "0 moves to current line start");
}

void test_vim_normal_jk_count_delegates_to_move_callbacks() {
  Harness h;
  h.buffer = "one";
  h.cursor = 0;
  auto ctx = make_ctx(h);
  xsql::cli::VimNormalState state;
  xsql::cli::handle_vim_normal_key('2', state, ctx);
  xsql::cli::handle_vim_normal_key('j', state, ctx);
  xsql::cli::handle_vim_normal_key('3', state, ctx);
  xsql::cli::handle_vim_normal_key('k', state, ctx);
  expect_eq(h.move_down_calls, static_cast<size_t>(2), "<n>j dispatches repeated move_down");
  expect_eq(h.move_up_calls, static_cast<size_t>(3), "<n>k dispatches repeated move_up");
}

void test_vim_normal_insert_commands_trigger_enter_insert() {
  Harness h;
  h.buffer = "abc";
  h.cursor = 1;
  auto ctx = make_ctx(h);
  xsql::cli::VimNormalState state;
  xsql::cli::handle_vim_normal_key('i', state, ctx);
  expect_true(h.entered_insert, "i enters insert mode");
}

}  // namespace

void register_vim_normal_tests(std::vector<TestCase>& tests) {
  tests.push_back({"vim_normal_sequence_count_then_x", test_vim_normal_sequence_count_then_x});
  tests.push_back({"vim_normal_sequence_d2w", test_vim_normal_sequence_d2w});
  tests.push_back({"vim_normal_zero_moves_to_line_start_without_prefix",
                   test_vim_normal_zero_moves_to_line_start_without_prefix});
  tests.push_back({"vim_normal_jk_count_delegates_to_move_callbacks",
                   test_vim_normal_jk_count_delegates_to_move_callbacks});
  tests.push_back({"vim_normal_insert_commands_trigger_enter_insert",
                   test_vim_normal_insert_commands_trigger_enter_insert});
}
