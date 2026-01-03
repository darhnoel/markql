#include <exception>

#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_select_ul_by_id() {
  std::string html = "<ul id='countries'><li>US</li></ul>";
  auto result = run_query(html, "SELECT ul FROM document WHERE attributes.id = 'countries'");
  expect_eq(result.rows.size(), 1, "select ul by id");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "ul", "tag is ul");
  }
}

void test_class_in_matches_token() {
  std::string html = "<div class=\"subtle newest\"></div><div class=\"old\"></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE attributes.class IN ('newest')");
  expect_eq(result.rows.size(), 1, "class IN matches token");
}

void test_parent_attribute_filter() {
  std::string html = "<div id='table-01'><table></table></div><div id='table-02'><table></table></div>";
  auto result = run_query(html, "SELECT table FROM document WHERE parent.attributes.id = 'table-01'");
  expect_eq(result.rows.size(), 1, "parent attribute filter");
}

void test_multi_tag_select() {
  std::string html = "<h1></h1><h2></h2><p></p>";
  auto result = run_query(html, "SELECT h1,h2 FROM document");
  expect_eq(result.rows.size(), 2, "multi-tag select");
}

void test_select_star() {
  std::string html = "<div></div><span></span>";
  auto result = run_query(html, "SELECT * FROM document");
  expect_true(result.rows.size() >= 2, "select star returns at least html nodes");
  bool saw_div = false;
  bool saw_span = false;
  for (const auto& row : result.rows) {
    if (row.tag == "div") saw_div = true;
    if (row.tag == "span") saw_span = true;
  }
  expect_true(saw_div && saw_span, "select star includes div/span");
}

void test_class_eq_matches_token() {
  std::string html = "<div class=\"subtle newest\"></div><div class=\"newest\"></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE attributes.class = 'subtle'");
  expect_eq(result.rows.size(), 1, "class = matches token");
}

void test_missing_attribute_no_match() {
  std::string html = "<div></div><div id='a'></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE attributes.id = 'missing'");
  expect_eq(result.rows.size(), 0, "missing attribute yields no match");
}

void test_invalid_query_throws() {
  bool threw = false;
  try {
    std::string html = "<div></div>";
    run_query(html, "SELECT FROM document");
  } catch (const std::exception&) {
    threw = true;
  }
  expect_true(threw, "invalid query throws");
}

void test_limit() {
  std::string html = "<div></div><div></div><div></div>";
  auto result = run_query(html, "SELECT div FROM document LIMIT 2");
  expect_eq(result.rows.size(), 2, "limit");
}

}  // namespace

void register_query_basic_tests(std::vector<TestCase>& tests) {
  tests.push_back({"select_ul_by_id", test_select_ul_by_id});
  tests.push_back({"class_in_matches_token", test_class_in_matches_token});
  tests.push_back({"parent_attribute_filter", test_parent_attribute_filter});
  tests.push_back({"multi_tag_select", test_multi_tag_select});
  tests.push_back({"select_star", test_select_star});
  tests.push_back({"class_eq_matches_token", test_class_eq_matches_token});
  tests.push_back({"missing_attribute_no_match", test_missing_attribute_no_match});
  tests.push_back({"invalid_query_throws", test_invalid_query_throws});
  tests.push_back({"limit", test_limit});
}
