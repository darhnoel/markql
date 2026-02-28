#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace xsql {

/// Classifies diagnostic urgency for linting and execution error rendering.
/// MUST remain stable for text/JSON outputs and tests.
enum class DiagnosticSeverity {
  Error,
  Warning,
  Note,
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
  std::string message;
  std::string help;
  std::string doc_ref;
  DiagnosticSpan span;
  std::string snippet;
  std::vector<DiagnosticRelated> related;
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

/// Renders diagnostics in a human-readable multi-block text format.
/// MUST be deterministic for stable golden tests.
std::string render_diagnostics_text(const std::vector<Diagnostic>& diagnostics);
/// Renders diagnostics as a stable JSON array for machine consumption.
/// MUST keep key ordering stable across runs.
std::string render_diagnostics_json(const std::vector<Diagnostic>& diagnostics);
/// Returns true when at least one ERROR severity diagnostic exists.
bool has_error_diagnostics(const std::vector<Diagnostic>& diagnostics);

/// Parses and validates only (no execution) and returns diagnostics.
/// MUST return empty list for valid queries.
std::vector<Diagnostic> lint_query(const std::string& query);
/// Maps a caught execution error to structured diagnostics for consistent rendering.
/// MUST avoid throwing and return at least one diagnostic for non-empty messages.
std::vector<Diagnostic> diagnose_query_failure(const std::string& query,
                                               const std::string& error_message);

}  // namespace xsql

