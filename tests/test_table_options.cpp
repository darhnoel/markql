#include "test_harness.h"
#include "test_utils.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path resolve_path(const std::string& relative) {
  const std::vector<std::filesystem::path> candidates = {
      std::filesystem::path(relative),
      std::filesystem::path("..") / relative,
      std::filesystem::path("../..") / relative,
  };
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate)) return candidate;
  }
  return candidates.front();
}

std::string load_fixture(const std::string& filename) {
  const std::filesystem::path path =
      resolve_path("tests/fixtures/tables/" + filename);
  return read_file_to_string(path);
}

std::string json_escape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

bool is_ascii_digits(const std::string& value) {
  if (value.empty()) return false;
  return std::all_of(
      value.begin(), value.end(),
      [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::string stable_table_json(const xsql::QueryResult& result) {
  std::ostringstream out;
  if (result.tables.empty()) {
    out << "[]\n";
    return out.str();
  }
  const auto& table = result.tables[0];
  const bool sparse =
      result.table_options.format == xsql::QueryResult::TableOptions::Format::Sparse;
  const bool sparse_long =
      result.table_options.sparse_shape == xsql::QueryResult::TableOptions::SparseShape::Long;

  if (!sparse) {
    out << "[\n";
    for (size_t r = 0; r < table.rows.size(); ++r) {
      if (r > 0) out << ",\n";
      out << "  [";
      for (size_t c = 0; c < table.rows[r].size(); ++c) {
        if (c > 0) out << ", ";
        out << "\"" << json_escape(table.rows[r][c]) << "\"";
      }
      out << "]";
    }
    out << "\n]\n";
    return out.str();
  }

  if (sparse_long) {
    const bool include_header = result.table_has_header;
    const size_t expected = include_header ? 4 : 3;
    out << "[\n";
    bool first = true;
    for (const auto& row : table.rows) {
      if (row.size() < expected) continue;
      if (!first) out << ",\n";
      first = false;
      out << "  {\"row_index\":";
      if (is_ascii_digits(row[0])) out << row[0];
      else out << "\"" << json_escape(row[0]) << "\"";
      out << ",\"col_index\":";
      if (is_ascii_digits(row[1])) out << row[1];
      else out << "\"" << json_escape(row[1]) << "\"";
      if (include_header) {
        out << ",\"header\":\"" << json_escape(row[2]) << "\"";
        out << ",\"value\":\"" << json_escape(row[3]) << "\"";
      } else {
        out << ",\"value\":\"" << json_escape(row[2]) << "\"";
      }
      out << "}";
    }
    out << "\n]\n";
    return out.str();
  }

  out << "[\n";
  for (size_t r = 0; r < table.sparse_wide_rows.size(); ++r) {
    if (r > 0) out << ",\n";
    out << "  {";
    const auto& row = table.sparse_wide_rows[r];
    for (size_t c = 0; c < row.size(); ++c) {
      if (c > 0) out << ",";
      out << "\"" << json_escape(row[c].first) << "\":"
          << "\"" << json_escape(row[c].second) << "\"";
    }
    out << "}";
  }
  out << "\n]\n";
  return out.str();
}

void assert_golden(const std::string& actual, const std::string& golden_relative) {
  const std::filesystem::path golden = resolve_path(golden_relative);
  const char* regen = std::getenv("XSQL_REGEN_GOLDEN");
  const bool should_regen = (regen != nullptr && std::string(regen) == "1");
  if (should_regen) {
    std::filesystem::create_directories(golden.parent_path());
    std::ofstream file(golden, std::ios::binary);
    file << actual;
    expect_true(file.good(), "golden write succeeded: " + golden.string());
    return;
  }
  const std::string expected = read_file_to_string(golden);
  expect_true(
      actual == expected,
      "golden mismatch: " + golden.string());
}

void test_table_header_normalize_behavior() {
  const std::string html = load_fixture("header_noise.html");
  auto result = run_query(
      html,
      "SELECT table FROM doc TO TABLE(HEADER=ON, HEADER_NORMALIZE=ON)");
  expect_true(result.to_table, "header normalize to_table");
  expect_true(!result.tables.empty(), "header normalize table exists");
  if (result.tables.empty() || result.tables[0].rows.empty()) return;
  const auto& header = result.tables[0].rows[0];
  expect_true(header.size() == 3, "header normalize column count");
  if (header.size() == 3) {
    expect_true(header[0] == "Nominal", "header normalize dedupe adjacent token");
    expect_true(header[1] == "Price USD", "header normalize collapse whitespace");
    expect_true(header[2] == "col_3", "header normalize fallback for empty header");
  }
}

void test_table_empty_is_semantics() {
  const std::string html = load_fixture("whitespace_cells.html");
  auto blank_or_null = run_query(
      html,
      "SELECT table FROM doc TO TABLE(TRIM_EMPTY_ROWS=ON, EMPTY_IS=BLANK_OR_NULL)");
  auto null_only = run_query(
      html,
      "SELECT table FROM doc TO TABLE(TRIM_EMPTY_ROWS=ON, EMPTY_IS=NULL_ONLY)");
  auto blank_only = run_query(
      html,
      "SELECT table FROM doc TO TABLE(TRIM_EMPTY_ROWS=ON, EMPTY_IS=BLANK_ONLY)");
  expect_true(!blank_or_null.tables.empty(), "empty_is blank_or_null table exists");
  expect_true(!null_only.tables.empty(), "empty_is null_only table exists");
  expect_true(!blank_only.tables.empty(), "empty_is blank_only table exists");
  if (blank_or_null.tables.empty() || null_only.tables.empty() || blank_only.tables.empty()) return;
  expect_eq(blank_or_null.tables[0].rows.size(), 3, "blank_or_null trims blank/null row");
  expect_eq(null_only.tables[0].rows.size(), 4, "null_only keeps present blank row");
  expect_eq(blank_only.tables[0].rows.size(), 3, "blank_only trims blank row");
}

void test_table_trim_empty_rows_semantics() {
  const std::string html = load_fixture("trailing_empty_rows_and_cols.html");
  auto baseline = run_query(html, "SELECT table FROM doc TO TABLE()");
  auto trimmed = run_query(html, "SELECT table FROM doc TO TABLE(TRIM_EMPTY_ROWS=ON)");
  expect_true(!baseline.tables.empty(), "trim rows baseline table exists");
  expect_true(!trimmed.tables.empty(), "trim rows table exists");
  if (baseline.tables.empty() || trimmed.tables.empty()) return;
  expect_eq(baseline.tables[0].rows.size(), 27, "baseline row count includes padding rows");
  expect_eq(trimmed.tables[0].rows.size(), 13, "trim rows removes trailing empty rows");
}

void test_table_trim_empty_cols_semantics() {
  const std::string trailing_html = load_fixture("trailing_empty_rows_and_cols.html");
  const std::string middle_html = load_fixture("internal_empty_col.html");
  auto off = run_query(trailing_html, "SELECT table FROM doc TO TABLE(TRIM_EMPTY_COLS=OFF)");
  auto trailing =
      run_query(trailing_html, "SELECT table FROM doc TO TABLE(TRIM_EMPTY_COLS=TRAILING)");
  auto all = run_query(middle_html, "SELECT table FROM doc TO TABLE(TRIM_EMPTY_COLS=ALL)");
  expect_true(!off.tables.empty(), "trim cols off table exists");
  expect_true(!trailing.tables.empty(), "trim cols trailing table exists");
  expect_true(!all.tables.empty(), "trim cols all table exists");
  if (off.tables.empty() || trailing.tables.empty() || all.tables.empty()) return;
  expect_eq(off.tables[0].rows[0].size(), 14, "trim cols OFF keeps all columns");
  expect_eq(trailing.tables[0].rows[0].size(), 8, "trim cols TRAILING removes right edge empties");
  expect_eq(all.tables[0].rows[0].size(), 3, "trim cols ALL removes middle empty column");
}

void test_table_sparse_non_empty_cells_only() {
  const std::string html = load_fixture("trailing_empty_rows_and_cols.html");
  auto long_result = run_query(
      html,
      "SELECT table FROM doc TO TABLE("
      "FORMAT=SPARSE,SPARSE_SHAPE=LONG,TRIM_EMPTY_ROWS=ON,TRIM_EMPTY_COLS=TRAILING,HEADER=ON)");
  auto wide_result = run_query(
      html,
      "SELECT table FROM doc TO TABLE("
      "FORMAT=SPARSE,SPARSE_SHAPE=WIDE,TRIM_EMPTY_ROWS=ON,TRIM_EMPTY_COLS=TRAILING,HEADER=ON)");
  expect_true(!long_result.tables.empty(), "sparse long table exists");
  expect_true(!wide_result.tables.empty(), "sparse wide table exists");
  if (long_result.tables.empty() || wide_result.tables.empty()) return;

  const auto& long_rows = long_result.tables[0].rows;
  expect_eq(long_rows.size(), 96, "sparse long non-empty cell count");
  if (!long_rows.empty()) {
    expect_true(long_rows[0].size() == 4, "sparse long includes header column");
    expect_true(long_rows[0][0] == "1", "sparse long row_index starts at 1");
    expect_true(long_rows[0][1] == "1", "sparse long col_index starts at 1");
    expect_true(long_rows[0][2] == "H1", "sparse long header value");
    expect_true(!long_rows[0][3].empty(), "sparse long value non-empty");
  }

  const auto& wide_rows = wide_result.tables[0].sparse_wide_rows;
  expect_eq(wide_rows.size(), 12, "sparse wide row count after trimming");
  if (!wide_rows.empty()) {
    expect_eq(wide_rows[0].size(), 8, "sparse wide includes only non-empty keys");
    expect_true(wide_rows[0][0].first == "H1", "sparse wide first key");
    expect_true(wide_rows[0][0].second == "r1c1", "sparse wide first value");
  }
}

std::string run_and_serialize(const std::string& fixture_file, const std::string& query) {
  const std::string html = load_fixture(fixture_file);
  const auto result = run_query(html, query);
  return stable_table_json(result);
}

void test_golden_table_trim_baseline() {
  assert_golden(
      run_and_serialize("trailing_empty_rows_and_cols.html",
                        "SELECT table FROM doc TO TABLE();"),
      "tests/golden/table_trim/01_baseline.txt");
}

void test_golden_table_trim_rows() {
  assert_golden(
      run_and_serialize("trailing_empty_rows_and_cols.html",
                        "SELECT table FROM doc TO TABLE(TRIM_EMPTY_ROWS=ON);"),
      "tests/golden/table_trim/02_trim_empty_rows.txt");
}

void test_golden_table_trim_trailing_cols() {
  assert_golden(
      run_and_serialize("trailing_empty_rows_and_cols.html",
                        "SELECT table FROM doc TO TABLE(TRIM_EMPTY_COLS=TRAILING);"),
      "tests/golden/table_trim/03_trim_trailing_cols.txt");
}

void test_golden_table_trim_rows_and_trailing_cols() {
  assert_golden(
      run_and_serialize(
          "trailing_empty_rows_and_cols.html",
          "SELECT table FROM doc TO TABLE(TRIM_EMPTY_ROWS=ON, TRIM_EMPTY_COLS=TRAILING);"),
      "tests/golden/table_trim/04_trim_rows_and_trailing_cols.txt");
}

void test_golden_table_trim_all_cols() {
  assert_golden(
      run_and_serialize("internal_empty_col.html",
                        "SELECT table FROM doc TO TABLE(TRIM_EMPTY_COLS=ALL);"),
      "tests/golden/table_trim/05_trim_all_cols.txt");
}

void test_golden_table_stop_after_empty_rows() {
  assert_golden(
      run_and_serialize("trailing_empty_rows_and_cols.html",
                        "SELECT table FROM doc TO TABLE(STOP_AFTER_EMPTY_ROWS=2);"),
      "tests/golden/table_trim/06_stop_after_empty_rows.txt");
}

void test_golden_table_header_normalize() {
  assert_golden(
      run_and_serialize(
          "header_noise.html",
          "SELECT table FROM doc TO TABLE(HEADER=ON, HEADER_NORMALIZE=ON);"),
      "tests/golden/table_trim/07_header_normalize.txt");
}

void test_golden_table_sparse_long() {
  assert_golden(
      run_and_serialize(
          "trailing_empty_rows_and_cols.html",
          "SELECT table FROM doc TO TABLE("
          "FORMAT=SPARSE, SPARSE_SHAPE=LONG, TRIM_EMPTY_ROWS=ON, "
          "TRIM_EMPTY_COLS=TRAILING, HEADER=ON);"),
      "tests/golden/table_sparse/08_sparse_long.txt");
}

void test_golden_table_sparse_wide() {
  assert_golden(
      run_and_serialize(
          "trailing_empty_rows_and_cols.html",
          "SELECT table FROM doc TO TABLE("
          "FORMAT=SPARSE, SPARSE_SHAPE=WIDE, TRIM_EMPTY_ROWS=ON, "
          "TRIM_EMPTY_COLS=TRAILING, HEADER=ON);"),
      "tests/golden/table_sparse/09_sparse_wide.txt");
}

}  // namespace

void register_table_option_tests(std::vector<TestCase>& tests) {
  tests.push_back({"table_header_normalize_behavior", test_table_header_normalize_behavior});
  tests.push_back({"table_empty_is_semantics", test_table_empty_is_semantics});
  tests.push_back({"table_trim_empty_rows_semantics", test_table_trim_empty_rows_semantics});
  tests.push_back({"table_trim_empty_cols_semantics", test_table_trim_empty_cols_semantics});
  tests.push_back({"table_sparse_non_empty_cells_only", test_table_sparse_non_empty_cells_only});

  tests.push_back({"golden_table_trim_baseline", test_golden_table_trim_baseline});
  tests.push_back({"golden_table_trim_rows", test_golden_table_trim_rows});
  tests.push_back({"golden_table_trim_trailing_cols", test_golden_table_trim_trailing_cols});
  tests.push_back({"golden_table_trim_rows_and_trailing_cols", test_golden_table_trim_rows_and_trailing_cols});
  tests.push_back({"golden_table_trim_all_cols", test_golden_table_trim_all_cols});
  tests.push_back({"golden_table_stop_after_empty_rows", test_golden_table_stop_after_empty_rows});
  tests.push_back({"golden_table_header_normalize", test_golden_table_header_normalize});
  tests.push_back({"golden_table_sparse_long", test_golden_table_sparse_long});
  tests.push_back({"golden_table_sparse_wide", test_golden_table_sparse_wide});
}
