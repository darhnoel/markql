#include <exception>

#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_missing_closing_tags() {
  std::string html = "<html><body><div><p>Hi";
  auto result = run_query(html, "SELECT p FROM document");
  expect_true(!result.rows.empty(), "missing closing tags should still parse p");
}

void test_mismatched_nesting() {
  std::string html = "<div><span>Title</div>";
  auto result = run_query(html, "SELECT span FROM document");
  expect_true(!result.rows.empty(), "mismatched nesting should still parse span");
}

void test_junk_bytes_no_throw() {
  bool threw = false;
  try {
    std::string html = "<div>\xFF\xFE junk</div>";
    run_query(html, "SELECT div FROM document");
  } catch (const std::exception&) {
    threw = true;
  }
  expect_true(!threw, "junk bytes should not crash parser");
}

}  // namespace

void register_malformed_html_tests(std::vector<TestCase>& tests) {
  tests.push_back({"malformed_missing_closing_tags", test_missing_closing_tags});
  tests.push_back({"malformed_mismatched_nesting", test_mismatched_nesting});
  tests.push_back({"malformed_junk_bytes_no_throw", test_junk_bytes_no_throw});
}
