#include <exception>

#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_limit_cap() {
  bool threw = false;
  try {
    std::string html = "<div></div>";
    run_query(html, "SELECT div FROM document LIMIT 100001");
  } catch (const std::exception&) {
    threw = true;
  }
  expect_true(threw, "LIMIT cap enforced");
}

void test_regex_length_cap() {
  bool threw = false;
  try {
    std::string html = "<div id='a'></div>";
    std::string pattern(2000, 'a');
    run_query(html, "SELECT div FROM document WHERE attributes.id ~ '" + pattern + "'");
  } catch (const std::exception&) {
    threw = true;
  }
  expect_true(threw, "regex length cap enforced");
}

}  // namespace

void register_guardrails_tests(std::vector<TestCase>& tests) {
  tests.push_back({"guardrails_limit_cap", test_limit_cap});
  tests.push_back({"guardrails_regex_length_cap", test_regex_length_cap});
}
