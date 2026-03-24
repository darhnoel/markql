#include "diagnostics_internal.h"

#include "markql_parser.h"

namespace markql {

using namespace diagnostics_internal;

Diagnostic make_syntax_diagnostic(const std::string& query, const std::string& parser_message,
                                  size_t error_byte) {
  Diagnostic d;
  d.severity = DiagnosticSeverity::Error;
  d.span = span_from_bytes(query, error_byte, error_byte + 1);
  set_syntax_details(d, query, parser_message, error_byte);
  d.snippet = render_code_frame(query, d.span, "");

  if (contains_icase(parser_message, "Expected END") && contains_icase(query, "CASE")) {
    const size_t case_pos = find_icase(query, "CASE");
    if (case_pos != std::string::npos) {
      DiagnosticRelated related;
      related.message = "CASE started here";
      related.span = span_from_bytes(query, case_pos, case_pos + 4);
      d.related.push_back(std::move(related));
    }
  }
  return d;
}

Diagnostic make_semantic_diagnostic(const std::string& query,
                                    const std::string& validation_message) {
  Diagnostic d;
  d.severity = DiagnosticSeverity::Error;
  d.message = validation_message;
  d.span = best_effort_semantic_span(query, validation_message);
  set_semantic_details(d);
  d.snippet = render_code_frame(query, d.span, "");
  return d;
}

Diagnostic make_runtime_diagnostic(const std::string& query, const std::string& runtime_message) {
  Diagnostic d;
  d.severity = DiagnosticSeverity::Error;
  d.category = "runtime";
  d.message = runtime_message;
  d.why = "The query validated, but execution failed while loading or evaluating data.";
  d.span = best_effort_semantic_span(query, runtime_message);
  d.code = "MQL-RUN-0001";
  d.help = "Check source availability and query source clauses before retrying.";
  d.example = "markql --query \"SELECT div FROM doc\" --input ./page.html";
  d.doc_ref = kSourcesDoc;
  if (looks_like_runtime_io(runtime_message)) {
    d.category = "runtime_io";
    d.code = "MQL-RUN-0002";
    d.why = "The failure came from file or network access rather than MarkQL syntax.";
    d.help = "Verify the input path/URL and network/file permissions.";
  }
  d.snippet = render_code_frame(query, d.span, "");
  return d;
}

Diagnostic make_select_alias_ambiguity_warning(const std::string& query, size_t byte_start,
                                               size_t byte_end) {
  Diagnostic d;
  d.severity = DiagnosticSeverity::Warning;
  d.code = "MQL-LINT-0001";
  d.category = "style_warning";
  d.message = "Selecting the FROM alias as a value is ambiguous";
  d.why = "The alias names the current row source, but selecting it as a value is easy to misread.";
  d.help = "Use SELECT self to return the current node";
  d.example = "SELECT self FROM doc AS node_row WHERE node_row.tag = 'div'";
  d.doc_ref = kSelectSelfDoc;
  d.span = span_from_bytes(query, byte_start, byte_end);
  d.snippet = render_code_frame(query, d.span, "");
  return d;
}

std::vector<Diagnostic> diagnose_query_failure(const std::string& query,
                                               const std::string& error_message) {
  std::vector<Diagnostic> out;
  std::string message = error_message;
  const std::string parse_prefix = "Query parse error: ";
  if (message.rfind(parse_prefix, 0) == 0) {
    message = message.substr(parse_prefix.size());
  }

  ParseResult parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    const size_t pos = parsed.error.has_value() ? parsed.error->position : 0;
    const std::string parse_message = parsed.error.has_value() ? parsed.error->message : message;
    out.push_back(make_syntax_diagnostic(query, parse_message, pos));
    return out;
  }

  if (looks_like_runtime_io(message)) {
    out.push_back(make_runtime_diagnostic(query, message));
    return out;
  }

  out.push_back(make_semantic_diagnostic(query, message));
  return out;
}

}  // namespace markql
