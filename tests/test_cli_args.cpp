#include "test_harness.h"

#include <vector>

#include "cli_args.h"

namespace {

void test_parse_cli_args_accepts_script_flags() {
  const char* argv[] = {
      "markql",
      "--query-file",
      "script.sql",
      "--continue-on-error",
      "--quiet",
  };
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
  xsql::cli::CliOptions options;
  std::string error;
  bool ok = xsql::cli::parse_cli_args(argc, const_cast<char**>(argv), options, error);
  expect_true(ok, "parse_cli_args accepts script flags");
  expect_true(options.query_file == "script.sql", "query-file value parsed");
  expect_true(options.continue_on_error, "continue-on-error parsed");
  expect_true(options.quiet, "quiet parsed");
}

void test_parse_cli_args_rejects_missing_value() {
  const char* argv[] = {
      "markql",
      "--query-file",
  };
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
  xsql::cli::CliOptions options;
  std::string error;
  bool ok = xsql::cli::parse_cli_args(argc, const_cast<char**>(argv), options, error);
  expect_true(!ok, "missing value is rejected");
  expect_true(error.find("Missing value for --query-file") != std::string::npos,
              "missing value has clear error");
}

void test_parse_cli_args_rejects_unknown_argument() {
  const char* argv[] = {
      "markql",
      "--unknown",
  };
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
  xsql::cli::CliOptions options;
  std::string error;
  bool ok = xsql::cli::parse_cli_args(argc, const_cast<char**>(argv), options, error);
  expect_true(!ok, "unknown argument is rejected");
  expect_true(error.find("Unknown argument: --unknown") != std::string::npos,
              "unknown argument has clear error");
}

void test_parse_cli_args_rejects_query_and_query_file_together() {
  const char* argv[] = {
      "markql",
      "--query",
      "SELECT div FROM document",
      "--query-file",
      "script.sql",
  };
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
  xsql::cli::CliOptions options;
  std::string error;
  bool ok = xsql::cli::parse_cli_args(argc, const_cast<char**>(argv), options, error);
  expect_true(!ok, "query and query-file together are rejected");
  expect_true(error.find("mutually exclusive") != std::string::npos,
              "mutual exclusion has clear error");
}

}  // namespace

void register_cli_args_tests(std::vector<TestCase>& tests) {
  tests.push_back({"parse_cli_args_accepts_script_flags", test_parse_cli_args_accepts_script_flags});
  tests.push_back({"parse_cli_args_rejects_missing_value", test_parse_cli_args_rejects_missing_value});
  tests.push_back({"parse_cli_args_rejects_unknown_argument", test_parse_cli_args_rejects_unknown_argument});
  tests.push_back({"parse_cli_args_rejects_query_and_query_file_together",
                   test_parse_cli_args_rejects_query_and_query_file_together});
}
