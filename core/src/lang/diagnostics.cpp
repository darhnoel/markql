#include "xsql/diagnostics.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>

#include "markql_parser.h"

namespace xsql {

namespace {

constexpr const char* kGrammarDoc = "docs/book/appendix-grammar.md";
constexpr const char* kFunctionsDoc = "docs/book/appendix-function-reference.md";
constexpr const char* kSourcesDoc = "docs/book/ch04-sources-and-loading.md";
constexpr const char* kCliDoc = "docs/markql-cli-guide.md";

std::string to_upper_ascii(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  return out;
}

std::string to_lower_ascii(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

bool contains_icase(std::string_view haystack, std::string_view needle) {
  return to_lower_ascii(haystack).find(to_lower_ascii(needle)) != std::string::npos;
}

size_t find_icase(std::string_view haystack, std::string_view needle) {
  return to_lower_ascii(haystack).find(to_lower_ascii(needle));
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::optional<std::string> extract_single_quoted(std::string_view message) {
  size_t start = message.find('\'');
  if (start == std::string::npos) return std::nullopt;
  size_t end = message.find('\'', start + 1);
  if (end == std::string::npos || end <= start + 1) return std::nullopt;
  return std::string(message.substr(start + 1, end - start - 1));
}

DiagnosticSpan span_from_bytes(const std::string& query, size_t byte_start, size_t byte_end) {
  DiagnosticSpan span;
  const size_t size = query.size();
  span.byte_start = std::min(byte_start, size);
  span.byte_end = std::min(std::max(byte_end, span.byte_start + (size == 0 ? 0u : 1u)), size);
  if (size == 0) {
    span.start_line = 1;
    span.start_col = 1;
    span.end_line = 1;
    span.end_col = 1;
    span.byte_start = 0;
    span.byte_end = 0;
    return span;
  }
  if (span.byte_start >= size) {
    span.byte_start = size - 1;
  }
  if (span.byte_end <= span.byte_start) {
    span.byte_end = std::min(size, span.byte_start + 1);
  }

  size_t line = 1;
  size_t col = 1;
  for (size_t i = 0; i < span.byte_start && i < size; ++i) {
    if (query[i] == '\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
  }
  span.start_line = line;
  span.start_col = col;

  for (size_t i = span.byte_start; i < span.byte_end && i < size; ++i) {
    if (query[i] == '\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
  }
  span.end_line = line;
  span.end_col = col;
  return span;
}

std::optional<DiagnosticSpan> find_keyword_span(const std::string& query, const std::string& keyword) {
  size_t pos = find_icase(query, keyword);
  if (pos == std::string::npos) return std::nullopt;
  return span_from_bytes(query, pos, pos + keyword.size());
}

std::optional<DiagnosticSpan> find_identifier_span(const std::string& query, const std::string& identifier) {
  if (identifier.empty()) return std::nullopt;
  const std::string lower_query = to_lower_ascii(query);
  const std::string lower_ident = to_lower_ascii(identifier);
  size_t pos = 0;
  while (true) {
    pos = lower_query.find(lower_ident, pos);
    if (pos == std::string::npos) return std::nullopt;
    const bool left_ok = (pos == 0) || !is_ident_char(query[pos - 1]);
    const size_t right = pos + lower_ident.size();
    const bool right_ok = right >= query.size() || !is_ident_char(query[right]);
    if (left_ok && right_ok) {
      return span_from_bytes(query, pos, right);
    }
    ++pos;
  }
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

std::string render_code_frame(const std::string& query,
                              const DiagnosticSpan& span,
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
    caret_width = std::max<size_t>(1, line_text.size() > caret_start ? line_text.size() - caret_start : 1);
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

DiagnosticSpan best_effort_semantic_span(const std::string& query,
                                         const std::string& message) {
  if (contains_icase(message, "ORDER BY")) {
    if (auto span = find_keyword_span(query, "ORDER BY"); span.has_value()) return *span;
  }
  if (contains_icase(message, "TO LIST")) {
    if (auto span = find_keyword_span(query, "TO LIST"); span.has_value()) return *span;
  }
  if (contains_icase(message, "TO TABLE")) {
    if (auto span = find_keyword_span(query, "TO TABLE"); span.has_value()) return *span;
  }
  if (contains_icase(message, "export")) {
    if (auto span = find_keyword_span(query, "TO CSV"); span.has_value()) return *span;
    if (auto span = find_keyword_span(query, "TO PARQUET"); span.has_value()) return *span;
    if (auto span = find_keyword_span(query, "TO JSON"); span.has_value()) return *span;
    if (auto span = find_keyword_span(query, "TO NDJSON"); span.has_value()) return *span;
  }
  if (contains_icase(message, "Duplicate source alias") ||
      contains_icase(message, "Identifier 'doc' is not bound") ||
      contains_icase(message, "Unknown identifier")) {
    if (auto token = extract_single_quoted(message); token.has_value()) {
      if (auto span = find_identifier_span(query, *token); span.has_value()) return *span;
    }
    if (auto span = find_keyword_span(query, "FROM"); span.has_value()) return *span;
  }
  if (contains_icase(message, "CTE")) {
    if (auto span = find_keyword_span(query, "WITH"); span.has_value()) return *span;
  }
  if (contains_icase(message, "JOIN")) {
    if (auto span = find_keyword_span(query, "JOIN"); span.has_value()) return *span;
  }
  if (contains_icase(message, "TEXT()") ||
      contains_icase(message, "INNER_HTML()") ||
      contains_icase(message, "RAW_INNER_HTML()")) {
    if (auto span = find_keyword_span(query, "SELECT"); span.has_value()) return *span;
  }
  if (contains_icase(message, "LIMIT")) {
    if (auto span = find_keyword_span(query, "LIMIT"); span.has_value()) return *span;
  }
  if (contains_icase(message, "EXCLUDE")) {
    if (auto span = find_keyword_span(query, "EXCLUDE"); span.has_value()) return *span;
  }
  if (contains_icase(message, "Expected source alias") ||
      contains_icase(message, "requires an alias")) {
    if (auto span = find_keyword_span(query, "FROM"); span.has_value()) return *span;
  }
  if (contains_icase(message, "WHERE")) {
    if (auto span = find_keyword_span(query, "WHERE"); span.has_value()) return *span;
  }
  if (query.empty()) return span_from_bytes(query, 0, 0);
  return span_from_bytes(query, 0, 1);
}

void set_syntax_code_help(Diagnostic& d) {
  const std::string upper = to_upper_ascii(d.message);
  d.code = "MQL-SYN-0001";
  d.help = "Check SQL clause order: WITH ... SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ... TO ...";
  d.doc_ref = kGrammarDoc;
  if (upper.find("UNEXPECTED TOKEN AFTER QUERY") != std::string::npos) {
    d.code = "MQL-SYN-0002";
    d.help = "Remove trailing tokens after the query terminates, or split multiple statements with ';'.";
    return;
  }
  if (upper.find("EXPECTED )") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.help = "Close the open parenthesis before continuing.";
    return;
  }
  if (upper.find("EXPECTED (") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.help = "Add the missing '(' for the function or clause.";
    return;
  }
  if (upper.find("EXPECTED SELECT") != std::string::npos ||
      upper.find("EXPECTED FROM") != std::string::npos ||
      upper.find("EXPECTED WHERE") != std::string::npos) {
    d.code = "MQL-SYN-0005";
    d.help = "Use canonical SQL order: WITH ... SELECT ... FROM ... WHERE ...";
    return;
  }
  if (upper.find("JOIN REQUIRES ON") != std::string::npos) {
    d.code = "MQL-SYN-0006";
    d.help = "Add ON <condition> after JOIN or use CROSS JOIN without ON.";
    return;
  }
  if (upper.find("CROSS JOIN DOES NOT ALLOW ON") != std::string::npos) {
    d.code = "MQL-SYN-0007";
    d.help = "Remove ON from CROSS JOIN, or change CROSS JOIN to JOIN/LEFT JOIN.";
    return;
  }
  if (upper.find("LATERAL SUBQUERY REQUIRES AN ALIAS") != std::string::npos) {
    d.code = "MQL-SYN-0008";
    d.help = "Add AS <alias> after the LATERAL subquery.";
    return;
  }
}

void set_semantic_code_help(Diagnostic& d) {
  d.code = "MQL-SEM-0999";
  d.help = "Review the failing clause and adjust query shape to match MarkQL validation rules.";
  d.doc_ref = kCliDoc;
  const std::string m = d.message;
  if (contains_icase(m, "Duplicate source alias")) {
    d.code = "MQL-SEM-0101";
    d.help = "Use unique aliases for each FROM/JOIN source in the same scope.";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Duplicate CTE name")) {
    d.code = "MQL-SEM-0102";
    d.help = "Rename one CTE so each WITH binding name is unique.";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Unknown identifier")) {
    d.code = "MQL-SEM-0103";
    d.help = "Reference a bound FROM alias (or legacy tag binding) and check spelling.";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Identifier 'doc' is not bound")) {
    d.code = "MQL-SEM-0104";
    d.help = "When FROM doc AS <alias> is used, reference only that alias (not doc.*).";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Derived table requires an alias")) {
    d.code = "MQL-SEM-0105";
    d.help = "Add AS <alias> after the derived subquery source.";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "TO LIST()")) {
    d.code = "MQL-SEM-0201";
    d.help = "TO LIST() requires exactly one projected column.";
    d.doc_ref = kCliDoc;
    return;
  }
  if (contains_icase(m, "TO TABLE()")) {
    d.code = "MQL-SEM-0202";
    d.help = "Use TO TABLE() only with SELECT table tag-only queries.";
    d.doc_ref = kCliDoc;
    return;
  }
  if (contains_icase(m, "Export")) {
    d.code = "MQL-SEM-0203";
    d.help = "Check export sink syntax and ensure required path arguments are present.";
    d.doc_ref = kCliDoc;
    return;
  }
  if (contains_icase(m, "TEXT()/INNER_HTML()/RAW_INNER_HTML()")) {
    d.code = "MQL-SEM-0301";
    d.help = "Add a WHERE clause with a non-tag filter (attributes/parent/etc.) before projecting TEXT()/INNER_HTML().";
    d.doc_ref = kFunctionsDoc;
    return;
  }
  if (contains_icase(m, "ORDER BY")) {
    d.code = "MQL-SEM-0401";
    d.help = "ORDER BY supports a restricted field set; adjust ORDER BY fields or aggregate usage.";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "LIMIT")) {
    d.code = "MQL-SEM-0402";
    d.help = "Reduce LIMIT to a supported value.";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "PARSE()") || contains_icase(m, "FRAGMENTS()") || contains_icase(m, "RAW()")) {
    d.code = "MQL-SEM-0501";
    d.help = "Ensure source constructors receive valid HTML strings or supported subqueries.";
    d.doc_ref = kSourcesDoc;
    return;
  }
}

bool looks_like_runtime_io(std::string_view message) {
  return contains_icase(message, "Failed to open file") ||
         contains_icase(message, "Failed to fetch URL") ||
         contains_icase(message, "URL fetching is disabled") ||
         contains_icase(message, "Unsupported Content-Type");
}

}  // namespace

Diagnostic make_syntax_diagnostic(const std::string& query,
                                  const std::string& parser_message,
                                  size_t error_byte) {
  Diagnostic d;
  d.severity = DiagnosticSeverity::Error;
  d.message = parser_message;
  d.span = span_from_bytes(query, error_byte, error_byte + 1);
  set_syntax_code_help(d);
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
  set_semantic_code_help(d);
  d.snippet = render_code_frame(query, d.span, "");
  return d;
}

Diagnostic make_runtime_diagnostic(const std::string& query,
                                   const std::string& runtime_message) {
  Diagnostic d;
  d.severity = DiagnosticSeverity::Error;
  d.message = runtime_message;
  d.span = best_effort_semantic_span(query, runtime_message);
  d.code = "MQL-RUN-0001";
  d.help = "Check source availability and query source clauses before retrying.";
  d.doc_ref = kSourcesDoc;
  if (looks_like_runtime_io(runtime_message)) {
    d.code = "MQL-RUN-0002";
    d.help = "Verify the input path/URL and network/file permissions.";
  }
  d.snippet = render_code_frame(query, d.span, "");
  return d;
}

std::string render_diagnostics_text(const std::vector<Diagnostic>& diagnostics) {
  std::ostringstream out;
  for (size_t i = 0; i < diagnostics.size(); ++i) {
    const auto& d = diagnostics[i];
    out << severity_name(d.severity) << "[" << d.code << "]: " << d.message << "\n";
    if (!d.snippet.empty()) out << d.snippet << "\n";
    for (const auto& related : d.related) {
      out << "note: " << related.message
          << " (line " << related.span.start_line << ", col " << related.span.start_col << ")\n";
    }
    out << "help: " << d.help << "\n";
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
    out << "\"span\":{"
        << "\"start_line\":" << d.span.start_line << ","
        << "\"start_col\":" << d.span.start_col << ","
        << "\"end_line\":" << d.span.end_line << ","
        << "\"end_col\":" << d.span.end_col << ","
        << "\"byte_start\":" << d.span.byte_start << ","
        << "\"byte_end\":" << d.span.byte_end
        << "},";
    out << "\"snippet\":\"" << json_escape(d.snippet) << "\",";
    out << "\"related\":[";
    for (size_t j = 0; j < d.related.size(); ++j) {
      if (j != 0) out << ",";
      const auto& related = d.related[j];
      out << "{";
      out << "\"message\":\"" << json_escape(related.message) << "\",";
      out << "\"span\":{"
          << "\"start_line\":" << related.span.start_line << ","
          << "\"start_col\":" << related.span.start_col << ","
          << "\"end_line\":" << related.span.end_line << ","
          << "\"end_col\":" << related.span.end_col << ","
          << "\"byte_start\":" << related.span.byte_start << ","
          << "\"byte_end\":" << related.span.byte_end
          << "}";
      out << "}";
    }
    out << "]";
    out << "}";
  }
  out << "]";
  return out.str();
}

bool has_error_diagnostics(const std::vector<Diagnostic>& diagnostics) {
  for (const auto& d : diagnostics) {
    if (d.severity == DiagnosticSeverity::Error) return true;
  }
  return false;
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
    const std::string parse_message =
        parsed.error.has_value() ? parsed.error->message : message;
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

}  // namespace xsql
