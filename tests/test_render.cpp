#include <string>
#include <vector>

#include "test_harness.h"

#include "repl/ui/render.h"

namespace {

void test_render_lines_include_prompt_extra_line() {
  const std::string prompt = "┌─(markql)[vim:edit]\n└─▪ ";
  const std::string cont_prompt = "... ";
  const int lines = xsql::cli::compute_render_lines(
      "", prompt, 4, 1, cont_prompt, 4, 80);
  expect_eq(static_cast<size_t>(lines), static_cast<size_t>(2),
            "multi-line prompt should count both prompt rows");
}

void test_cursor_line_starts_after_prompt_extra_line() {
  const std::string prompt = "┌─(markql)[vim:edit]\n└─▪ ";
  const std::string cont_prompt = "... ";
  const int line = xsql::cli::compute_cursor_line(
      "", 0, prompt, 4, 1, cont_prompt, 4, 80);
  expect_eq(static_cast<size_t>(line), static_cast<size_t>(1),
            "cursor on empty buffer should start on second prompt row");
}

void test_render_lines_with_buffer_newline_and_cont_prompt() {
  const std::string prompt = "┌─(markql)[vim:edit]\n└─▪ ";
  const std::string cont_prompt = "... ";
  const int lines = xsql::cli::compute_render_lines(
      "abc\ndef", prompt, 4, 1, cont_prompt, 4, 80);
  expect_eq(static_cast<size_t>(lines), static_cast<size_t>(3),
            "buffer newline should add one rendered row after prompt rows");
}

}  // namespace

void register_render_tests(std::vector<TestCase>& tests) {
  tests.push_back({"render_lines_include_prompt_extra_line",
                   test_render_lines_include_prompt_extra_line});
  tests.push_back({"cursor_line_starts_after_prompt_extra_line",
                   test_cursor_line_starts_after_prompt_extra_line});
  tests.push_back({"render_lines_with_buffer_newline_and_cont_prompt",
                   test_render_lines_with_buffer_newline_and_cont_prompt});
}
