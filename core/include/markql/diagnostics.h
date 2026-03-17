#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace markql {

/// Classifies diagnostic urgency for linting and execution error rendering.
/// MUST remain stable for text/JSON outputs and tests.
enum class DiagnosticSeverity {
  Error,
  Warning,
  Note,
};

/// Describes how much of the lint engine ran for a query.
/// MUST stay stable for CLI/JSON summaries and future editor tooling.
enum class LintCoverageLevel {
  ParseOnly,
  Full,
  Reduced,
  Mixed,
};

/// Describes a source span in both byte offsets and line/column coordinates.
/// MUST use 1-based line/column values; byte offsets are 0-based.
struct DiagnosticSpan {
  size_t start_line = 1;
  size_t start_col = 1;
  size_t end_line = 1;
  size_t end_col = 1;
  size_t byte_start = 0;
  size_t byte_end = 0;
};

/// Holds an additional location tied to the primary diagnostic.
/// MUST keep message concise and actionable.
struct DiagnosticRelated {
  std::string message;
  DiagnosticSpan span;
};

/// Structured query diagnostic for syntax and semantic validation.
/// MUST include stable codes, actionable help, and a docs pointer.
struct Diagnostic {
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  std::string code;
  std::string category;
  std::string message;
  std::string why;
  std::string help;
  std::string example;
  std::string expected;
  std::string encountered;
  std::string doc_ref;
  DiagnosticSpan span;
  std::string snippet;
  std::vector<DiagnosticRelated> related;
};

/// Controls human-readable diagnostics rendering behavior.
/// MUST default to plain deterministic text unless explicitly enabled.
struct DiagnosticTextRenderOptions {
  bool use_color = false;
};

/// Summary metadata for a lint run.
/// MUST describe the confidence level of the current lint result without overstating certainty.
struct LintSummary {
  bool parse_succeeded = false;
  LintCoverageLevel coverage = LintCoverageLevel::ParseOnly;
  bool relation_style_query = false;
  bool used_reduced_validation = false;
  size_t error_count = 0;
  size_t warning_count = 0;
  size_t note_count = 0;
};

/// Authoritative lint result with diagnostics plus coverage metadata.
/// MUST remain the single source of truth for CLI/Python lint reporting.
struct LintResult {
  std::vector<Diagnostic> diagnostics;
  LintSummary summary;
};

/// Builds a syntax diagnostic anchored at a byte position.
/// MUST return deterministic code/help/doc mappings for known parser errors.
Diagnostic make_syntax_diagnostic(const std::string& query,
                                  const std::string& parser_message,
                                  size_t error_byte);
/// Builds a semantic validation diagnostic from a validation error message.
/// MUST map known validation errors to stable codes with help and docs.
Diagnostic make_semantic_diagnostic(const std::string& query,
                                    const std::string& validation_message);
/// Builds a generic runtime diagnostic when an execution error is not a validation rule.
/// MUST still provide actionable guidance and docs reference.
Diagnostic make_runtime_diagnostic(const std::string& query,
                                   const std::string& runtime_message);
/// Builds a lint warning for ambiguous node selection via FROM alias sugar.
/// MUST keep code/message/help/doc_ref stable for migration tooling.
Diagnostic make_select_alias_ambiguity_warning(const std::string& query,
                                               size_t byte_start,
                                               size_t byte_end);

/// Renders diagnostics in a human-readable multi-block text format.
/// MUST be deterministic for stable golden tests.
std::string render_diagnostics_text(const std::vector<Diagnostic>& diagnostics);
/// Renders diagnostics text with optional styling.
/// MUST preserve plain-text layout when color is disabled.
std::string render_diagnostics_text(const std::vector<Diagnostic>& diagnostics,
                                    const DiagnosticTextRenderOptions& options);
/// Renders diagnostics as a stable JSON array for machine consumption.
/// MUST keep key ordering stable across runs.
std::string render_diagnostics_json(const std::vector<Diagnostic>& diagnostics);
/// Renders a coverage-aware lint result for human-readable CLI output.
/// MUST distinguish parse success from full or reduced validation coverage.
std::string render_lint_result_text(const LintResult& result);
/// Renders a coverage-aware lint result for human-readable CLI output with styling options.
/// MUST preserve plain-text layout when color is disabled.
std::string render_lint_result_text(const LintResult& result,
                                    const DiagnosticTextRenderOptions& options);
/// Renders a coverage-aware lint result for machine-readable CLI/editor output.
/// MUST keep key ordering stable and preserve the old diagnostics-array renderer separately.
std::string render_lint_result_json(const LintResult& result);
/// Returns true when at least one ERROR severity diagnostic exists.
bool has_error_diagnostics(const std::vector<Diagnostic>& diagnostics);

/// Parses and validates only (no execution) and returns diagnostics.
/// MUST return empty list for valid queries.
std::vector<Diagnostic> lint_query(const std::string& query);
/// Parses and validates only (no execution) and returns diagnostics plus coverage metadata.
/// MUST remain authoritative for CLI/Python lint reporting.
LintResult lint_query_detailed(const std::string& query);
/// Maps a caught execution error to structured diagnostics for consistent rendering.
/// MUST avoid throwing and return at least one diagnostic for non-empty messages.
std::vector<Diagnostic> diagnose_query_failure(const std::string& query,
                                               const std::string& error_message);

}  // namespace markql
