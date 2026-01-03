#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_shorthand_attribute_filter() {
  std::string html = "<div id='root'></div><div></div>";
  auto result = run_query(html, "SELECT div FROM document WHERE id = 'root'");
  expect_eq(result.rows.size(), 1, "shorthand attribute filter");
}

void test_shorthand_qualified_attribute_filter() {
  std::string html = "<a href='x'></a>";
  auto result = run_query(html, "SELECT a FROM document AS a WHERE a.href = 'x'");
  expect_eq(result.rows.size(), 1, "shorthand qualified attribute filter");
}

}  // namespace

void register_shorthand_tests(std::vector<TestCase>& tests) {
  tests.push_back({"shorthand_attribute_filter", test_shorthand_attribute_filter});
  tests.push_back({"shorthand_qualified_attribute_filter", test_shorthand_qualified_attribute_filter});
}
