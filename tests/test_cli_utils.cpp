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

void test_inspect_sql_input_line_comment_only() {
  auto inspection = xsql::cli::inspect_sql_input("-- just comment");
  expect_true(!inspection.has_error, "line-comment-only input has no lexer error");
  expect_true(inspection.empty_after_comments, "line-comment-only input becomes empty");
}

void test_inspect_sql_input_block_comment_only() {
  auto inspection = xsql::cli::inspect_sql_input("/* just comment */");
  expect_true(!inspection.has_error, "block-comment-only input has no lexer error");
  expect_true(inspection.empty_after_comments, "block-comment-only input becomes empty");
}

void test_inspect_sql_input_unterminated_block_comment() {
  auto inspection = xsql::cli::inspect_sql_input("/* missing");
  expect_true(inspection.has_error, "unterminated block comment is reported");
  expect_true(inspection.error_message == "Unterminated block comment",
              "unterminated block comment message is deterministic");
}

void test_parse_query_source_doc_alias_targets_doc_input() {
  auto parsed = xsql::cli::parse_query_source("SELECT n.node_id FROM doc AS n");
  expect_true(parsed.has_value(), "doc alias query parses");
  expect_true(parsed->alias.has_value(), "doc alias keeps a dispatch alias");
  expect_true(*parsed->alias == "doc", "FROM doc AS n resolves dispatch alias to doc");
}

void test_parse_query_source_cte_alias_not_dispatch_alias() {
  auto parsed = xsql::cli::parse_query_source(
      "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n) "
      "SELECT r.row_id FROM rows AS r");
  expect_true(parsed.has_value(), "cte query parses");
  expect_true(!parsed->alias.has_value(), "CTE row alias is not treated as input alias");
}

void test_parse_query_source_named_input_alias_dispatches() {
  auto parsed = xsql::cli::parse_query_source("SELECT x.node_id FROM x WHERE x.tag = 'li'");
  expect_true(parsed.has_value(), "named input query parses");
  expect_true(parsed->alias.has_value(), "named input query keeps dispatch alias");
  expect_true(*parsed->alias == "x", "named input alias is used for dispatch");
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
  tests.push_back({"inspect_sql_input_line_comment_only", test_inspect_sql_input_line_comment_only});
  tests.push_back({"inspect_sql_input_block_comment_only", test_inspect_sql_input_block_comment_only});
  tests.push_back({"inspect_sql_input_unterminated_block_comment",
                   test_inspect_sql_input_unterminated_block_comment});
  tests.push_back({"parse_query_source_doc_alias_targets_doc_input",
                   test_parse_query_source_doc_alias_targets_doc_input});
  tests.push_back({"parse_query_source_cte_alias_not_dispatch_alias",
                   test_parse_query_source_cte_alias_not_dispatch_alias});
  tests.push_back({"parse_query_source_named_input_alias_dispatches",
                   test_parse_query_source_named_input_alias_dispatches});
}
