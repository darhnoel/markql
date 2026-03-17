#include "test_harness.h"

#include <vector>

#include "markql/diagnostics.h"
#include "markql/version.h"

namespace {

void test_lint_syntax_diagnostic_has_stable_code_and_span() {
  std::vector<markql::Diagnostic> diagnostics = markql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "syntax diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.severity == markql::DiagnosticSeverity::Error, "syntax severity is error");
  expect_true(first.code == "MQL-SYN-0001", "syntax code is stable");
  expect_true(first.category == "parse", "syntax category is parse");
  expect_true(first.message == "Missing projection after SELECT", "syntax message is upgraded");
  expect_true(first.expected == "projection after SELECT", "syntax expected text");
  expect_true(first.encountered == "FROM", "syntax encountered token");
  expect_true(first.span.start_line == 1, "syntax span line");
  expect_true(first.span.start_col > 1, "syntax span col");
}

void test_lint_semantic_diagnostic_has_stable_code() {
  std::vector<markql::Diagnostic> diagnostics = markql::lint_query(
      "SELECT TEXT(n) "
      "FROM doc AS n "
      "WHERE n.tag = 'div'");
  expect_true(!diagnostics.empty(), "semantic diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.code == "MQL-SEM-0301", "semantic code is stable");
  expect_true(first.category == "structural_rule", "semantic category is structural");
  expect_true(first.message.find("TEXT()/INNER_HTML()/RAW_INNER_HTML()") != std::string::npos,
              "semantic message text");
  expect_true(!first.help.empty(), "semantic help is present");
  expect_true(!first.why.empty(), "semantic why is present");
  expect_true(!first.example.empty(), "semantic example is present");
  expect_true(!first.doc_ref.empty(), "semantic doc ref is present");
}

void test_lint_warns_select_from_alias_as_ambiguous_node_value() {
  std::vector<markql::Diagnostic> diagnostics = markql::lint_query(
      "SELECT node_row FROM doc AS node_row WHERE node_row.tag = 'div'");
  expect_true(!diagnostics.empty(), "ambiguous alias-select warning produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.severity == markql::DiagnosticSeverity::Warning, "warning severity is stable");
  expect_true(first.code == "MQL-LINT-0001", "warning code is stable");
  expect_true(first.category == "style_warning", "warning category is stable");
  expect_true(first.message == "Selecting the FROM alias as a value is ambiguous",
              "warning message is stable");
  expect_true(first.help == "Use SELECT self to return the current node",
              "warning help is stable");
  expect_true(first.doc_ref.find("appendix-grammar.md#select-self-for-current-row-nodes") !=
                  std::string::npos,
              "warning doc_ref points to canonical self docs");
}

void test_lint_select_self_has_no_alias_ambiguity_warning() {
  std::vector<markql::Diagnostic> diagnostics = markql::lint_query(
      "SELECT self FROM doc AS node_row WHERE node_row.tag = 'div'");
  expect_true(diagnostics.empty(), "canonical select self has no ambiguity warning");
}

void test_lint_detailed_reports_full_coverage_for_simple_query() {
  markql::LintResult result = markql::lint_query_detailed("SELECT div FROM doc");
  expect_true(result.summary.parse_succeeded, "simple query parses");
  expect_true(result.summary.coverage == markql::LintCoverageLevel::Full,
              "simple query gets full coverage");
  expect_true(result.summary.error_count == 0, "simple query has no errors");
  expect_true(result.summary.warning_count == 0, "simple query has no warnings");
  const std::string rendered = markql::render_lint_result_text(result);
  expect_true(rendered.find("Validation coverage: full") != std::string::npos,
              "full coverage summary is rendered");
  expect_true(rendered.find("no proven diagnostics") != std::string::npos,
              "clean summary avoids overclaiming correctness");
}

void test_lint_detailed_reports_reduced_coverage_for_relation_query() {
  markql::LintResult result = markql::lint_query_detailed(
      "WITH r AS (SELECT n.node_id AS row_id FROM doc AS n WHERE n.tag = 'tr') "
      "SELECT x.row_id FROM r AS x");
  expect_true(result.summary.parse_succeeded, "relation query parses");
  expect_true(result.summary.coverage == markql::LintCoverageLevel::Reduced,
              "relation query gets reduced coverage");
  expect_true(result.summary.relation_style_query, "relation query flag is set");
  expect_true(result.summary.used_reduced_validation, "reduced validation flag is set");
  const std::string json = markql::render_lint_result_json(result);
  expect_true(json.find("\"coverage\":\"reduced\"") != std::string::npos,
              "reduced coverage appears in json");
  expect_true(json.find("\"used_reduced_validation\":true") != std::string::npos,
              "json reports reduced validation");
}

void test_lint_warns_on_suspicious_member_access_in_relation_query() {
  markql::LintResult result = markql::lint_query_detailed(
      "WITH r AS (SELECT n.node_id AS row_id FROM doc AS n WHERE n.tag = 'tr') "
      "SELECT x.row_id FROM r AS x WHERE x.tagy = 'tr'");
  expect_true(result.summary.warning_count > 0, "suspicion warning is counted");
  expect_true(!result.diagnostics.empty(), "suspicion diagnostic emitted");
  if (result.diagnostics.empty()) return;
  const auto& first = result.diagnostics.front();
  expect_true(first.code == "MQL-LINT-0002", "suspicion warning code is stable");
  expect_true(first.category == "suspicion_warning", "suspicion category is stable");
  expect_true(first.expected == "tag", "warning suggests built-in replacement");
  expect_true(first.encountered == "tagy", "warning records encountered member");
}

void test_lint_warns_on_unknown_qualifier_in_relation_query() {
  markql::LintResult result = markql::lint_query_detailed(
      "WITH r AS (SELECT n.node_id AS row_id FROM doc AS n WHERE n.tag = 'tr') "
      "SELECT x.row_id FROM r AS x WHERE doc.tag = 'tr'");
  expect_true(!result.diagnostics.empty(), "binding warning emitted");
  if (result.diagnostics.empty()) return;
  const auto& first = result.diagnostics.front();
  expect_true(first.code == "MQL-LINT-0003", "binding warning code is stable");
  expect_true(first.category == "binding_warning", "binding warning category is stable");
  expect_true(first.encountered == "doc", "binding warning shows qualifier");
}

void test_diagnostic_text_renderer_contains_help_and_caret() {
  std::vector<markql::Diagnostic> diagnostics = markql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "diagnostics available");
  if (diagnostics.empty()) return;
  std::string rendered = markql::render_diagnostics_text(diagnostics);
  expect_true(rendered.find("help:") != std::string::npos, "text renderer has help");
  expect_true(rendered.find("^") != std::string::npos, "text renderer has caret snippet");
}

void test_diagnostic_json_renderer_contains_stable_fields() {
  std::vector<markql::Diagnostic> diagnostics = markql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "diagnostics available for json");
  if (diagnostics.empty()) return;
  std::string json = markql::render_diagnostics_json(diagnostics);
  expect_true(json.find("\"severity\":\"ERROR\"") != std::string::npos, "json severity");
  expect_true(json.find("\"code\":\"MQL-SYN-0001\"") != std::string::npos, "json code");
  expect_true(json.find("\"span\"") != std::string::npos, "json span");
  expect_true(json.find("\"snippet\"") != std::string::npos, "json snippet");
  expect_true(json.find("\"category\":\"parse\"") != std::string::npos, "json category");
  expect_true(json.find("\"expected\":\"projection after SELECT\"") != std::string::npos,
              "json expected");
  expect_true(json.find("\"encountered\":\"FROM\"") != std::string::npos, "json encountered");
}

void test_diagnostic_json_renderer_matches_snapshot() {
  std::vector<markql::Diagnostic> diagnostics = markql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "diagnostics available for json snapshot");
  if (diagnostics.empty()) return;
  std::string json = markql::render_diagnostics_json(diagnostics);
  const std::string expected =
      "[{\"severity\":\"ERROR\",\"code\":\"MQL-SYN-0001\",\"message\":\"Missing projection after SELECT\","
      "\"help\":\"Add a tag, self, *, or a projection expression after SELECT.\","
      "\"doc_ref\":\"docs/book/appendix-grammar.md\","
      "\"span\":{\"start_line\":1,\"start_col\":8,\"end_line\":1,\"end_col\":9,\"byte_start\":7,\"byte_end\":8},"
      "\"snippet\":\" --> line 1, col 8\\n  |\\n1 | SELECT FROM doc\\n  |        ^\","
      "\"related\":[],"
      "\"category\":\"parse\","
      "\"why\":\"SELECT must contain at least one projection before FROM in MarkQL.\","
      "\"example\":\"SELECT div FROM doc\","
      "\"expected\":\"projection after SELECT\","
      "\"encountered\":\"FROM\"}]";
  expect_true(json == expected, "json snapshot stable");
}

void test_diagnostic_text_renderer_matches_golden_snippet() {
  std::vector<markql::Diagnostic> diagnostics = markql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "diagnostics available for golden text");
  if (diagnostics.empty()) return;
  std::string rendered = markql::render_diagnostics_text(diagnostics);
  const std::string expected =
      "ERROR[MQL-SYN-0001]: Missing projection after SELECT\n"
      " --> line 1, col 8\n"
      "  |\n"
      "1 | SELECT FROM doc\n"
      "  |        ^\n"
      "category: parse\n"
      "encountered: FROM\n"
      "expected: projection after SELECT\n"
      "why: SELECT must contain at least one projection before FROM in MarkQL.\n"
      "help: Add a tag, self, *, or a projection expression after SELECT.\n"
      "example: SELECT div FROM doc\n";
  expect_true(rendered == expected, "golden diagnostic text");
}

void test_diagnostic_text_renderer_color_includes_ansi_tokens() {
  std::vector<markql::Diagnostic> diagnostics = markql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "diagnostics available for colorized text");
  if (diagnostics.empty()) return;
  markql::DiagnosticTextRenderOptions options;
  options.use_color = true;
  std::string rendered = markql::render_diagnostics_text(diagnostics, options);
  expect_true(rendered.find("\033[31mERROR\033[0m") != std::string::npos,
              "colorized text renders severity with ansi style");
  expect_true(rendered.find("\033[36mhelp:\033[0m") != std::string::npos,
              "colorized text renders help label with ansi style");
}

void test_diagnostic_json_renderer_never_contains_ansi_sequences() {
  std::vector<markql::Diagnostic> diagnostics = markql::lint_query("SELECT FROM doc");
  expect_true(!diagnostics.empty(), "diagnostics available for json ansi guard");
  if (diagnostics.empty()) return;
  std::string json = markql::render_diagnostics_json(diagnostics);
  expect_true(json.find("\033[") == std::string::npos, "json diagnostics remain ansi-free");
}

void test_diagnose_query_failure_maps_parse_error() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::diagnose_query_failure("SELECT FROM doc", "Query parse error: Expected tag identifier");
  expect_true(!diagnostics.empty(), "mapped parse failure diagnostics");
  if (diagnostics.empty()) return;
  expect_true(diagnostics.front().code == "MQL-SYN-0001", "mapped parse code");
  expect_true(diagnostics.front().message == "Missing projection after SELECT",
              "mapped parse message is upgraded");
}

void test_lint_invalid_clause_order_has_specific_diagnostic() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::lint_query("SELECT div WHERE tag = 'div' FROM doc");
  expect_true(!diagnostics.empty(), "clause-order diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.code == "MQL-SYN-0005", "clause-order code is stable");
  expect_true(first.message == "Invalid clause order before FROM",
              "clause-order message is specific");
  expect_true(first.expected == "FROM", "clause-order expected token");
  expect_true(first.encountered == "WHERE", "clause-order encountered token");
}

void test_lint_operator_typo_prefers_local_repair_over_clause_order_help() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::lint_query("SELECT div FROM doc WHERE tag LKE 'div'");
  expect_true(!diagnostics.empty(), "operator typo diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.message == "Expected a comparison operator",
              "operator typo message is local");
  expect_true(first.expected == "comparison operator", "operator typo expected text is local");
  expect_true(first.encountered == "LKE", "operator typo encountered token is preserved");
  expect_true(first.help.find("LIKE") != std::string::npos, "operator typo suggests LIKE");
  expect_true(first.help.find("clause order") == std::string::npos,
              "operator typo help avoids clause-order fallback");
  expect_true(first.example.find("WHERE tag LIKE") != std::string::npos,
              "operator typo example stays local to comparison syntax");
}

void test_lint_position_keyword_typo_prefers_local_repair() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::lint_query("SELECT div FROM doc WHERE POSITION('a' ON text) > 0");
  expect_true(!diagnostics.empty(), "position typo diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.message == "POSITION(...) requires the keyword IN",
              "position typo message is local");
  expect_true(first.expected == "IN", "position typo expected token is local");
  expect_true(first.encountered == "ON", "position typo encountered token is preserved");
  expect_true(first.help.find("IN") != std::string::npos, "position typo suggests IN");
  expect_true(first.example.find("POSITION('a' IN text)") != std::string::npos,
              "position typo example stays local to POSITION syntax");
}

void test_lint_case_keyword_typo_prefers_local_repair() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::lint_query("SELECT CASE WHEN tag = 'div' THN 'yes' ELSE 'no' END AS flag FROM doc");
  expect_true(!diagnostics.empty(), "case typo diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.message == "CASE expression is missing THEN",
              "case typo message is local");
  expect_true(first.expected == "THEN", "case typo expected token is local");
  expect_true(first.encountered == "THN", "case typo encountered token is preserved");
  expect_true(first.help.find("THEN") != std::string::npos, "case typo suggests THEN");
  expect_true(first.example.find("CASE WHEN") != std::string::npos,
              "case typo example stays local to CASE syntax");
}

void test_lint_exists_axis_typo_prefers_local_repair() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::lint_query("SELECT div FROM doc WHERE EXISTS(dscendant WHERE tag = 'a')");
  expect_true(!diagnostics.empty(), "exists axis typo diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.message == "Malformed EXISTS(...) predicate",
              "exists axis typo message is local");
  expect_true(first.expected == "axis name inside EXISTS(...)", "exists axis expected text is local");
  expect_true(first.encountered == "DSCENDANT", "exists axis encountered token is preserved");
  expect_true(first.help.find("descendant") != std::string::npos,
              "exists axis typo suggests descendant");
  expect_true(first.help.find("clause order") == std::string::npos,
              "exists axis typo avoids top-level fallback guidance");
}

void test_lint_to_target_typo_prefers_local_repair() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::lint_query("SELECT div FROM doc TO JSNO()");
  expect_true(!diagnostics.empty(), "to-target typo diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.message == "TO requires an output target",
              "to-target message is local");
  expect_true(first.help.find("JSON") != std::string::npos, "to-target typo suggests JSON");
  expect_true(first.example.find("TO LIST()") != std::string::npos,
              "to-target example stays local to TO syntax");
}

void test_lint_show_keyword_typo_prefers_local_repair() {
  std::vector<markql::Diagnostic> diagnostics = markql::lint_query("SHOW FUNCTONS");
  expect_true(!diagnostics.empty(), "show typo diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.message == "SHOW requires a supported metadata category",
              "show typo message is local");
  expect_true(first.help.find("FUNCTIONS") != std::string::npos,
              "show typo suggests FUNCTIONS");
  expect_true(first.example == "SHOW FUNCTIONS", "show typo example is local");
}

void test_lint_table_option_value_typo_prefers_local_repair() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::lint_query("SELECT table FROM doc TO TABLE(HEADER OF)");
  expect_true(!diagnostics.empty(), "table option typo diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.message == "Invalid value for TABLE() option HEADER",
              "table option message is local");
  expect_true(first.help.find("OFF") != std::string::npos, "table option typo suggests OFF");
  expect_true(first.example.find("TO TABLE(HEADER ON)") != std::string::npos,
              "table option example stays local to TABLE option syntax");
}

void test_lint_exists_shape_has_specific_guidance() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::lint_query("SELECT div FROM doc WHERE EXISTS(WHERE tag = 'a')");
  expect_true(!diagnostics.empty(), "exists diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.message == "Malformed EXISTS(...) predicate",
              "exists message is specific");
  expect_true(first.expected == "axis name inside EXISTS(...)", "exists expected text");
  expect_true(first.help.find("EXISTS(child)") != std::string::npos, "exists help example");
}

void test_lint_project_shape_has_specific_guidance() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::lint_query("SELECT PROJECT(li) FROM doc");
  expect_true(!diagnostics.empty(), "project diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.message == "Invalid PROJECT()/FLATTEN_EXTRACT() usage",
              "project message is specific");
  expect_true(first.help == "Use PROJECT(li) AS (title: TEXT(h2))",
              "project help is specific");
}

void test_lint_node_function_rejects_scalar_argument_with_specific_guidance() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::lint_query("SELECT TEXT('oops') FROM doc WHERE tag = 'div'");
  expect_true(!diagnostics.empty(), "node-function diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.message == "This function requires a node/tag reference",
              "node-function message is specific");
  expect_true(first.expected == "tag name, alias, or self", "node-function expected text");
}

void test_lint_malformed_with_clause_has_specific_guidance() {
  std::vector<markql::Diagnostic> diagnostics =
      markql::lint_query("WITH AS (SELECT div FROM doc) SELECT div FROM doc");
  expect_true(!diagnostics.empty(), "with diagnostics produced");
  if (diagnostics.empty()) return;
  const auto& first = diagnostics.front();
  expect_true(first.message == "Malformed WITH clause", "with message is specific");
  expect_true(first.help.find("WITH rows AS") != std::string::npos, "with help example");
}

void test_version_string_contains_provenance() {
  markql::VersionInfo info = markql::get_version_info();
  expect_true(!info.version.empty(), "version field available");
  expect_true(!info.git_commit.empty(), "git commit field available");
  std::string rendered = markql::version_string();
  expect_true(rendered.find(info.version) != std::string::npos, "rendered includes version");
  expect_true(rendered.find(info.git_commit) != std::string::npos, "rendered includes commit");
}

}  // namespace

void register_diagnostic_tests(std::vector<TestCase>& tests) {
  tests.push_back({"lint_syntax_diagnostic_has_stable_code_and_span",
                   test_lint_syntax_diagnostic_has_stable_code_and_span});
  tests.push_back({"lint_semantic_diagnostic_has_stable_code",
                   test_lint_semantic_diagnostic_has_stable_code});
  tests.push_back({"lint_warns_select_from_alias_as_ambiguous_node_value",
                   test_lint_warns_select_from_alias_as_ambiguous_node_value});
  tests.push_back({"lint_select_self_has_no_alias_ambiguity_warning",
                   test_lint_select_self_has_no_alias_ambiguity_warning});
  tests.push_back({"lint_detailed_reports_full_coverage_for_simple_query",
                   test_lint_detailed_reports_full_coverage_for_simple_query});
  tests.push_back({"lint_detailed_reports_reduced_coverage_for_relation_query",
                   test_lint_detailed_reports_reduced_coverage_for_relation_query});
  tests.push_back({"lint_warns_on_suspicious_member_access_in_relation_query",
                   test_lint_warns_on_suspicious_member_access_in_relation_query});
  tests.push_back({"lint_warns_on_unknown_qualifier_in_relation_query",
                   test_lint_warns_on_unknown_qualifier_in_relation_query});
  tests.push_back({"diagnostic_text_renderer_contains_help_and_caret",
                   test_diagnostic_text_renderer_contains_help_and_caret});
  tests.push_back({"diagnostic_json_renderer_contains_stable_fields",
                   test_diagnostic_json_renderer_contains_stable_fields});
  tests.push_back({"diagnostic_json_renderer_matches_snapshot",
                   test_diagnostic_json_renderer_matches_snapshot});
  tests.push_back({"diagnostic_text_renderer_matches_golden_snippet",
                   test_diagnostic_text_renderer_matches_golden_snippet});
  tests.push_back({"diagnostic_text_renderer_color_includes_ansi_tokens",
                   test_diagnostic_text_renderer_color_includes_ansi_tokens});
  tests.push_back({"diagnostic_json_renderer_never_contains_ansi_sequences",
                   test_diagnostic_json_renderer_never_contains_ansi_sequences});
  tests.push_back({"diagnose_query_failure_maps_parse_error",
                   test_diagnose_query_failure_maps_parse_error});
  tests.push_back({"lint_invalid_clause_order_has_specific_diagnostic",
                   test_lint_invalid_clause_order_has_specific_diagnostic});
  tests.push_back({"lint_operator_typo_prefers_local_repair_over_clause_order_help",
                   test_lint_operator_typo_prefers_local_repair_over_clause_order_help});
  tests.push_back({"lint_position_keyword_typo_prefers_local_repair",
                   test_lint_position_keyword_typo_prefers_local_repair});
  tests.push_back({"lint_case_keyword_typo_prefers_local_repair",
                   test_lint_case_keyword_typo_prefers_local_repair});
  tests.push_back({"lint_exists_axis_typo_prefers_local_repair",
                   test_lint_exists_axis_typo_prefers_local_repair});
  tests.push_back({"lint_to_target_typo_prefers_local_repair",
                   test_lint_to_target_typo_prefers_local_repair});
  tests.push_back({"lint_show_keyword_typo_prefers_local_repair",
                   test_lint_show_keyword_typo_prefers_local_repair});
  tests.push_back({"lint_table_option_value_typo_prefers_local_repair",
                   test_lint_table_option_value_typo_prefers_local_repair});
  tests.push_back({"lint_exists_shape_has_specific_guidance",
                   test_lint_exists_shape_has_specific_guidance});
  tests.push_back({"lint_project_shape_has_specific_guidance",
                   test_lint_project_shape_has_specific_guidance});
  tests.push_back({"lint_node_function_rejects_scalar_argument_with_specific_guidance",
                   test_lint_node_function_rejects_scalar_argument_with_specific_guidance});
  tests.push_back({"lint_malformed_with_clause_has_specific_guidance",
                   test_lint_malformed_with_clause_has_specific_guidance});
  tests.push_back({"version_string_contains_provenance",
                   test_version_string_contains_provenance});
}
