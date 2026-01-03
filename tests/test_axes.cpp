#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_child_axis_direct_only() {
  std::string html = "<div id='root'><a href='x'><span></span></a></div>";
  auto result = run_query(html, "SELECT span FROM document WHERE child.tag = 'a'");
  expect_eq(result.rows.size(), 0, "child axis direct only");
}

void test_ancestor_filter_on_a() {
  std::string html = "<div id='root'><a href='x'><span></span></a></div>";
  auto result = run_query(html, "SELECT span FROM document WHERE ancestor.tag = 'a'");
  expect_eq(result.rows.size(), 1, "ancestor filter on a");
}

void test_ancestor_attribute_filter() {
  std::string html = "<div id='root'><a href='x'><span></span></a></div>";
  auto result = run_query(html, "SELECT span FROM document WHERE ancestor.attributes.id = 'root'");
  expect_eq(result.rows.size(), 1, "ancestor attribute filter");
}

void test_descendant_attribute_filter() {
  std::string html = "<div id='root'><a href='x'><span></span></a></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE descendant.attributes.href = 'x'");
  expect_eq(result.rows.size(), 1, "descendant attribute filter");
}

void test_parent_tag_filter() {
  std::string html = "<div id='root'><span></span></div>";
  auto result = run_query(html, "SELECT span FROM document WHERE parent.tag = 'div'");
  expect_eq(result.rows.size(), 1, "parent tag filter");
}

void test_parent_id_filter() {
  std::string html = "<div id='root'><span></span></div>";
  auto base = run_query(html, "SELECT span.parent_id FROM document");
  expect_eq(base.rows.size(), 1, "parent id filter base row");
  if (base.rows.empty() || !base.rows[0].parent_id.has_value()) {
    expect_true(false, "parent id filter base value");
    return;
  }
  int64_t parent_id = *base.rows[0].parent_id;
  auto result = run_query(html, "SELECT span FROM document WHERE parent_id = " + std::to_string(parent_id));
  expect_eq(result.rows.size(), 1, "parent id filter");
}

void test_node_id_filter() {
  std::string html = "<div></div><span></span>";
  auto base = run_query(html, "SELECT span.node_id FROM document");
  expect_eq(base.rows.size(), 1, "node id filter base row");
  if (base.rows.empty()) {
    expect_true(false, "node id filter base value");
    return;
  }
  int64_t node_id = base.rows[0].node_id;
  auto result = run_query(html, "SELECT span FROM document WHERE node_id = " + std::to_string(node_id));
  expect_eq(result.rows.size(), 1, "node id filter");
}

}  // namespace

void register_axis_tests(std::vector<TestCase>& tests) {
  tests.push_back({"child_axis_direct_only", test_child_axis_direct_only});
  tests.push_back({"ancestor_filter_on_a", test_ancestor_filter_on_a});
  tests.push_back({"ancestor_attribute_filter", test_ancestor_attribute_filter});
  tests.push_back({"descendant_attribute_filter", test_descendant_attribute_filter});
  tests.push_back({"parent_tag_filter", test_parent_tag_filter});
  tests.push_back({"parent_id_filter", test_parent_id_filter});
  tests.push_back({"node_id_filter", test_node_id_filter});
}
