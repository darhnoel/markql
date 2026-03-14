#include "test_harness.h"
#include "test_utils.h"

#include <filesystem>

#include "cli_utils.h"
#include "export/export_sinks.h"
#include "render/duckbox_renderer.h"
#include "markql/column_names.h"

namespace {

void test_normalize_colname_examples() {
  expect_true(markql::normalize_colname("data-id") == "data_id", "normalize data-id");
  expect_true(markql::normalize_colname("data-testid") == "data_testid", "normalize data-testid");
  expect_true(markql::normalize_colname("aria-label") == "aria_label", "normalize aria-label");
  expect_true(markql::normalize_colname("  ---  ") == "col", "normalize blank symbols");
  expect_true(markql::normalize_colname("123abc") == "c_123abc", "normalize leading digit");
  expect_true(markql::normalize_colname("group") == "group_", "normalize reserved keyword");
  expect_true(markql::normalize_colname("a--b") == "a_b", "normalize repeated separators");
  expect_true(markql::normalize_colname("A:B.C") == "a_b_c", "normalize mixed separators");
}

void test_normalize_colname_collision_suffixing() {
  std::vector<std::string> cols = {"data-id", "data_id"};
  auto schema = markql::build_column_name_map(cols, markql::ColumnNameMode::Normalize);
  expect_eq(schema.size(), 2, "collision schema size");
  if (schema.size() == 2) {
    expect_true(schema[0].output_name == "data_id", "collision first name");
    expect_true(schema[1].output_name == "data_id__2", "collision second name");
  }
}

void test_duckbox_uses_normalized_headers_by_default() {
  markql::QueryResult result;
  result.columns = {"data-id"};
  markql::QueryResultRow row;
  row.attributes["data-id"] = "x";
  result.rows.push_back(std::move(row));
  markql::render::DuckboxOptions options;
  options.max_width = 80;
  options.max_rows = 40;
  options.highlight = false;
  options.is_tty = false;
  std::string out = markql::render::render_duckbox(result, options);
  expect_true(out.find("data_id") != std::string::npos, "duckbox normalized header");
}

void test_duckbox_raw_mode_keeps_raw_header() {
  markql::QueryResult result;
  result.columns = {"data-id"};
  markql::QueryResultRow row;
  row.attributes["data-id"] = "x";
  result.rows.push_back(std::move(row));
  markql::render::DuckboxOptions options;
  options.max_width = 80;
  options.max_rows = 40;
  options.highlight = false;
  options.is_tty = false;
  options.colname_mode = markql::ColumnNameMode::Raw;
  std::string out = markql::render::render_duckbox(result, options);
  expect_true(out.find("data-id") != std::string::npos, "duckbox raw header");
}

void test_csv_header_normalized_default() {
  markql::QueryResult result;
  result.columns = {"data-id"};
  markql::QueryResultRow row;
  row.attributes["data-id"] = "x";
  result.rows.push_back(std::move(row));
  auto path = std::filesystem::temp_directory_path() / "markql_colname_norm.csv";
  std::string error;
  bool ok = markql::cli::write_csv(result, path.string(), error);
  expect_true(ok, "write_csv normalized ok");
  expect_true(error.empty(), "write_csv normalized no error");
  std::string content = read_file_to_string(path);
  std::filesystem::remove(path);
  expect_true(content.rfind("data_id\n", 0) == 0, "write_csv normalized header");
}

void test_csv_header_raw_mode() {
  markql::QueryResult result;
  result.columns = {"data-id"};
  markql::QueryResultRow row;
  row.attributes["data-id"] = "x";
  result.rows.push_back(std::move(row));
  auto path = std::filesystem::temp_directory_path() / "markql_colname_raw.csv";
  std::string error;
  bool ok = markql::cli::write_csv(result, path.string(), error, markql::ColumnNameMode::Raw);
  expect_true(ok, "write_csv raw ok");
  expect_true(error.empty(), "write_csv raw no error");
  std::string content = read_file_to_string(path);
  std::filesystem::remove(path);
  expect_true(content.rfind("data-id\n", 0) == 0, "write_csv raw header");
}

void test_json_keys_follow_colname_mode() {
  markql::QueryResult result;
  result.columns = {"data-id"};
  markql::QueryResultRow row;
  row.attributes["data-id"] = "x";
  result.rows.push_back(std::move(row));
  std::string normalized = markql::cli::build_json(result, markql::ColumnNameMode::Normalize);
  std::string raw = markql::cli::build_json(result, markql::ColumnNameMode::Raw);
  expect_true(normalized.find("\"data_id\"") != std::string::npos, "json normalized key");
  expect_true(raw.find("\"data-id\"") != std::string::npos, "json raw key");
}

}  // namespace

void register_column_name_tests(std::vector<TestCase>& tests) {
  tests.push_back({"normalize_colname_examples", test_normalize_colname_examples});
  tests.push_back({"normalize_colname_collision_suffixing", test_normalize_colname_collision_suffixing});
  tests.push_back({"duckbox_uses_normalized_headers_by_default", test_duckbox_uses_normalized_headers_by_default});
  tests.push_back({"duckbox_raw_mode_keeps_raw_header", test_duckbox_raw_mode_keeps_raw_header});
  tests.push_back({"csv_header_normalized_default", test_csv_header_normalized_default});
  tests.push_back({"csv_header_raw_mode", test_csv_header_raw_mode});
  tests.push_back({"json_keys_follow_colname_mode", test_json_keys_follow_colname_mode});
}
