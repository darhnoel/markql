#include <string>
#include <vector>

#include "test_harness.h"

#include "repl/core/vim_edit.h"

namespace {

void test_vim_x_deletes_single_ascii() {
  std::string buffer = "abcd";
  size_t cursor = 1;  // on 'b'
  bool changed = xsql::cli::delete_vim_chars_under_cursor(buffer, cursor, 1);
  expect_true(changed, "x should delete one char");
  expect_true(buffer == "acd", "x removes char under cursor");
  expect_eq(cursor, static_cast<size_t>(1), "cursor stays at same byte index");
}

void test_vim_count_x_deletes_multiple_ascii() {
  std::string buffer = "abcdef";
  size_t cursor = 2;  // on 'c'
  bool changed = xsql::cli::delete_vim_chars_under_cursor(buffer, cursor, 3);
  expect_true(changed, "<n>x should delete multiple chars");
  expect_true(buffer == "abf", "3x removes cde");
  expect_eq(cursor, static_cast<size_t>(2), "cursor remains at original position");
}

void test_vim_count_x_clamps_at_eof() {
  std::string buffer = "abc";
  size_t cursor = 1;  // on 'b'
  bool changed = xsql::cli::delete_vim_chars_under_cursor(buffer, cursor, 99);
  expect_true(changed, "large count still deletes until eof");
  expect_true(buffer == "a", "count clamps at end");
  expect_eq(cursor, static_cast<size_t>(1), "cursor is clamped after deletion");
}

void test_vim_x_handles_utf8_codepoint_boundaries() {
  std::string buffer = "a国b";
  size_t cursor = 1;  // on '国' (3-byte UTF-8)
  bool changed = xsql::cli::delete_vim_chars_under_cursor(buffer, cursor, 1);
  expect_true(changed, "x should delete one UTF-8 codepoint");
  expect_true(buffer == "ab", "UTF-8 codepoint deleted without corruption");
  expect_eq(cursor, static_cast<size_t>(1), "cursor remains byte-stable");
}

void test_vim_x_at_eof_is_noop() {
  std::string buffer = "abc";
  size_t cursor = buffer.size();
  bool changed = xsql::cli::delete_vim_chars_under_cursor(buffer, cursor, 1);
  expect_true(!changed, "x at eof is noop");
  expect_true(buffer == "abc", "buffer unchanged at eof");
}

void test_vim_word_forward_small_vs_big() {
  std::string buffer = "foo,bar baz";
  size_t small = xsql::cli::move_vim_word_forward_n(buffer, 0, 1, false);
  size_t big = xsql::cli::move_vim_word_forward_n(buffer, 0, 1, true);
  expect_eq(small, static_cast<size_t>(3), "w stops at punctuation boundary");
  expect_eq(big, static_cast<size_t>(8), "W moves across punctuation and trailing space");
}

void test_vim_word_backward_small_vs_big() {
  std::string buffer = "foo,bar baz";
  size_t small = xsql::cli::move_vim_word_backward_n(buffer, 8, 1, false);
  size_t big = xsql::cli::move_vim_word_backward_n(buffer, 8, 1, true);
  expect_eq(small, static_cast<size_t>(4), "b stops at punctuation boundary");
  expect_eq(big, static_cast<size_t>(0), "B moves to beginning of previous non-space chunk");
}

void test_vim_delete_motion_dw() {
  std::string buffer = "alpha beta";
  size_t cursor = 0;
  bool changed = xsql::cli::delete_vim_motion(buffer, cursor, 1, 'w', buffer.size());
  expect_true(changed, "dw should delete forward word span");
  expect_true(buffer == "beta", "dw deletes first word and following separator");
  expect_eq(cursor, static_cast<size_t>(0), "cursor stays at delete start");
}

void test_vim_delete_motion_db() {
  std::string buffer = "alpha beta";
  size_t cursor = 6;  // start of beta
  bool changed = xsql::cli::delete_vim_motion(buffer, cursor, 1, 'b', buffer.size());
  expect_true(changed, "db should delete backward word span");
  expect_true(buffer == "beta", "db deletes previous word and separator");
  expect_eq(cursor, static_cast<size_t>(0), "cursor moves to deleted span start");
}

void test_vim_delete_motion_dollar() {
  std::string buffer = "abc def";
  size_t cursor = 4;  // on d
  bool changed = xsql::cli::delete_vim_motion(buffer, cursor, 1, '$', buffer.size());
  expect_true(changed, "d$ should delete to line end");
  expect_true(buffer == "abc ", "d$ keeps prefix and deletes tail");
  expect_eq(cursor, static_cast<size_t>(4), "cursor remains at original position");
}

void test_vim_delete_motion_unknown_is_noop() {
  std::string buffer = "abc";
  size_t cursor = 1;
  bool changed = xsql::cli::delete_vim_motion(buffer, cursor, 1, 'q', buffer.size());
  expect_true(!changed, "unknown delete motion is noop");
  expect_true(buffer == "abc", "buffer unchanged for unknown motion");
  expect_eq(cursor, static_cast<size_t>(1), "cursor unchanged for unknown motion");
}

}  // namespace

void register_vim_edit_tests(std::vector<TestCase>& tests) {
  tests.push_back({"vim_x_deletes_single_ascii", test_vim_x_deletes_single_ascii});
  tests.push_back({"vim_count_x_deletes_multiple_ascii", test_vim_count_x_deletes_multiple_ascii});
  tests.push_back({"vim_count_x_clamps_at_eof", test_vim_count_x_clamps_at_eof});
  tests.push_back({"vim_x_handles_utf8_codepoint_boundaries",
                   test_vim_x_handles_utf8_codepoint_boundaries});
  tests.push_back({"vim_x_at_eof_is_noop", test_vim_x_at_eof_is_noop});
  tests.push_back({"vim_word_forward_small_vs_big", test_vim_word_forward_small_vs_big});
  tests.push_back({"vim_word_backward_small_vs_big", test_vim_word_backward_small_vs_big});
  tests.push_back({"vim_delete_motion_dw", test_vim_delete_motion_dw});
  tests.push_back({"vim_delete_motion_db", test_vim_delete_motion_db});
  tests.push_back({"vim_delete_motion_dollar", test_vim_delete_motion_dollar});
  tests.push_back({"vim_delete_motion_unknown_is_noop", test_vim_delete_motion_unknown_is_noop});
}
