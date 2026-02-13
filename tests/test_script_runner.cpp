#include "test_harness.h"
#include "test_utils.h"

#include <sstream>
#include <stdexcept>

#include "cli_utils.h"
#include "script_runner.h"

namespace {

void test_split_script_ignores_empty_statements() {
  auto split = xsql::cli::split_sql_script(";; SELECT div FROM document; ; SELECT span FROM document;;");
  expect_true(!split.error_message.has_value(), "split script has no lexer error");
  expect_eq(split.statements.size(), 2, "empty statements are ignored");
}

void test_split_script_with_comments() {
  std::string script =
      "-- leading\n"
      "SELECT div FROM document;\n"
      "/* block */\n"
      "SELECT span FROM document;";
  auto split = xsql::cli::split_sql_script(script);
  expect_true(!split.error_message.has_value(), "split with comments has no lexer error");
  expect_eq(split.statements.size(), 2, "comments between statements are ignored");
}

void test_split_script_unterminated_block_comment() {
  auto split = xsql::cli::split_sql_script("SELECT div FROM document; /* not closed");
  expect_true(split.error_message.has_value(), "unterminated block comment reports split error");
  if (split.error_message.has_value()) {
    expect_true(*split.error_message == "Unterminated block comment",
                "split error message is deterministic");
  }
}

void test_split_script_semicolon_and_comment_markers_inside_string_literals() {
  std::string script =
      "SELECT div FROM document WHERE text = 'a;--b/*c*/';\n"
      "SELECT span FROM document;";
  auto split = xsql::cli::split_sql_script(script);
  expect_true(!split.error_message.has_value(), "split handles markers in string literals");
  expect_eq(split.statements.size(), 2, "string literal markers do not break statement boundaries");
}

void test_run_script_multi_statement_delimiters() {
  std::string script =
      "SHOW FUNCTIONS;\n"
      "SELECT div FROM document WHERE tag = 'div';";
  std::ostringstream out;
  std::ostringstream err;
  size_t executed = 0;
  auto exec = [&](const std::string& statement) {
    ++executed;
    (void)run_query("<div></div>", statement);
  };
  xsql::cli::ScriptRunOptions options;
  int code = xsql::cli::run_sql_script(script, options, exec, out, err);
  expect_eq(code, 0, "multi-statement script exits 0");
  expect_eq(executed, 2, "multi-statement script executes all statements");
  std::string stdout_text = out.str();
  expect_true(stdout_text.find("== stmt 1/2 ==") != std::string::npos,
              "delimiter printed for statement 1");
  expect_true(stdout_text.find("== stmt 2/2 ==") != std::string::npos,
              "delimiter printed for statement 2");
}

void test_run_script_comments_and_empty_statements() {
  std::string script =
      ";;\n"
      "-- comment\n"
      "SELECT div FROM document;\n"
      "/* another comment */\n"
      ";\n"
      "SELECT span FROM document;\n";
  std::ostringstream out;
  std::ostringstream err;
  size_t executed = 0;
  auto exec = [&](const std::string& statement) {
    ++executed;
    (void)run_query("<div></div><span></span>", statement);
  };
  xsql::cli::ScriptRunOptions options;
  int code = xsql::cli::run_sql_script(script, options, exec, out, err);
  expect_eq(code, 0, "script with comments exits 0");
  expect_eq(executed, 2, "script with comments executes non-empty statements only");
}

void test_run_script_stops_on_first_error_by_default() {
  std::string script =
      "SELECT div FROM document;\n"
      "SELECT FROM document;\n"
      "SELECT span FROM document;";
  std::ostringstream out;
  std::ostringstream err;
  size_t executed = 0;
  auto exec = [&](const std::string& statement) {
    ++executed;
    (void)run_query("<div></div><span></span>", statement);
  };
  xsql::cli::ScriptRunOptions options;
  int code = xsql::cli::run_sql_script(script, options, exec, out, err);
  expect_eq(code, 1, "script stops with exit 1 on first parse error");
  expect_eq(executed, 1, "default script mode stops before later statements");
  std::string stderr_text = err.str();
  expect_true(stderr_text.find("statement 2/3") != std::string::npos,
              "error includes statement index");
  expect_true(stderr_text.find("line ") != std::string::npos,
              "error includes line information");
}

void test_run_script_continue_on_error() {
  std::string script =
      "SELECT div FROM document;\n"
      "SELECT FROM document;\n"
      "SELECT span FROM document;";
  std::ostringstream out;
  std::ostringstream err;
  size_t executed = 0;
  auto exec = [&](const std::string& statement) {
    ++executed;
    (void)run_query("<div></div><span></span>", statement);
  };
  xsql::cli::ScriptRunOptions options;
  options.continue_on_error = true;
  int code = xsql::cli::run_sql_script(script, options, exec, out, err);
  expect_eq(code, 1, "continue-on-error still returns 1 when any statement fails");
  expect_eq(executed, 2, "continue-on-error executes later valid statements");
}

void test_run_script_quiet_suppresses_delimiter() {
  std::string script = "SELECT div FROM document; SELECT span FROM document;";
  std::ostringstream out;
  std::ostringstream err;
  auto exec = [&](const std::string& statement) {
    (void)run_query("<div></div><span></span>", statement);
  };
  xsql::cli::ScriptRunOptions options;
  options.quiet = true;
  int code = xsql::cli::run_sql_script(script, options, exec, out, err);
  expect_eq(code, 0, "quiet mode script exits 0");
  expect_true(out.str().find("== stmt") == std::string::npos,
              "quiet mode suppresses delimiters");
}

void test_utf8_validation_for_script_file_content() {
  expect_true(xsql::cli::is_valid_utf8("SELECT div FROM document;"),
              "valid UTF-8 script is accepted");
  std::string invalid = "SELECT ";
  invalid.push_back(static_cast<char>(0xC3));
  expect_true(!xsql::cli::is_valid_utf8(invalid),
              "invalid UTF-8 script content is rejected");
}

}  // namespace

void register_script_runner_tests(std::vector<TestCase>& tests) {
  tests.push_back({"split_script_ignores_empty_statements", test_split_script_ignores_empty_statements});
  tests.push_back({"split_script_with_comments", test_split_script_with_comments});
  tests.push_back({"split_script_unterminated_block_comment", test_split_script_unterminated_block_comment});
  tests.push_back({"split_script_semicolon_and_comment_markers_inside_string_literals",
                   test_split_script_semicolon_and_comment_markers_inside_string_literals});
  tests.push_back({"run_script_multi_statement_delimiters", test_run_script_multi_statement_delimiters});
  tests.push_back({"run_script_comments_and_empty_statements", test_run_script_comments_and_empty_statements});
  tests.push_back({"run_script_stops_on_first_error_by_default", test_run_script_stops_on_first_error_by_default});
  tests.push_back({"run_script_continue_on_error", test_run_script_continue_on_error});
  tests.push_back({"run_script_quiet_suppresses_delimiter", test_run_script_quiet_suppresses_delimiter});
  tests.push_back({"utf8_validation_for_script_file_content", test_utf8_validation_for_script_file_content});
}
