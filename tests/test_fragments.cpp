#include <exception>

#include "test_harness.h"
#include "test_utils.h"

namespace {

void assert_two_li_values(const xsql::QueryResult& result, const std::string& context) {
  expect_eq(result.rows.size(), 2, context + " row count");
  if (result.rows.size() < 2) return;
  expect_true(result.rows[0].tag == "li", context + " row1 tag");
  expect_true(result.rows[1].tag == "li", context + " row2 tag");
  expect_true(result.rows[0].text == "1", context + " row1 text");
  expect_true(result.rows[1].text == "2", context + " row2 text");
}

void test_raw_source_literal() {
  std::string html = "<div></div>";
  auto result = run_query(html, "SELECT li FROM RAW('<ul><li>1</li><li>2</li></ul>')");
  assert_two_li_values(result, "RAW() source literal parses list items");
}

void test_fragments_from_raw() {
  std::string html = "<div></div>";
  auto result = run_query(html,
                          "SELECT li FROM FRAGMENTS(RAW('<ul><li>1</li><li>2</li></ul>')) AS frag");
  assert_two_li_values(result, "FRAGMENTS() parses RAW() fragments");
}

void test_fragments_from_inner_html() {
  std::string html = "<div class='pagination'><ul><li>1</li><li>2</li></ul></div>";
  auto result = run_query(html,
                          "SELECT li FROM FRAGMENTS(SELECT inner_html(div, 2) FROM document "
                          "WHERE attributes.class = 'pagination') AS frag");
  assert_two_li_values(result, "FRAGMENTS() parses inner_html fragments");
}

void test_fragments_non_html_error() {
  bool threw = false;
  try {
    std::string html = "<a href='x'></a>";
    run_query(html,
              "SELECT li FROM FRAGMENTS(SELECT attributes.href FROM document) AS frag");
  } catch (const std::exception&) {
    threw = true;
  }
  expect_true(threw, "FRAGMENTS rejects non-HTML input");
}

void test_fragments_warn_deprecated() {
  std::string html = "<div></div>";
  auto result = run_query(html,
                          "SELECT li FROM FRAGMENTS(RAW('<ul><li>1</li><li>2</li></ul>')) AS frag");
  assert_two_li_values(result, "FRAGMENTS deprecation output");
  expect_true(!result.warnings.empty(), "FRAGMENTS emits deprecation warning");
  if (!result.warnings.empty()) {
    expect_true(result.warnings[0].find("deprecated") != std::string::npos,
                "FRAGMENTS warning message content");
  }
}

void test_parse_from_string_expr() {
  std::string html = "<div></div>";
  auto result = run_query(html,
                          "SELECT li FROM PARSE('<ul><li>1</li><li>2</li></ul>') AS frag");
  assert_two_li_values(result, "PARSE() parses HTML string");
  expect_true(result.warnings.empty(), "PARSE() has no deprecation warning");
}

void test_parse_from_subquery() {
  std::string html = "<div class='pagination'><ul><li>1</li><li>2</li></ul></div>";
  auto result = run_query(html,
                          "SELECT li FROM PARSE(SELECT inner_html(div, 2) FROM document "
                          "WHERE attributes.class = 'pagination') AS frag");
  assert_two_li_values(result, "PARSE() parses subquery fragments");
  expect_true(result.warnings.empty(), "PARSE() subquery has no deprecation warning");
}

}  // namespace

void register_fragments_tests(std::vector<TestCase>& tests) {
  tests.push_back({"raw_source_literal", test_raw_source_literal});
  tests.push_back({"fragments_from_raw", test_fragments_from_raw});
  tests.push_back({"fragments_from_inner_html", test_fragments_from_inner_html});
  tests.push_back({"fragments_non_html_error", test_fragments_non_html_error});
  tests.push_back({"fragments_warn_deprecated", test_fragments_warn_deprecated});
  tests.push_back({"parse_from_string_expr", test_parse_from_string_expr});
  tests.push_back({"parse_from_subquery", test_parse_from_subquery});
}
