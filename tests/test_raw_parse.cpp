#include <exception>

#include "test_harness.h"
#include "test_utils.h"

namespace {

void assert_two_li_values(const markql::QueryResult& result, const std::string& context) {
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

void test_parse_from_string_expr() {
  std::string html = "<div></div>";
  auto result = run_query(html, "SELECT li FROM PARSE('<ul><li>1</li><li>2</li></ul>') AS frag");
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

void register_raw_parse_tests(std::vector<TestCase>& tests) {
  tests.push_back({"raw_source_literal", test_raw_source_literal});
  tests.push_back({"parse_from_string_expr", test_parse_from_string_expr});
  tests.push_back({"parse_from_subquery", test_parse_from_subquery});
}
