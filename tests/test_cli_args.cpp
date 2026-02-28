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

void test_parse_cli_args_accepts_lint_inline_query() {
  const char* argv[] = {
      "markql",
      "--lint",
      "SELECT FROM doc",
  };
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
  xsql::cli::CliOptions options;
  std::string error;
  bool ok = xsql::cli::parse_cli_args(argc, const_cast<char**>(argv), options, error);
  expect_true(ok, "lint inline query is accepted");
  expect_true(options.lint, "lint mode parsed");
  expect_true(options.query == "SELECT FROM doc", "lint inline query captured");
}

void test_parse_cli_args_accepts_lint_json_format() {
  const char* argv[] = {
      "markql",
      "--lint",
      "SELECT FROM doc",
      "--format",
      "json",
  };
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
  xsql::cli::CliOptions options;
  std::string error;
  bool ok = xsql::cli::parse_cli_args(argc, const_cast<char**>(argv), options, error);
  expect_true(ok, "lint json format is accepted");
  expect_true(options.lint_format == "json", "lint format parsed");
}

void test_parse_cli_args_rejects_format_without_lint() {
  const char* argv[] = {
      "markql",
      "--format",
      "json",
  };
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
  xsql::cli::CliOptions options;
  std::string error;
  bool ok = xsql::cli::parse_cli_args(argc, const_cast<char**>(argv), options, error);
  expect_true(!ok, "format without lint is rejected");
  expect_true(error.find("--format is only supported with --lint") != std::string::npos,
              "format without lint has clear error");
}

void test_parse_cli_args_accepts_version_flag() {
  const char* argv[] = {
      "markql",
      "--version",
  };
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
  xsql::cli::CliOptions options;
  std::string error;
  bool ok = xsql::cli::parse_cli_args(argc, const_cast<char**>(argv), options, error);
  expect_true(ok, "version flag is accepted");
  expect_true(options.show_version, "version mode parsed");
}

}  // namespace

void register_cli_args_tests(std::vector<TestCase>& tests) {
  tests.push_back({"parse_cli_args_accepts_script_flags", test_parse_cli_args_accepts_script_flags});
  tests.push_back({"parse_cli_args_rejects_missing_value", test_parse_cli_args_rejects_missing_value});
  tests.push_back({"parse_cli_args_rejects_unknown_argument", test_parse_cli_args_rejects_unknown_argument});
  tests.push_back({"parse_cli_args_rejects_query_and_query_file_together",
                   test_parse_cli_args_rejects_query_and_query_file_together});
  tests.push_back({"parse_cli_args_accepts_lint_inline_query",
                   test_parse_cli_args_accepts_lint_inline_query});
  tests.push_back({"parse_cli_args_accepts_lint_json_format",
                   test_parse_cli_args_accepts_lint_json_format});
  tests.push_back({"parse_cli_args_rejects_format_without_lint",
                   test_parse_cli_args_rejects_format_without_lint});
  tests.push_back({"parse_cli_args_accepts_version_flag",
                   test_parse_cli_args_accepts_version_flag});
}
