#pragma once

#include <string>
#include <vector>

struct TestCase {
  const char* name;
  void (*fn)();
};

void expect_true(bool condition, const std::string& message);
void expect_eq(size_t actual, size_t expected, const std::string& message);

int run_test(const TestCase& test);
int run_all_tests(const std::vector<TestCase>& tests);
