#include "test_harness.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;
std::string g_current_test;

}  // namespace

void expect_true(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL [" << g_current_test << "]: " << message << std::endl;
    ++g_failures;
  }
}

void expect_eq(size_t actual, size_t expected, const std::string& message) {
  if (actual != expected) {
    std::cerr << "FAIL [" << g_current_test << "]: " << message
              << " (expected " << expected << ", got " << actual << ")" << std::endl;
    ++g_failures;
  }
}

int run_test(const TestCase& test) {
  g_current_test = test.name;
  g_failures = 0;
  test.fn();
  return g_failures;
}

int run_all_tests(const std::vector<TestCase>& tests) {
  int total_failures = 0;
  for (const auto& test : tests) {
    int failures = run_test(test);
    if (failures > 0) {
      std::cerr << "FAILED: " << test.name << " (" << failures << ")" << std::endl;
      total_failures += failures;
    }
  }
  if (total_failures > 0) {
    std::cerr << total_failures << " test(s) failed." << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "All tests passed." << std::endl;
  return EXIT_SUCCESS;
}
