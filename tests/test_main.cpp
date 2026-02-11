#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "test_harness.h"

void register_query_basic_tests(std::vector<TestCase>& tests);
void register_source_alias_tests(std::vector<TestCase>& tests);
void register_shorthand_tests(std::vector<TestCase>& tests);
void register_projection_tests(std::vector<TestCase>& tests);
void register_function_tests(std::vector<TestCase>& tests);
void register_axis_tests(std::vector<TestCase>& tests);
void register_predicate_tests(std::vector<TestCase>& tests);
void register_order_by_tests(std::vector<TestCase>& tests);
void register_duckbox_tests(std::vector<TestCase>& tests);
void register_export_tests(std::vector<TestCase>& tests);
void register_flatten_text_tests(std::vector<TestCase>& tests);
void register_flatten_extract_tests(std::vector<TestCase>& tests);
void register_repl_tests(std::vector<TestCase>& tests);
void register_malformed_html_tests(std::vector<TestCase>& tests);
void register_fragments_tests(std::vector<TestCase>& tests);
void register_guardrails_tests(std::vector<TestCase>& tests);
void register_meta_command_tests(std::vector<TestCase>& tests);
void register_cli_utils_tests(std::vector<TestCase>& tests);
void register_string_sql_tests(std::vector<TestCase>& tests);
void register_column_name_tests(std::vector<TestCase>& tests);
#ifdef XSQL_ENABLE_KHMER_NUMBER
void register_khmer_number_tests(std::vector<TestCase>& tests);
#endif

namespace {

std::unordered_set<std::string> parse_skip_list_from_env() {
  std::unordered_set<std::string> out;
  const char* raw = std::getenv("XSQL_TEST_SKIP");
  if (!raw || !*raw) return out;
  std::istringstream iss(raw);
  std::string token;
  while (std::getline(iss, token, ',')) {
    size_t start = 0;
    while (start < token.size() && std::isspace(static_cast<unsigned char>(token[start]))) ++start;
    size_t end = token.size();
    while (end > start && std::isspace(static_cast<unsigned char>(token[end - 1]))) --end;
    if (end > start) out.insert(token.substr(start, end - start));
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<TestCase> tests;
  tests.reserve(64);
  register_query_basic_tests(tests);
  register_source_alias_tests(tests);
  register_shorthand_tests(tests);
  register_projection_tests(tests);
  register_function_tests(tests);
  register_axis_tests(tests);
  register_predicate_tests(tests);
  register_order_by_tests(tests);
  register_duckbox_tests(tests);
  register_export_tests(tests);
  register_flatten_text_tests(tests);
  register_flatten_extract_tests(tests);
  register_repl_tests(tests);
  register_malformed_html_tests(tests);
  register_fragments_tests(tests);
  register_guardrails_tests(tests);
  register_meta_command_tests(tests);
  register_cli_utils_tests(tests);
  register_string_sql_tests(tests);
  register_column_name_tests(tests);
#ifdef XSQL_ENABLE_KHMER_NUMBER
  register_khmer_number_tests(tests);
#endif

  const auto skip_tests = parse_skip_list_from_env();

  if (argc > 1) {
    std::string target = argv[1];
    if (skip_tests.find(target) != skip_tests.end()) {
      std::cout << "SKIPPED: " << target << std::endl;
      return EXIT_SUCCESS;
    }
    for (const auto& test : tests) {
      if (target == test.name) {
        int failures = run_test(test);
        return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
      }
    }
    std::cerr << "Unknown test: " << target << std::endl;
    std::cerr << "Available tests:" << std::endl;
    for (const auto& test : tests) {
      std::cerr << "  " << test.name << std::endl;
    }
    return EXIT_FAILURE;
  }

  if (skip_tests.empty()) {
    return run_all_tests(tests);
  }

  std::vector<TestCase> filtered;
  filtered.reserve(tests.size());
  for (const auto& test : tests) {
    if (skip_tests.find(test.name) != skip_tests.end()) {
      std::cout << "SKIPPED: " << test.name << std::endl;
      continue;
    }
    filtered.push_back(test);
  }
  return run_all_tests(filtered);
}
