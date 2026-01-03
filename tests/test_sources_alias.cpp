#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_alias_qualifier() {
  std::string html = "<a id='root' href='x'></a>";
  auto result = run_query(html, "SELECT a FROM document AS doc WHERE doc.attributes.id = 'root'");
  expect_eq(result.rows.size(), 1, "alias qualifier");
}

void test_alias_source_only() {
  std::string html = "<a id='root' href='x'></a>";
  auto result = run_query(html, "SELECT a FROM document AS doc WHERE attributes.id = 'root'");
  expect_eq(result.rows.size(), 1, "alias source only");
}

}  // namespace

void register_source_alias_tests(std::vector<TestCase>& tests) {
  tests.push_back({"alias_qualifier", test_alias_qualifier});
  tests.push_back({"alias_source_only", test_alias_source_only});
}
