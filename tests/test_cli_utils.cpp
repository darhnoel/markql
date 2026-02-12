#include "test_harness.h"

#include "cli_utils.h"
#include "repl/input/text_util.h"

namespace {

void test_count_table_rows_header() {
  xsql::QueryResult::TableResult table;
  table.rows = {
      {"H1", "H2"},
      {"a", "b"},
      {"c", "d"},
  };
  size_t count = xsql::cli::count_table_rows(table, true);
  expect_eq(count, 2, "count_table_rows excludes header");
}

void test_count_table_rows_no_header() {
  xsql::QueryResult::TableResult table;
  table.rows = {
      {"a", "b"},
      {"c", "d"},
  };
  size_t count = xsql::cli::count_table_rows(table, false);
  expect_eq(count, 2, "count_table_rows includes all rows");
}

void test_count_result_rows() {
  xsql::QueryResult result;
  result.rows.resize(3);
  size_t count = xsql::cli::count_result_rows(result);
  expect_eq(count, 3, "count_result_rows returns row count");
}

void test_proportional_column_end_maps_to_end() {
  size_t mapped = xsql::cli::proportional_column(17, 17, 23);
  expect_eq(mapped, 23, "proportional_column keeps end alignment");
}

void test_proportional_column_scales_middle() {
  size_t mapped = xsql::cli::proportional_column(5, 10, 20);
  expect_eq(mapped, 10, "proportional_column scales middle cursor");
}

void test_proportional_column_zero_source_len() {
  size_t mapped = xsql::cli::proportional_column(3, 0, 2);
  expect_eq(mapped, 2, "proportional_column clamps when source length is zero");
}

void test_column_width_cjk_wide() {
  std::string text = "\xE6\x97\xA5\xE6\x9C\xAC""a";
  size_t width = xsql::cli::column_width(text, 0, text.size());
  expect_eq(width, 5, "column_width counts CJK as width 2");
}

void test_column_to_index_cjk_boundary() {
  std::string text = "\xE6\x97\xA5\xE6\x9C\xAC""a";
  size_t idx = xsql::cli::column_to_index(text, 0, text.size(), 4);
  expect_eq(idx, static_cast<size_t>(6), "column_to_index maps display column to UTF-8 byte index");
}

}  // namespace

void register_cli_utils_tests(std::vector<TestCase>& tests) {
  tests.push_back({"count_table_rows_header", test_count_table_rows_header});
  tests.push_back({"count_table_rows_no_header", test_count_table_rows_no_header});
  tests.push_back({"count_result_rows", test_count_result_rows});
  tests.push_back({"proportional_column_end_maps_to_end", test_proportional_column_end_maps_to_end});
  tests.push_back({"proportional_column_scales_middle", test_proportional_column_scales_middle});
  tests.push_back({"proportional_column_zero_source_len", test_proportional_column_zero_source_len});
  tests.push_back({"column_width_cjk_wide", test_column_width_cjk_wide});
  tests.push_back({"column_to_index_cjk_boundary", test_column_to_index_cjk_boundary});
}
