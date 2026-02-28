#include "test_harness.h"

#include <vector>

#include "xsql/diagnostics.h"
#include "xsql/version.h"

namespace {

void test_lint_syntax_diagnostic_has_stable_code_and_span() {
  std::vector<xsql::Diagnostic> diagnostics = xsql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "syntax diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.severity == xsql::DiagnosticSeverity::Error, "syntax severity is error");
  expect_true(first.code == "MQL-SYN-0001", "syntax code is stable");
  expect_true(first.span.start_line == 1, "syntax span line");
  expect_true(first.span.start_col > 1, "syntax span col");
}

void test_lint_semantic_diagnostic_has_stable_code() {
  std::vector<xsql::Diagnostic> diagnostics = xsql::lint_query(
      "SELECT TEXT(n) "
      "FROM doc AS n "
      "WHERE n.tag = 'div'");
  expect_true(!diagnostics.empty(), "semantic diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.code == "MQL-SEM-0301", "semantic code is stable");
  expect_true(first.message.find("TEXT()/INNER_HTML()/RAW_INNER_HTML()") != std::string::npos,
              "semantic message text");
  expect_true(!first.help.empty(), "semantic help is present");
  expect_true(!first.doc_ref.empty(), "semantic doc ref is present");
}

void test_diagnostic_text_renderer_contains_help_and_caret() {
  std::vector<xsql::Diagnostic> diagnostics = xsql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "diagnostics available");
  if (diagnostics.empty()) return;
  std::string rendered = xsql::render_diagnostics_text(diagnostics);
  expect_true(rendered.find("help:") != std::string::npos, "text renderer has help");
  expect_true(rendered.find("^") != std::string::npos, "text renderer has caret snippet");
}

void test_diagnostic_json_renderer_contains_stable_fields() {
  std::vector<xsql::Diagnostic> diagnostics = xsql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "diagnostics available for json");
  if (diagnostics.empty()) return;
  std::string json = xsql::render_diagnostics_json(diagnostics);
  expect_true(json.find("\"severity\":\"ERROR\"") != std::string::npos, "json severity");
  expect_true(json.find("\"code\":\"MQL-SYN-0001\"") != std::string::npos, "json code");
  expect_true(json.find("\"span\"") != std::string::npos, "json span");
  expect_true(json.find("\"snippet\"") != std::string::npos, "json snippet");
}

void test_diagnostic_json_renderer_matches_snapshot() {
  std::vector<xsql::Diagnostic> diagnostics = xsql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "diagnostics available for json snapshot");
  if (diagnostics.empty()) return;
  std::string json = xsql::render_diagnostics_json(diagnostics);
  const std::string expected =
      "[{\"severity\":\"ERROR\",\"code\":\"MQL-SYN-0001\",\"message\":\"Expected tag identifier\","
      "\"help\":\"Check SQL clause order: WITH ... SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ... TO ...\","
      "\"doc_ref\":\"docs/book/appendix-grammar.md\","
      "\"span\":{\"start_line\":1,\"start_col\":8,\"end_line\":1,\"end_col\":9,\"byte_start\":7,\"byte_end\":8},"
      "\"snippet\":\" --> line 1, col 8\\n  |\\n1 | SELECT FROM doc\\n  |        ^\","
      "\"related\":[]}]";
  expect_true(json == expected, "json snapshot stable");
}

void test_diagnostic_text_renderer_matches_golden_snippet() {
  std::vector<xsql::Diagnostic> diagnostics = xsql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "diagnostics available for golden text");
  if (diagnostics.empty()) return;
  std::string rendered = xsql::render_diagnostics_text(diagnostics);
  const std::string expected =
      "ERROR[MQL-SYN-0001]: Expected tag identifier\n"
      " --> line 1, col 8\n"
      "  |\n"
      "1 | SELECT FROM doc\n"
      "  |        ^\n"
      "help: Check SQL clause order: WITH ... SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ... TO ...\n";
  expect_true(rendered == expected, "golden diagnostic text");
}

void test_diagnose_query_failure_maps_parse_error() {
  std::vector<xsql::Diagnostic> diagnostics =
      xsql::diagnose_query_failure("SELECT FROM doc", "Query parse error: Expected tag identifier");
  expect_true(!diagnostics.empty(), "mapped parse failure diagnostics");
  if (diagnostics.empty()) return;
  expect_true(diagnostics.front().code == "MQL-SYN-0001", "mapped parse code");
}

void test_version_string_contains_provenance() {
  xsql::VersionInfo info = xsql::get_version_info();
  expect_true(!info.version.empty(), "version field available");
  expect_true(!info.git_commit.empty(), "git commit field available");
  std::string rendered = xsql::version_string();
  expect_true(rendered.find(info.version) != std::string::npos, "rendered includes version");
  expect_true(rendered.find(info.git_commit) != std::string::npos, "rendered includes commit");
}

}  // namespace

void register_diagnostic_tests(std::vector<TestCase>& tests) {
  tests.push_back({"lint_syntax_diagnostic_has_stable_code_and_span",
                   test_lint_syntax_diagnostic_has_stable_code_and_span});
  tests.push_back({"lint_semantic_diagnostic_has_stable_code",
                   test_lint_semantic_diagnostic_has_stable_code});
  tests.push_back({"diagnostic_text_renderer_contains_help_and_caret",
                   test_diagnostic_text_renderer_contains_help_and_caret});
  tests.push_back({"diagnostic_json_renderer_contains_stable_fields",
                   test_diagnostic_json_renderer_contains_stable_fields});
  tests.push_back({"diagnostic_json_renderer_matches_snapshot",
                   test_diagnostic_json_renderer_matches_snapshot});
  tests.push_back({"diagnostic_text_renderer_matches_golden_snippet",
                   test_diagnostic_text_renderer_matches_golden_snippet});
  tests.push_back({"diagnose_query_failure_maps_parse_error",
                   test_diagnose_query_failure_maps_parse_error});
  tests.push_back({"version_string_contains_provenance",
                   test_version_string_contains_provenance});
}
