#include "test_harness.h"
#include "test_utils.h"

#include "query_parser.h"

namespace {

void test_parse_like_predicate() {
  auto parsed = xsql::parse_query("SELECT div FROM document WHERE text LIKE '%foo%'");
  expect_true(parsed.query.has_value(), "parse LIKE predicate");
}

void test_parse_position_with_in() {
  auto parsed = xsql::parse_query(
      "SELECT li FROM document WHERE POSITION('coupon' IN LOWER(TEXT(li))) > 0");
  expect_true(parsed.query.has_value(), "parse POSITION(... IN ...)");
}

void test_parse_project_nested_string_functions() {
  auto parsed = xsql::parse_query(
      "SELECT PROJECT(li) AS (slug: LOWER(REPLACE(TRIM(TEXT(h2)), ' ', '-'))) "
      "FROM document WHERE EXISTS(child WHERE tag = 'h2')");
  expect_true(parsed.query.has_value(), "parse PROJECT nested string functions");
}

void test_parse_legacy_has_direct_text() {
  auto parsed = xsql::parse_query("SELECT div FROM document WHERE div HAS_DIRECT_TEXT 'hr'");
  expect_true(parsed.query.has_value(), "parse legacy HAS_DIRECT_TEXT");
}

void test_parse_scoped_selector_inside_text() {
  auto parsed = xsql::parse_query(
      "SELECT PROJECT(li) AS (dur: TEXT(span WHERE span HAS_DIRECT_TEXT 'hr')) "
      "FROM document WHERE EXISTS(child WHERE tag = 'span')");
  expect_true(parsed.query.has_value(), "parse scoped selector inside TEXT()");
}

void test_eval_like_wildcards() {
  std::string html = "<div>abc</div><div>axc</div><div>zzz</div>";
  auto percent = run_query(html, "SELECT div FROM document WHERE text LIKE '%c'");
  expect_eq(percent.rows.size(), 2, "LIKE % wildcard");

  auto underscore = run_query(html, "SELECT div FROM document WHERE text LIKE 'a_c'");
  expect_eq(underscore.rows.size(), 2, "LIKE _ wildcard");
}

void test_eval_string_functions_in_select() {
  std::string html = "<div class='a'>  Hello World  </div><div>Plain</div>";
  auto result = run_query(
      html,
      "SELECT CONCAT(attributes.class, '-x') AS concat, "
      "SUBSTRING(TRIM(TEXT(div)), 1, 5) AS sub, "
      "REPLACE(TEXT(div), 'World', 'MarkQL') AS replaced "
      "FROM document WHERE tag = 'div' ORDER BY node_id ASC");
  expect_eq(result.rows.size(), 2, "string function select row count");
  if (result.rows.size() >= 2) {
    expect_true(result.rows[0].computed_fields["concat"] == "a-x", "concat non-null behavior");
    expect_true(result.rows[0].computed_fields["sub"] == "Hello", "substring is 1-based");
    expect_true(result.rows[0].computed_fields["replaced"] == "  Hello MarkQL  ", "replace output");
    expect_true(result.rows[1].computed_fields.find("concat") == result.rows[1].computed_fields.end(),
                "concat null propagation");
  }
}

void test_eval_length_byte_semantics() {
  std::string html = "<div>é</div>";
  auto result = run_query(html, "SELECT LENGTH(TEXT(div)) AS len FROM document WHERE tag = 'div'");
  expect_eq(result.rows.size(), 1, "length row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["len"] == "2", "length uses UTF-8 byte count");
  }
}

void test_eval_position_found_and_not_found() {
  std::string html = "<div>hello world</div><div>abc</div>";
  auto found = run_query(
      html,
      "SELECT div FROM document WHERE POSITION('world' IN TEXT(div)) = 7");
  expect_eq(found.rows.size(), 1, "position found");
  auto missing = run_query(
      html,
      "SELECT div FROM document WHERE POSITION('world' IN TEXT(div)) = 0");
  expect_eq(missing.rows.size(), 1, "position not found returns 0");
}

void test_eval_direct_text_excludes_descendants() {
  std::string html =
      "<div>Top<span>Nested</span></div>"
      "<div><span>Top</span></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE DIRECT_TEXT(div) LIKE '%Top%'");
  expect_eq(result.rows.size(), 1, "direct_text excludes descendant text");
}

void test_project_missing_match_null_and_coalesce() {
  std::string html =
      "<ul>"
      "<li><span>A</span></li>"
      "<li><em>B</em></li>"
      "</ul>";
  auto result = run_query(
      html,
      "SELECT li.node_id, PROJECT(li) AS ("
      "missing: TEXT(span WHERE span HAS_DIRECT_TEXT 'Z'),"
      "fallback: COALESCE(TEXT(span WHERE span HAS_DIRECT_TEXT 'Z'), TEXT(em))"
      ") FROM document WHERE tag = 'li' ORDER BY node_id ASC");
  expect_eq(result.rows.size(), 2, "project null and coalesce row count");
  if (result.rows.size() >= 2) {
    expect_true(result.rows[0].computed_fields.find("missing") == result.rows[0].computed_fields.end(),
                "project missing selector yields null");
    expect_true(result.rows[1].computed_fields["fallback"] == "B",
                "coalesce returns first non-null");
  }
}

void test_project_selector_scope_and_first_match() {
  std::string html =
      "<div>"
      "<li><h2>A1</h2><h2>A2</h2></li>"
      "<li><h2>B1</h2><h2>B2</h2></li>"
      "</div>";
  auto result = run_query(
      html,
      "SELECT li.node_id, PROJECT(li) AS (title: TEXT(h2)) "
      "FROM document WHERE tag = 'li' ORDER BY node_id ASC");
  expect_eq(result.rows.size(), 2, "project scope row count");
  if (result.rows.size() >= 2) {
    expect_true(result.rows[0].computed_fields["title"] == "A1", "project first match row1");
    expect_true(result.rows[1].computed_fields["title"] == "B1", "project first match row2");
  }
}

void test_project_top_level_comparison_expression() {
  std::string html =
      "<ul>"
      "<li><h2>A</h2><span>has coupon</span></li>"
      "<li><h2>B</h2><span>no deal</span></li>"
      "</ul>";
  auto result = run_query(
      html,
      "SELECT PROJECT(li) AS (has_coupon: POSITION('coupon' IN LOWER(TEXT(li))) > 0) "
      "FROM document WHERE tag = 'li' ORDER BY node_id ASC");
  expect_eq(result.rows.size(), 2, "project comparison row count");
  if (result.rows.size() >= 2) {
    expect_true(result.rows[0].computed_fields["has_coupon"] == "true",
                "project comparison true case");
    expect_true(result.rows[1].computed_fields["has_coupon"] == "false",
                "project comparison false case");
  }
}

void test_regression_canonical_project_query() {
  std::string html =
      "<ul>"
      "<li><h3>London</h3><span>2 stops</span><span>22 hr 5 min</span><span role='text'>258564 JPY</span></li>"
      "<li><h3>Milan</h3><span>1 stop</span><span>24 hr 40 min</span><span role='text'>287610 JPY</span></li>"
      "</ul>";
  auto result = run_query(
      html,
      "SELECT li.node_id, PROJECT(li) AS ("
      "city: TEXT(h3),"
      "stops: TEXT(span WHERE span HAS_DIRECT_TEXT 'stop'),"
      "duration: COALESCE("
      "TEXT(span WHERE span HAS_DIRECT_TEXT 'hr'),"
      "TEXT(span WHERE span HAS_DIRECT_TEXT 'min')"
      "),"
      "price: TEXT(span WHERE attributes.role = 'text')"
      ") "
      "FROM doc "
      "WHERE EXISTS(descendant WHERE tag = 'h3') "
      "AND EXISTS(descendant WHERE attributes.role = 'text')");
  expect_eq(result.rows.size(), 2, "canonical project regression rows");
  if (result.rows.size() >= 2) {
    expect_true(result.rows[0].computed_fields["city"] == "London", "canonical city row1");
    expect_true(result.rows[1].computed_fields["city"] == "Milan", "canonical city row2");
  }
}

}  // namespace

void register_string_sql_tests(std::vector<TestCase>& tests) {
  tests.push_back({"parse_like_predicate", test_parse_like_predicate});
  tests.push_back({"parse_position_with_in", test_parse_position_with_in});
  tests.push_back({"parse_project_nested_string_functions", test_parse_project_nested_string_functions});
  tests.push_back({"parse_legacy_has_direct_text", test_parse_legacy_has_direct_text});
  tests.push_back({"parse_scoped_selector_inside_text", test_parse_scoped_selector_inside_text});
  tests.push_back({"eval_like_wildcards", test_eval_like_wildcards});
  tests.push_back({"eval_string_functions_in_select", test_eval_string_functions_in_select});
  tests.push_back({"eval_length_byte_semantics", test_eval_length_byte_semantics});
  tests.push_back({"eval_position_found_and_not_found", test_eval_position_found_and_not_found});
  tests.push_back({"eval_direct_text_excludes_descendants", test_eval_direct_text_excludes_descendants});
  tests.push_back({"project_missing_match_null_and_coalesce", test_project_missing_match_null_and_coalesce});
  tests.push_back({"project_selector_scope_and_first_match", test_project_selector_scope_and_first_match});
  tests.push_back({"project_top_level_comparison_expression", test_project_top_level_comparison_expression});
  tests.push_back({"regression_canonical_project_query", test_regression_canonical_project_query});
}
