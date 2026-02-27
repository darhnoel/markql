#include "test_harness.h"
#include "test_utils.h"

#include <stdexcept>

#include "lang/markql_parser.h"

namespace {

void test_parse_self_projection_and_where() {
  auto parsed = xsql::parse_query(
      "SELECT self.node_id, self.tag FROM document WHERE self.parent_id IS NULL LIMIT 5");
  expect_true(parsed.query.has_value(), "parse self.<field> in SELECT and WHERE");
}

void test_parse_self_function_predicate() {
  auto parsed = xsql::parse_query(
      "SELECT self.node_id, self.tag, DIRECT_TEXT(self) AS dt "
      "FROM document WHERE DIRECT_TEXT(self) LIKE '%needle%'");
  expect_true(parsed.query.has_value(), "parse DIRECT_TEXT(self) in SELECT and WHERE");
}

void test_parse_self_text_attr_inner_html_functions() {
  auto parsed = xsql::parse_query(
      "SELECT TEXT(self) AS t, ATTR(self, id) AS idv, INNER_HTML(self, MAX_DEPTH) AS ih, "
      "RAW_INNER_HTML(self, MAX_DEPTH) AS rh "
      "FROM document WHERE self.attributes.id IS NOT NULL");
  expect_true(parsed.query.has_value(), "parse TEXT/ATTR/INNER_HTML/RAW_INNER_HTML with self");
}

void test_eval_direct_text_self_without_tag_guessing() {
  std::string html = "<div>needle</div><span>other</span>";
  auto result = run_query(
      html,
      "SELECT self.node_id, self.tag, DIRECT_TEXT(self) AS dt "
      "FROM document WHERE DIRECT_TEXT(self) LIKE '%needle%'");
  expect_eq(result.rows.size(), 1, "direct_text(self) row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "div", "direct_text(self) keeps matching row");
    expect_true(result.rows[0].computed_fields["dt"] == "needle",
                "direct_text(self) extracts current row direct text");
  }
}

void test_eval_self_rebind_inside_exists_descendant() {
  std::string html =
      "<div id='a'><p>needle</p></div>"
      "<div id='b'><p>other</p></div>";
  auto result = run_query(
      html,
      "SELECT self.node_id, self.tag, ATTR(self, id) AS id "
      "FROM document "
      "WHERE tag = 'div' "
      "AND EXISTS(descendant WHERE DIRECT_TEXT(self) LIKE '%needle%')");
  expect_eq(result.rows.size(), 1, "exists(descendant) rebind row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["id"] == "a",
                "self is rebound to descendant scope inside EXISTS");
  }
}

void test_eval_text_and_attr_with_self() {
  std::string html = "<div id='x'><span>A</span></div>";
  auto result = run_query(
      html,
      "SELECT TEXT(self) AS t, ATTR(self, id) AS idv "
      "FROM document WHERE self.attributes.id IS NOT NULL");
  expect_eq(result.rows.size(), 1, "text/attr(self) row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["t"] == "A", "text(self) extracts current node text");
    expect_true(result.rows[0].computed_fields["idv"] == "x", "attr(self, ...) reads current node attribute");
  }
}

void test_eval_inner_html_with_self() {
  std::string html = "<div id='x'><span>A</span></div>";
  auto result = run_query(
      html,
      "SELECT INNER_HTML(self, MAX_DEPTH) AS ih "
      "FROM document WHERE self.attributes.id IS NOT NULL");
  expect_eq(result.rows.size(), 1, "inner_html(self) row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["ih"] == "<span>A</span>",
                "inner_html(self) uses current row node");
  }
}

void test_eval_raw_inner_html_with_self() {
  std::string html = "<div id='x'><span>A</span></div>";
  auto result = run_query(
      html,
      "SELECT RAW_INNER_HTML(self, MAX_DEPTH) AS rh "
      "FROM document WHERE self.attributes.id IS NOT NULL");
  expect_eq(result.rows.size(), 1, "raw_inner_html(self) row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["rh"] == "<span>A</span>",
                "raw_inner_html(self) uses current row node");
  }
}

void test_select_mixing_tag_and_self_projection_still_errors() {
  bool threw = false;
  try {
    (void)run_query("<div></div>", "SELECT div, self.node_id FROM document");
  } catch (const std::runtime_error& e) {
    threw = std::string(e.what()).find("Cannot mix tag-only and projected fields in SELECT") !=
            std::string::npos;
  }
  expect_true(threw, "mixing tag-only and self projection should fail clearly");
}

}  // namespace

void register_self_ref_tests(std::vector<TestCase>& tests) {
  tests.push_back({"parse_self_projection_and_where", test_parse_self_projection_and_where});
  tests.push_back({"parse_self_function_predicate", test_parse_self_function_predicate});
  tests.push_back({"parse_self_text_attr_inner_html_functions", test_parse_self_text_attr_inner_html_functions});
  tests.push_back({"eval_direct_text_self_without_tag_guessing", test_eval_direct_text_self_without_tag_guessing});
  tests.push_back({"eval_self_rebind_inside_exists_descendant", test_eval_self_rebind_inside_exists_descendant});
  tests.push_back({"eval_text_and_attr_with_self", test_eval_text_and_attr_with_self});
  tests.push_back({"eval_inner_html_with_self", test_eval_inner_html_with_self});
  tests.push_back({"eval_raw_inner_html_with_self", test_eval_raw_inner_html_with_self});
  tests.push_back({"select_mixing_tag_and_self_projection_still_errors",
                   test_select_mixing_tag_and_self_projection_still_errors});
}
