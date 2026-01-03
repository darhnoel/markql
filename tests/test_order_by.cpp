#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_order_by_tag() {
  std::string html = "<div></div><span></span>";
  auto result = run_query(html, "SELECT * FROM document ORDER BY tag");
  expect_true(result.rows.size() >= 2, "order by tag row count");
  if (result.rows.size() >= 2) {
    expect_true(result.rows[0].tag <= result.rows[1].tag, "order by tag sort");
  }
}

void test_order_by_node_id_desc() {
  std::string html = "<div></div><span></span>";
  auto result = run_query(html, "SELECT * FROM document ORDER BY node_id DESC");
  expect_true(result.rows.size() >= 2, "order by node_id desc row count");
  if (result.rows.size() >= 2) {
    expect_true(result.rows[0].node_id >= result.rows[1].node_id, "order by node_id desc sort");
  }
}

void test_order_by_multi() {
  std::string html = "<div></div><div></div><span></span>";
  auto result = run_query(html, "SELECT * FROM document ORDER BY tag, node_id DESC");
  expect_true(result.rows.size() >= 2, "order by multi row count");
}

}  // namespace

void register_order_by_tests(std::vector<TestCase>& tests) {
  tests.push_back({"order_by_tag", test_order_by_tag});
  tests.push_back({"order_by_node_id_desc", test_order_by_node_id_desc});
  tests.push_back({"order_by_multi", test_order_by_multi});
}
