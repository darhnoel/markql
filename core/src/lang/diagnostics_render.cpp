#include "diagnostics_internal.h"

#include <sstream>

namespace markql::diagnostics_internal {

constexpr const char* kAnsiReset = "\033[0m";
constexpr const char* kAnsiRed = "\033[31m";
constexpr const char* kAnsiYellow = "\033[33m";
constexpr const char* kAnsiBlue = "\033[34m";
constexpr const char* kAnsiCyan = "\033[36m";

const char* severity_ansi_color(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Error:
      return kAnsiRed;
    case DiagnosticSeverity::Warning:
      return kAnsiYellow;
    case DiagnosticSeverity::Note:
      return kAnsiBlue;
  }
  return kAnsiRed;
}

std::string style_token(const std::string& token, const char* color,
                        const DiagnosticTextRenderOptions& options) {
  if (!options.use_color) return token;
  return std::string(color) + token + kAnsiReset;
}

std::string severity_name(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::Error:
      return "ERROR";
    case DiagnosticSeverity::Warning:
      return "WARNING";
    case DiagnosticSeverity::Note:
      return "NOTE";
  }
  return "ERROR";
}

std::string coverage_name(LintCoverageLevel coverage) {
  switch (coverage) {
    case LintCoverageLevel::ParseOnly:
      return "parse_only";
    case LintCoverageLevel::Full:
      return "full";
    case LintCoverageLevel::Reduced:
      return "reduced";
    case LintCoverageLevel::Mixed:
      return "mixed";
  }
  return "parse_only";
}

std::string lint_status_name(const LintSummary& summary) {
  if (!summary.parse_succeeded) return "parse_failed";
  if (summary.error_count > 0) return "errors";
  if (summary.warning_count > 0) return "warnings";
  return "clean";
}

std::string lint_summary_message(const LintSummary& summary) {
  if (!summary.parse_succeeded) {
    return "Query parsing failed before full lint validation could run.";
  }
  switch (summary.coverage) {
    case LintCoverageLevel::Full:
      return "Query parsed successfully and completed full lint validation.";
    case LintCoverageLevel::Reduced:
      return "Query parsed successfully and completed reduced lint validation.";
    case LintCoverageLevel::Mixed:
      return "Query script parsed with mixed lint coverage across statements.";
    case LintCoverageLevel::ParseOnly:
      return "Query parsed successfully, but only parse-level coverage was available.";
  }
  return "Query parsing completed.";
}

std::string lint_coverage_note(const LintSummary& summary) {
  if (summary.used_reduced_validation || summary.relation_style_query) {
    return "Relation-style queries (WITH, JOIN, CTE, or derived sources) currently use a reduced "
           "lint validation path.";
  }
  if (summary.coverage == LintCoverageLevel::ParseOnly) {
    return "No semantic validation ran after parsing.";
  }
  if (summary.coverage == LintCoverageLevel::Mixed) {
    return "Some statements were fully validated while others only received reduced or parse-level "
           "coverage.";
  }
  return "";
}

std::string json_escape(std::string_view s) {
  std::ostringstream out;
  for (char c : s) {
    switch (c) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << c;
        break;
    }
  }
  return out.str();
}

std::string render_code_frame(const std::string& query, const DiagnosticSpan& span,
                              const std::string& label) {
  if (query.empty()) return "";
  size_t line_start = 0;
  size_t current_line = 1;
  while (current_line < span.start_line && line_start < query.size()) {
    size_t nl = query.find('\n', line_start);
    if (nl == std::string::npos) break;
    line_start = nl + 1;
    ++current_line;
  }
  size_t line_end = query.find('\n', line_start);
  if (line_end == std::string::npos) line_end = query.size();
  std::string line_text = query.substr(line_start, line_end - line_start);
  if (!line_text.empty() && line_text.back() == '\r') line_text.pop_back();

  const size_t caret_start = span.start_col > 0 ? span.start_col - 1 : 0;
  size_t caret_width = 1;
  if (span.start_line == span.end_line && span.end_col > span.start_col) {
    caret_width = span.end_col - span.start_col;
  }
  if (caret_start > line_text.size()) {
    return "";
  }
  if (caret_start + caret_width > line_text.size() + 1) {
    caret_width =
        std::max<size_t>(1, line_text.size() > caret_start ? line_text.size() - caret_start : 1);
  }
  const size_t line_digits = std::to_string(span.start_line).size();

  std::ostringstream out;
  out << " --> line " << span.start_line << ", col " << span.start_col << "\n";
  out << std::string(line_digits, ' ') << " |\n";
  out << span.start_line << " | " << line_text << "\n";
  out << std::string(line_digits, ' ') << " | " << std::string(caret_start, ' ')
      << std::string(caret_width, '^');
  if (!label.empty()) out << " " << label;
  return out.str();
}

}  // namespace markql::diagnostics_internal

namespace markql {

using namespace diagnostics_internal;

std::string render_diagnostics_text(const std::vector<Diagnostic>& diagnostics) {
  return render_diagnostics_text(diagnostics, DiagnosticTextRenderOptions{});
}

std::string render_diagnostics_text(const std::vector<Diagnostic>& diagnostics,
                                    const DiagnosticTextRenderOptions& options) {
  std::ostringstream out;
  for (size_t i = 0; i < diagnostics.size(); ++i) {
    const auto& d = diagnostics[i];
    out << style_token(severity_name(d.severity), severity_ansi_color(d.severity), options) << "["
        << d.code << "]: " << d.message << "\n";
    if (!d.snippet.empty()) out << d.snippet << "\n";
    if (!d.category.empty()) {
      out << style_token("category:", kAnsiBlue, options) << " " << d.category << "\n";
    }
    if (!d.encountered.empty()) {
      out << style_token("encountered:", kAnsiBlue, options) << " " << d.encountered << "\n";
    }
    if (!d.expected.empty()) {
      out << style_token("expected:", kAnsiBlue, options) << " " << d.expected << "\n";
    }
    for (const auto& related : d.related) {
      out << style_token("note:", kAnsiBlue, options) << " " << related.message << " (line "
          << related.span.start_line << ", col " << related.span.start_col << ")\n";
    }
    if (!d.why.empty()) {
      out << style_token("why:", kAnsiBlue, options) << " " << d.why << "\n";
    }
    out << style_token("help:", kAnsiCyan, options) << " " << d.help << "\n";
    if (!d.example.empty()) {
      out << style_token("example:", kAnsiBlue, options) << " " << d.example << "\n";
    }
    if (i + 1 < diagnostics.size()) out << "\n\n";
  }
  return out.str();
}

std::string render_diagnostics_json(const std::vector<Diagnostic>& diagnostics) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < diagnostics.size(); ++i) {
    const auto& d = diagnostics[i];
    if (i != 0) out << ",";
    out << "{";
    out << "\"severity\":\"" << json_escape(severity_name(d.severity)) << "\",";
    out << "\"code\":\"" << json_escape(d.code) << "\",";
    out << "\"message\":\"" << json_escape(d.message) << "\",";
    out << "\"help\":\"" << json_escape(d.help) << "\",";
    out << "\"doc_ref\":\"" << json_escape(d.doc_ref) << "\",";
    out << "\"span\":{" << "\"start_line\":" << d.span.start_line << ","
        << "\"start_col\":" << d.span.start_col << "," << "\"end_line\":" << d.span.end_line << ","
        << "\"end_col\":" << d.span.end_col << "," << "\"byte_start\":" << d.span.byte_start << ","
        << "\"byte_end\":" << d.span.byte_end << "},";
    out << "\"snippet\":\"" << json_escape(d.snippet) << "\",";
    out << "\"related\":[";
    for (size_t j = 0; j < d.related.size(); ++j) {
      if (j != 0) out << ",";
      const auto& related = d.related[j];
      out << "{";
      out << "\"message\":\"" << json_escape(related.message) << "\",";
      out << "\"span\":{" << "\"start_line\":" << related.span.start_line << ","
          << "\"start_col\":" << related.span.start_col << ","
          << "\"end_line\":" << related.span.end_line << ","
          << "\"end_col\":" << related.span.end_col << ","
          << "\"byte_start\":" << related.span.byte_start << ","
          << "\"byte_end\":" << related.span.byte_end << "}";
      out << "}";
    }
    out << "],";
    out << "\"category\":\"" << json_escape(d.category) << "\",";
    out << "\"why\":\"" << json_escape(d.why) << "\",";
    out << "\"example\":\"" << json_escape(d.example) << "\",";
    out << "\"expected\":\"" << json_escape(d.expected) << "\",";
    out << "\"encountered\":\"" << json_escape(d.encountered) << "\"";
    out << "}";
  }
  out << "]";
  return out.str();
}

std::string render_lint_result_text(const LintResult& result) {
  return render_lint_result_text(result, DiagnosticTextRenderOptions{});
}

std::string render_lint_result_text(const LintResult& result,
                                    const DiagnosticTextRenderOptions& options) {
  std::ostringstream out;
  out << lint_summary_message(result.summary) << "\n";
  out << "Validation coverage: " << coverage_name(result.summary.coverage) << "\n";
  const std::string coverage_note = lint_coverage_note(result.summary);
  if (!coverage_note.empty()) {
    out << "Coverage note: " << coverage_note << "\n";
  }
  out << "Result: " << result.summary.error_count << " error(s), " << result.summary.warning_count
      << " warning(s), " << result.summary.note_count << " note(s)";
  if (result.summary.error_count == 0 && result.summary.warning_count == 0 &&
      result.summary.note_count == 0) {
    out << " (no proven diagnostics)";
  }
  if (!result.diagnostics.empty()) {
    out << "\n\n" << render_diagnostics_text(result.diagnostics, options);
  }
  return out.str();
}

std::string render_lint_result_json(const LintResult& result) {
  std::ostringstream out;
  out << "{";
  out << "\"summary\":{";
  out << "\"parse_succeeded\":" << (result.summary.parse_succeeded ? "true" : "false") << ",";
  out << "\"coverage\":\"" << json_escape(coverage_name(result.summary.coverage)) << "\",";
  out << "\"relation_style_query\":" << (result.summary.relation_style_query ? "true" : "false")
      << ",";
  out << "\"used_reduced_validation\":"
      << (result.summary.used_reduced_validation ? "true" : "false") << ",";
  out << "\"status\":\"" << json_escape(lint_status_name(result.summary)) << "\",";
  out << "\"message\":\"" << json_escape(lint_summary_message(result.summary)) << "\",";
  out << "\"coverage_note\":\"" << json_escape(lint_coverage_note(result.summary)) << "\",";
  out << "\"error_count\":" << result.summary.error_count << ",";
  out << "\"warning_count\":" << result.summary.warning_count << ",";
  out << "\"note_count\":" << result.summary.note_count;
  out << "},";
  out << "\"diagnostics\":" << render_diagnostics_json(result.diagnostics);
  out << "}";
  return out.str();
}

bool has_error_diagnostics(const std::vector<Diagnostic>& diagnostics) {
  for (const auto& d : diagnostics) {
    if (d.severity == DiagnosticSeverity::Error) return true;
  }
  return false;
}

}  // namespace markql
