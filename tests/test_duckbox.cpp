#include "test_harness.h"
#include "test_utils.h"

#include "render/duckbox_renderer.h"

namespace {

void test_duckbox_basic_table() {
  auto result = make_result({"node_id", "tag"}, {{"1", "div"}});
  xsql::render::DuckboxOptions options;
  options.max_width = 80;
  options.max_rows = 40;
  options.highlight = false;
  options.is_tty = false;
  std::string out = xsql::render::render_duckbox(result, options);
  std::string expected =
      "┌─────────┬──────┐\n"
      "│ node_id │ tag  │\n"
      "├─────────┼──────┤\n"
      "│       1 │ div  │\n"
      "└─────────┴──────┘";
  expect_true(out == expected, "duckbox basic table");
}

void test_duckbox_truncate_cells() {
  auto result = make_result({"node_id", "text"}, {{"1", "a very long piece of text"}});
  xsql::render::DuckboxOptions options;
  options.max_width = 30;
  options.max_rows = 40;
  options.highlight = false;
  options.is_tty = false;
  std::string out = xsql::render::render_duckbox(result, options);
  expect_true(out.find("a very") != std::string::npos, "duckbox truncate cells contains prefix");
  expect_true(out.find("…") != std::string::npos, "duckbox truncate cells ellipsis");
  expect_true(out.find("a very long piece of text") == std::string::npos,
              "duckbox truncate cells no full text");
}

void test_duckbox_maxrows_truncate() {
  auto result = make_result({"node_id"}, {{"1"}, {"2"}, {"3"}});
  xsql::render::DuckboxOptions options;
  options.max_width = 80;
  options.max_rows = 2;
  options.highlight = false;
  options.is_tty = false;
  std::string out = xsql::render::render_duckbox(result, options);
  expect_true(out.find("trun") != std::string::npos, "duckbox maxrows truncate");
}

void test_duckbox_numeric_alignment() {
  auto result = make_result({"name", "value"}, {{"alpha", "12"}, {"beta", "3.5"}});
  xsql::render::DuckboxOptions options;
  options.max_width = 80;
  options.max_rows = 40;
  options.highlight = false;
  options.is_tty = false;
  std::string out = xsql::render::render_duckbox(result, options);
  std::string expected =
      "┌───────┬───────┐\n"
      "│ name  │ value │\n"
      "├───────┼───────┤\n"
      "│ alpha │    12 │\n"
      "│ beta  │   3.5 │\n"
      "└───────┴───────┘";
  expect_true(out == expected, "duckbox numeric alignment");
}

void test_duckbox_null_rendering() {
  auto result = make_result({"parent_id", "tag"}, {{"NULL", "div"}});
  xsql::render::DuckboxOptions options;
  options.max_width = 80;
  options.max_rows = 40;
  options.highlight = false;
  options.is_tty = false;
  std::string out = xsql::render::render_duckbox(result, options);
  std::string expected =
      "┌───────────┬──────┐\n"
      "│ parent_id │ tag  │\n"
      "├───────────┼──────┤\n"
      "│ NULL      │ div  │\n"
      "└───────────┴──────┘";
  expect_true(out == expected, "duckbox null rendering");
}

}  // namespace

void register_duckbox_tests(std::vector<TestCase>& tests) {
  tests.push_back({"duckbox_basic_table", test_duckbox_basic_table});
  tests.push_back({"duckbox_truncate_cells", test_duckbox_truncate_cells});
  tests.push_back({"duckbox_maxrows_truncate", test_duckbox_maxrows_truncate});
  tests.push_back({"duckbox_numeric_alignment", test_duckbox_numeric_alignment});
  tests.push_back({"duckbox_null_rendering", test_duckbox_null_rendering});
}
