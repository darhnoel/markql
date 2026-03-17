#include "markql/diagnostics.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <vector>
#include <string_view>

#include "markql_parser.h"

namespace markql {

namespace {

constexpr const char* kGrammarDoc = "docs/book/appendix-grammar.md";
constexpr const char* kFunctionsDoc = "docs/book/appendix-function-reference.md";
constexpr const char* kSourcesDoc = "docs/book/ch04-sources-and-loading.md";
constexpr const char* kCliDoc = "docs/markql-cli-guide.md";
constexpr const char* kSelectSelfDoc =
    "docs/book/appendix-grammar.md#select-self-for-current-row-nodes";

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

bool is_typo_candidate_token(std::string_view token) {
  if (token.empty() || token == "<end of input>") return false;
  return std::all_of(token.begin(), token.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-';
  });
}

size_t bounded_edit_distance(std::string_view lhs, std::string_view rhs, size_t max_distance) {
  if (lhs == rhs) return 0;
  if (lhs.empty()) return rhs.size();
  if (rhs.empty()) return lhs.size();
  if (lhs.size() > rhs.size() + max_distance || rhs.size() > lhs.size() + max_distance) {
    return max_distance + 1;
  }

  std::vector<size_t> prev(rhs.size() + 1);
  std::vector<size_t> cur(rhs.size() + 1);
  for (size_t j = 0; j <= rhs.size(); ++j) prev[j] = j;
  for (size_t i = 1; i <= lhs.size(); ++i) {
    cur[0] = i;
    size_t row_min = cur[0];
    for (size_t j = 1; j <= rhs.size(); ++j) {
      const size_t cost = lhs[i - 1] == rhs[j - 1] ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
      row_min = std::min(row_min, cur[j]);
    }
    if (row_min > max_distance) return max_distance + 1;
    prev.swap(cur);
  }
  return prev[rhs.size()];
}

std::optional<std::string> best_typo_match(std::string_view encountered,
                                           const std::vector<std::string>& candidates,
                                           size_t max_distance = 2) {
  if (!is_typo_candidate_token(encountered)) return std::nullopt;
  const std::string lowered = to_lower_ascii(encountered);
  size_t best_distance = max_distance + 1;
  std::optional<std::string> best_match;
  for (const auto& candidate : candidates) {
    const size_t distance = bounded_edit_distance(lowered, to_lower_ascii(candidate), max_distance);
    if (distance < best_distance) {
      best_distance = distance;
      best_match = candidate;
    }
  }
  if (best_distance <= max_distance) return best_match;
  return std::nullopt;
}

void set_typo_aware_help(Diagnostic& d,
                         std::string_view encountered_raw,
                         const std::vector<std::string>& candidates,
                         const std::string& fallback_help,
                         const std::string& fallback_example) {
  if (auto suggestion = best_typo_match(encountered_raw, candidates); suggestion.has_value()) {
    d.help = "Replace '" + std::string(encountered_raw) + "' with '" + *suggestion + "'.";
    d.example = fallback_example;
    return;
  }
  d.help = fallback_help;
  d.example = fallback_example;
}

struct TokenSlice {
  std::string text;
  size_t start = 0;
  size_t end = 0;
};

std::string normalize_token_name(std::string token) {
  if (token.empty()) return token;
  if (token == "<end of input>") return "end of input";
  if (token.front() == '\'' || token.front() == '"') return token;
  const bool alpha =
      std::all_of(token.begin(), token.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
      });
  if (!alpha) return token;
  return to_upper_ascii(token);
}

TokenSlice token_at_or_after(const std::string& query, size_t byte_pos) {
  TokenSlice token;
  size_t pos = std::min(byte_pos, query.size());
  while (pos < query.size() && std::isspace(static_cast<unsigned char>(query[pos]))) {
    ++pos;
  }
  if (pos >= query.size()) {
    token.text = "<end of input>";
    token.start = query.size();
    token.end = query.size();
    return token;
  }

  token.start = pos;
  const char c = query[pos];
  auto finish = [&](size_t end) {
    token.end = std::min(end, query.size());
    token.text = query.substr(token.start, token.end - token.start);
    return token;
  };

  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
    ++pos;
    while (pos < query.size()) {
      const char next = query[pos];
      if (!std::isalnum(static_cast<unsigned char>(next)) && next != '_' && next != '-') break;
      ++pos;
    }
    return finish(pos);
  }
  if (std::isdigit(static_cast<unsigned char>(c))) {
    ++pos;
    while (pos < query.size() && std::isdigit(static_cast<unsigned char>(query[pos]))) ++pos;
    return finish(pos);
  }
  if (c == '\'' || c == '"') {
    const char quote = c;
    ++pos;
    while (pos < query.size()) {
      if (query[pos] == '\\' && pos + 1 < query.size()) {
        pos += 2;
        continue;
      }
      ++pos;
      if (query[pos - 1] == quote) break;
    }
    return finish(pos);
  }
  if (pos + 1 < query.size()) {
    const std::string two = query.substr(pos, 2);
    if (two == "<=" || two == ">=" || two == "!=" || two == "<>") return finish(pos + 2);
  }
  return finish(pos + 1);
}

std::string trim_ascii(std::string_view in) {
  size_t start = 0;
  size_t end = in.size();
  while (start < end && std::isspace(static_cast<unsigned char>(in[start]))) ++start;
  while (end > start && std::isspace(static_cast<unsigned char>(in[end - 1]))) --end;
  return std::string(in.substr(start, end - start));
}

std::vector<std::string> split_expected_terms(std::string fragment) {
  const auto replace_all = [&](std::string& value,
                               std::string_view needle,
                               std::string_view replacement) {
    size_t pos = 0;
    while ((pos = value.find(needle, pos)) != std::string::npos) {
      value.replace(pos, needle.size(), replacement);
      pos += replacement.size();
    }
  };

  replace_all(fragment, ", or ", ", ");
  replace_all(fragment, " or ", ", ");
  replace_all(fragment, " OR ", ", ");
  replace_all(fragment, " Or ", ", ");

  std::vector<std::string> out;
  size_t start = 0;
  while (start <= fragment.size()) {
    size_t end = fragment.find(',', start);
    if (end == std::string::npos) end = fragment.size();
    std::string piece = trim_ascii(std::string_view(fragment).substr(start, end - start));
    while (!piece.empty() && (piece.back() == '.' || piece.back() == ')')) piece.pop_back();
    while (!piece.empty() && piece.front() == '(') piece.erase(piece.begin());
    if (!piece.empty()) out.push_back(piece);
    if (end == fragment.size()) break;
    start = end + 1;
  }
  return out;
}

std::string format_expected_choices(const std::vector<std::string>& choices) {
  if (choices.empty()) return "";
  if (choices.size() == 1) return choices.front();
  if (choices.size() == 2) return choices[0] + " or " + choices[1];
  std::ostringstream out;
  for (size_t i = 0; i < choices.size(); ++i) {
    if (i > 0) out << (i + 1 == choices.size() ? ", or " : ", ");
    out << choices[i];
  }
  return out.str();
}

bool contains_recent_icase(const std::string& query,
                           size_t byte_pos,
                           std::string_view needle,
                           size_t window = 96) {
  const size_t end = std::min(byte_pos, query.size());
  const size_t start = end > window ? end - window : 0;
  return contains_icase(std::string_view(query).substr(start, end - start), needle);
}

enum class ParseExpectationFamily {
  None,
  KeywordSet,
  OperatorSet,
  AxisName,
  ScalarExpression,
  OpenParen,
  CloseParen,
};

struct ParseExpectation {
  ParseExpectationFamily family = ParseExpectationFamily::None;
  std::string label;
  std::vector<std::string> candidates;
  std::string subject;
};

std::string extract_expected_subject(std::string_view message) {
  const std::vector<std::string> markers = {" after ", " inside ", " in "};
  for (const auto& marker : markers) {
    const size_t pos = find_icase(message, marker);
    if (pos == std::string::npos) continue;
    return trim_ascii(message.substr(pos + marker.size()));
  }
  return "";
}

ParseExpectation classify_parse_expectation(std::string_view parser_message) {
  ParseExpectation out;
  const std::string upper = to_upper_ascii(parser_message);
  out.subject = extract_expected_subject(parser_message);

  if (upper.find("EXPECTED SCALAR EXPRESSION") != std::string::npos) {
    out.family = ParseExpectationFamily::ScalarExpression;
    out.label = "scalar expression";
    return out;
  }
  if (upper.find("EXPECTED AXIS NAME") != std::string::npos) {
    out.family = ParseExpectationFamily::AxisName;
    out.label = "axis name";
    out.candidates = {"self", "parent", "child", "ancestor", "descendant"};
    return out;
  }
  if (upper.find("EXPECTED =, <>, <, <=, >, >=, ~, LIKE, IN, CONTAINS, HAS_DIRECT_TEXT, OR IS") !=
      std::string::npos) {
    out.family = ParseExpectationFamily::OperatorSet;
    out.label = "comparison operator";
    out.candidates = {"=", "<>", "<", "<=", ">", ">=", "~",
                      "LIKE", "IN", "CONTAINS", "HAS_DIRECT_TEXT", "IS"};
    return out;
  }
  if (upper.find("EXPECTED (") != std::string::npos) {
    out.family = ParseExpectationFamily::OpenParen;
    out.label = "(";
    out.candidates = {"("};
    return out;
  }
  if (upper.find("EXPECTED )") != std::string::npos) {
    out.family = ParseExpectationFamily::CloseParen;
    out.label = ")";
    out.candidates = {")"};
    return out;
  }

  const size_t expected_pos = find_icase(parser_message, "Expected ");
  if (expected_pos == std::string::npos) return out;

  size_t end = parser_message.size();
  for (const std::string marker : {" after ", " inside ", " in "}) {
    const size_t marker_pos = find_icase(parser_message, marker);
    if (marker_pos != std::string::npos) end = std::min(end, marker_pos);
  }
  std::string fragment = trim_ascii(parser_message.substr(expected_pos + 9, end - (expected_pos + 9)));
  if (fragment.empty()) return out;

  const size_t paren_pos = fragment.find('(');
  if (paren_pos != std::string::npos && fragment.back() == ')') {
    out.label = trim_ascii(fragment.substr(0, paren_pos));
    out.candidates = split_expected_terms(fragment.substr(paren_pos + 1, fragment.size() - paren_pos - 2));
  } else {
    out.candidates = split_expected_terms(fragment);
  }
  if (!out.candidates.empty()) {
    out.family = ParseExpectationFamily::KeywordSet;
    if (out.label.empty()) out.label = format_expected_choices(out.candidates);
  }
  return out;
}

bool is_table_option_subject(std::string_view subject) {
  return contains_icase(subject, "HEADER") || contains_icase(subject, "TRIM_EMPTY_ROWS") ||
         contains_icase(subject, "TRIM_EMPTY_COLS") || contains_icase(subject, "EMPTY_IS") ||
         contains_icase(subject, "FORMAT") || contains_icase(subject, "SPARSE_SHAPE");
}

std::string example_for_table_option(std::string_view subject) {
  if (contains_icase(subject, "TRIM_EMPTY_COLS")) {
    return "SELECT table FROM doc TO TABLE(TRIM_EMPTY_COLS=TRAILING)";
  }
  if (contains_icase(subject, "EMPTY_IS")) {
    return "SELECT table FROM doc TO TABLE(EMPTY_IS=BLANK_OR_NULL)";
  }
  if (contains_icase(subject, "FORMAT")) {
    return "SELECT table FROM doc TO TABLE(FORMAT=SPARSE)";
  }
  if (contains_icase(subject, "SPARSE_SHAPE")) {
    return "SELECT table FROM doc TO TABLE(FORMAT=SPARSE, SPARSE_SHAPE=LONG)";
  }
  if (contains_icase(subject, "HEADER_NORMALIZE")) {
    return "SELECT table FROM doc TO TABLE(HEADER_NORMALIZE=ON)";
  }
  if (contains_icase(subject, "TRIM_EMPTY_ROWS")) {
    return "SELECT table FROM doc TO TABLE(TRIM_EMPTY_ROWS=ON)";
  }
  return "SELECT table FROM doc TO TABLE(HEADER ON)";
}

void set_choice_help(Diagnostic& d,
                     std::string_view encountered_raw,
                     const std::vector<std::string>& candidates,
                     const std::string& fallback_help,
                     const std::string& example) {
  set_typo_aware_help(d, encountered_raw, candidates, fallback_help, example);
}

bool apply_family_based_syntax_details(Diagnostic& d,
                                       const std::string& query,
                                       const std::string& parser_message,
                                       size_t error_byte,
                                       const TokenSlice& encountered,
                                       const ParseExpectation& expectation) {
  if (expectation.family == ParseExpectationFamily::None) return false;

  const bool exists_context =
      expectation.family == ParseExpectationFamily::AxisName &&
      (contains_icase(expectation.subject, "EXISTS") || contains_icase(parser_message, "EXISTS") ||
       contains_recent_icase(query, error_byte, "EXISTS("));

  if (expectation.family == ParseExpectationFamily::ScalarExpression) {
    d.message = "Missing scalar expression";
    d.why = "Predicates and scalar functions need a value or expression at this position.";
    d.help = "Add a string, number, NULL, function call, or field expression.";
    d.example = "SELECT div FROM doc WHERE attributes.id = 'main'";
    d.expected = "scalar expression";
    return true;
  }

  if (expectation.family == ParseExpectationFamily::OperatorSet) {
    d.message = "Expected a comparison operator";
    d.why =
        "A MarkQL comparison must place an operator between the left-hand expression and the right-hand value.";
    d.expected = "comparison operator";
    set_choice_help(
        d,
        encountered.text,
        expectation.candidates,
        "Use a supported operator such as =, LIKE, IN, CONTAINS, HAS_DIRECT_TEXT, or IS.",
        "SELECT div FROM doc WHERE tag LIKE 'div'");
    return true;
  }

  if (expectation.family == ParseExpectationFamily::AxisName && exists_context) {
    d.message = "Malformed EXISTS(...) predicate";
    d.why =
        "EXISTS(axis [WHERE ...]) requires one of MarkQL's supported axes: self, parent, child, ancestor, or descendant.";
    d.expected = "axis name inside EXISTS(...)";
    set_choice_help(
        d,
        encountered.text,
        expectation.candidates,
        "Use EXISTS(self|parent|child|ancestor|descendant [WHERE ...]).",
        "SELECT div FROM doc WHERE EXISTS(descendant WHERE tag = 'a')");
    return true;
  }

  if (expectation.family == ParseExpectationFamily::KeywordSet) {
    if (contains_icase(expectation.subject, "POSITION")) {
      d.message = "POSITION(...) requires the keyword IN";
      d.why = "MarkQL uses the SQL-style POSITION(substr IN str) form.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(
          d,
          encountered.text,
          expectation.candidates,
          "Use POSITION(substr IN str).",
          "SELECT div FROM doc WHERE POSITION('a' IN text) > 0");
      return true;
    }
    if (contains_icase(expectation.subject, "CASE EXPRESSION")) {
      d.message = "CASE expression is missing " + format_expected_choices(expectation.candidates);
      d.why = "Each CASE WHEN branch in MarkQL must use the expected keyword before its result expression.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(
          d,
          encountered.text,
          expectation.candidates,
          "Write CASE WHEN <condition> THEN <value> [ELSE <value>] END.",
          "SELECT CASE WHEN tag = 'div' THEN 'yes' ELSE 'no' END AS flag FROM doc");
      return true;
    }
    if (contains_icase(expectation.subject, "ORDER")) {
      d.code = "MQL-SYN-0005";
      d.message = "ORDER must be followed by BY";
      d.why = "MarkQL uses ORDER BY to introduce sort fields.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(
          d,
          encountered.text,
          expectation.candidates,
          "Write ORDER BY <field>.",
          "SELECT div FROM doc ORDER BY node_id");
      return true;
    }
    if (contains_icase(expectation.subject, "TO")) {
      d.message = "TO requires an output target";
      d.why =
          "After TO, MarkQL expects a supported output target such as LIST(), TABLE(), CSV(...), JSON(), or NDJSON().";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(
          d,
          encountered.text,
          expectation.candidates,
          "Use TO LIST(), TO TABLE(), TO CSV(...), TO PARQUET(...), TO JSON(), or TO NDJSON().",
          "SELECT a.href FROM doc WHERE href IS NOT NULL TO LIST()");
      return true;
    }
    if (contains_icase(expectation.subject, "SHOW")) {
      d.message = "SHOW requires a supported metadata category";
      d.why = "SHOW in the current MarkQL grammar only accepts supported metadata categories.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(
          d,
          encountered.text,
          expectation.candidates,
          "Use SHOW FUNCTIONS, SHOW AXES, SHOW OPERATORS, SHOW INPUT, or SHOW INPUTS.",
          "SHOW FUNCTIONS");
      return true;
    }
    if (contains_icase(expectation.subject, "DESCRIBE")) {
      d.message = "DESCRIBE requires a supported target";
      d.why = "DESCRIBE in the current MarkQL grammar only accepts supported metadata targets.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(
          d,
          encountered.text,
          expectation.candidates,
          "Use DESCRIBE doc, DESCRIBE document, or DESCRIBE language.",
          "DESCRIBE language");
      return true;
    }
    if (is_table_option_subject(expectation.subject)) {
      d.message = "Invalid value for TABLE() option " + trim_ascii(expectation.subject);
      d.why =
          "This TABLE() option accepts only the listed values in the current MarkQL grammar.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(
          d,
          encountered.text,
          expectation.candidates,
          "Use one of: " + format_expected_choices(expectation.candidates) + ".",
          example_for_table_option(expectation.subject));
      return true;
    }
    if (!expectation.subject.empty()) {
      d.message = parser_message;
      d.why = "This MarkQL construct only accepts the listed keyword values at this position.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(
          d,
          encountered.text,
          expectation.candidates,
          "Use one of: " + format_expected_choices(expectation.candidates) + ".",
          "");
      return true;
    }
  }

  return false;
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
    return "Relation-style queries (WITH, JOIN, CTE, or derived sources) currently use a reduced lint validation path.";
  }
  if (summary.coverage == LintCoverageLevel::ParseOnly) {
    return "No semantic validation ran after parsing.";
  }
  if (summary.coverage == LintCoverageLevel::Mixed) {
    return "Some statements were fully validated while others only received reduced or parse-level coverage.";
  }
  return "";
}

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

std::string style_token(const std::string& token,
                        const char* color,
                        const DiagnosticTextRenderOptions& options) {
  if (!options.use_color) return token;
  return std::string(color) + token + kAnsiReset;
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
  if (contains_icase(message, "PROJECT()") || contains_icase(message, "FLATTEN_EXTRACT()")) {
    if (auto span = find_keyword_span(query, "PROJECT"); span.has_value()) return *span;
    if (auto span = find_keyword_span(query, "FLATTEN_EXTRACT"); span.has_value()) return *span;
  }
  if (contains_icase(message, "FLATTEN_TEXT()")) {
    if (auto span = find_keyword_span(query, "FLATTEN_TEXT"); span.has_value()) return *span;
    if (auto span = find_keyword_span(query, "FLATTEN"); span.has_value()) return *span;
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

void set_syntax_details(Diagnostic& d,
                        const std::string& query,
                        const std::string& parser_message,
                        size_t error_byte) {
  const TokenSlice encountered = token_at_or_after(query, error_byte);
  const std::string upper = to_upper_ascii(parser_message);
  const std::string encountered_upper = normalize_token_name(encountered.text);
  const ParseExpectation expectation = classify_parse_expectation(parser_message);
  d.category = "parse";
  d.code = "MQL-SYN-0001";
  d.message = parser_message;
  d.why = "MarkQL could not parse the query at this token.";
  d.help = "Review the local syntax near the highlighted token and compare it with the surrounding MarkQL construct.";
  d.example.clear();
  d.expected.clear();
  d.encountered = encountered_upper;
  d.doc_ref = kGrammarDoc;

  if (upper.find("EXPECTED TAG IDENTIFIER") != std::string::npos && encountered_upper == "FROM") {
    d.message = "Missing projection after SELECT";
    d.why = "SELECT must contain at least one projection before FROM in MarkQL.";
    d.help = "Add a tag, self, *, or a projection expression after SELECT.";
    d.example = "SELECT div FROM doc";
    d.expected = "projection after SELECT";
    return;
  }

  if (upper.find("EXPECTED FROM") != std::string::npos &&
      (encountered_upper == "WHERE" || encountered_upper == "ORDER" ||
       encountered_upper == "LIMIT" || encountered_upper == "TO" ||
       encountered_upper == "JOIN" || encountered_upper == "EXCLUDE")) {
    d.code = "MQL-SYN-0005";
    d.message = "Invalid clause order before FROM";
    d.why = "MarkQL clause order is WITH ... SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ... TO ...";
    d.help = "Move the FROM clause so it appears before later clauses.";
    d.example = "SELECT div FROM doc WHERE tag = 'div'";
    d.expected = "FROM";
    return;
  }

  if (upper.find("CASE EXPRESSION REQUIRES AT LEAST ONE WHEN") != std::string::npos) {
    d.message = "CASE expression requires WHEN ... THEN";
    d.why = "A CASE expression in MarkQL must contain at least one WHEN ... THEN branch.";
    d.help = "Add at least one WHEN ... THEN branch before END.";
    d.example = "SELECT CASE WHEN tag = 'div' THEN 'yes' ELSE 'no' END AS flag FROM doc";
    return;
  }

  if (upper.find("EXISTS") != std::string::npos &&
      upper.find("EXPECTED ) AFTER EXISTS") != std::string::npos) {
    d.message = "Malformed EXISTS(...) predicate";
    d.why = "EXISTS() in MarkQL requires an axis name and may include an inner WHERE clause.";
    d.help = "Use EXISTS(child) or EXISTS(child WHERE tag = 'a').";
    d.example = "SELECT div FROM doc WHERE EXISTS(child WHERE tag = 'a')";
    return;
  }

  if (upper.find("EXPECTED CTE NAME AFTER WITH") != std::string::npos ||
      upper.find("EXPECTED AS AFTER CTE NAME") != std::string::npos ||
      upper.find("EXPECTED ( AFTER CTE AS") != std::string::npos ||
      upper.find("EXPECTED ) AFTER CTE SUBQUERY") != std::string::npos) {
    d.message = "Malformed WITH clause";
    d.why = "Each CTE must follow WITH name AS (subquery).";
    d.help = "Use WITH rows AS (SELECT ...) SELECT ... FROM rows";
    d.example = "WITH rows AS (SELECT div FROM doc) SELECT * FROM rows";
    return;
  }

  if (upper.find("PROJECT/FLATTEN_EXTRACT") != std::string::npos ||
      upper.find("PROJECT()") != std::string::npos ||
      upper.find("FLATTEN_EXTRACT()") != std::string::npos) {
    d.message = "Invalid PROJECT()/FLATTEN_EXTRACT() usage";
    d.why = "PROJECT()/FLATTEN_EXTRACT() requires a base tag and AS (alias: expression, ...) mapping.";
    d.help = "Use PROJECT(li) AS (title: TEXT(h2))";
    d.example = "SELECT PROJECT(li) AS (title: TEXT(h2)) FROM doc WHERE tag = 'li'";
    return;
  }

  if (upper.find("EXPECTED TAG IDENTIFIER INSIDE TEXT()") != std::string::npos ||
      upper.find("EXPECTED TAG IDENTIFIER INSIDE ATTR()") != std::string::npos ||
      upper.find("EXPECTED TAG IDENTIFIER OR SELF INSIDE EXTRACTION FUNCTION") != std::string::npos ||
      upper.find("EXPECTED TAG IDENTIFIER OR SELF INSIDE ATTR()") != std::string::npos) {
    d.message = "This function requires a node/tag reference";
    d.why = "MarkQL extraction functions operate on a tag or self, not on a scalar literal.";
    d.help = "Pass a tag name, a bound alias, or self to the function.";
    d.example = "SELECT TEXT(self) FROM doc WHERE self.tag = 'div'";
    d.expected = "tag name, alias, or self";
    return;
  }

  if (apply_family_based_syntax_details(
          d, query, parser_message, error_byte, encountered, expectation)) {
    return;
  }

  if (upper.find("UNEXPECTED TOKEN AFTER QUERY") != std::string::npos) {
    d.code = "MQL-SYN-0002";
    d.message = "Unexpected trailing input after the query";
    d.why = "MarkQL finished parsing one query, but more tokens remained.";
    d.help = "Remove trailing tokens after the query terminates, or split multiple statements with ';'.";
    d.example = "SELECT div FROM doc;";
    return;
  }
  if (upper.find("EXPECTED ) AFTER TEXT ARGUMENT") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "TEXT(...) must close its argument list before the query continues.";
    d.help = "Close TEXT(...) with ')' before continuing.";
    d.example = "SELECT TEXT(div) FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ) AFTER INNER_HTML/RAW_INNER_HTML ARGUMENT") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "INNER_HTML(...)/RAW_INNER_HTML(...) must close its argument list before the query continues.";
    d.help = "Close INNER_HTML(...)/RAW_INNER_HTML(...) with ')'.";
    d.example = "SELECT INNER_HTML(div) FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ) AFTER ATTR ARGUMENTS") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "ATTR(tag, attr) must close after the attribute argument.";
    d.help = "Close ATTR(...) with ')' after the attribute name.";
    d.example = "SELECT ATTR(a, href) FROM doc WHERE tag = 'a'";
    return;
  }
  if (upper.find("EXPECTED ) AFTER POSITION ARGUMENTS") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "POSITION(substr IN str) must close after the string expression.";
    d.help = "Close POSITION(...) with ')' after the second argument.";
    d.example = "SELECT div FROM doc WHERE POSITION('a' IN text) > 0";
    return;
  }
  if (upper.find("EXPECTED ) AFTER FUNCTION ARGUMENTS") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "This function call must close its argument list before the query continues.";
    d.help = "Close the function call with ')' after the last argument.";
    d.example = "SELECT LOWER(text) AS lowered FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ) AFTER TRIM ARGUMENT") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "TRIM(...) must close its argument list before aliasing or continuing the query.";
    d.help = "Close TRIM(...) with ')' after the argument.";
    d.example = "SELECT TRIM(text) AS cleaned FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ) AFTER COUNT ARGUMENT") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "COUNT(...) must close after its tag or '*' argument.";
    d.help = "Close COUNT(...) with ')' after the argument.";
    d.example = "SELECT COUNT(*) FROM doc";
    return;
  }
  if (upper.find("EXPECTED ) AFTER TFIDF ARGUMENTS") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "TFIDF(...) must close after its tag list and options.";
    d.help = "Close TFIDF(...) with ')' after the final tag or option.";
    d.example = "SELECT TFIDF(div, TOP_TERMS=10) FROM doc";
    return;
  }
  if (upper.find("EXPECTED )") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "A clause or function call was opened earlier and was not closed here.";
    d.help = "Close the open parenthesis before continuing.";
    d.example = "SELECT ATTR(self, id) FROM doc WHERE self.tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ( AFTER COUNT") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "COUNT in MarkQL is written as COUNT(...).";
    d.help = "Add '(' after COUNT.";
    d.example = "SELECT COUNT(*) FROM doc";
    return;
  }
  if (upper.find("EXPECTED ( AFTER TRIM") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "TRIM in MarkQL is written as TRIM(...).";
    d.help = "Add '(' after TRIM.";
    d.example = "SELECT TRIM(text) FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ( AFTER TEXT") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "TEXT in MarkQL is written as TEXT(tag|self).";
    d.help = "Add '(' after TEXT.";
    d.example = "SELECT TEXT(div) FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ( AFTER INNER_HTML/RAW_INNER_HTML") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "INNER_HTML and RAW_INNER_HTML in MarkQL are written with parentheses.";
    d.help = "Add '(' after INNER_HTML or RAW_INNER_HTML.";
    d.example = "SELECT INNER_HTML(div) FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ( AFTER COALESCE") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "COALESCE in MarkQL is written as COALESCE(a, b, ...).";
    d.help = "Add '(' after COALESCE.";
    d.example = "SELECT COALESCE(TEXT(span), 'n/a') AS value FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ( AFTER TFIDF") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "TFIDF in MarkQL is written as TFIDF(tag, ...).";
    d.help = "Add '(' after TFIDF.";
    d.example = "SELECT TFIDF(div) FROM doc";
    return;
  }
  if (upper.find("EXPECTED ( AFTER FUNCTION NAME") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "Scalar functions in MarkQL use parentheses for their argument list.";
    d.help = "Add '(' after the function name.";
    d.example = "SELECT LOWER(text) AS lowered FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED (") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "This MarkQL construct requires a parenthesized argument list or subquery.";
    d.help = "Add the missing '(' for the function or clause.";
    d.example = "SELECT TEXT(self) FROM doc WHERE self.tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED SELECT") != std::string::npos ||
      upper.find("EXPECTED FROM") != std::string::npos ||
      upper.find("EXPECTED WHERE") != std::string::npos) {
    d.code = "MQL-SYN-0005";
    d.expected = upper.find("EXPECTED SELECT") != std::string::npos ? "SELECT" :
                 upper.find("EXPECTED FROM") != std::string::npos ? "FROM" : "WHERE";
    d.why = "The query does not follow MarkQL's clause order.";
    d.help = "Use canonical SQL order: WITH ... SELECT ... FROM ... WHERE ...";
    d.example = "SELECT div FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("JOIN REQUIRES ON") != std::string::npos) {
    d.code = "MQL-SYN-0006";
    d.why = "JOIN and LEFT JOIN need an ON predicate in MarkQL relation queries.";
    d.help = "Add ON <condition> after JOIN or use CROSS JOIN without ON.";
    d.example = "SELECT * FROM a JOIN b ON a.id = b.id";
    return;
  }
  if (upper.find("CROSS JOIN DOES NOT ALLOW ON") != std::string::npos) {
    d.code = "MQL-SYN-0007";
    d.why = "CROSS JOIN in MarkQL is unconditional and does not accept an ON clause.";
    d.help = "Remove ON from CROSS JOIN, or change CROSS JOIN to JOIN/LEFT JOIN.";
    d.example = "SELECT * FROM a CROSS JOIN b";
    return;
  }
  if (upper.find("LATERAL SUBQUERY REQUIRES AN ALIAS") != std::string::npos) {
    d.code = "MQL-SYN-0008";
    d.why = "A LATERAL subquery becomes a source in FROM/JOIN and therefore needs an alias.";
    d.help = "Add AS <alias> after the LATERAL subquery.";
    d.example = "SELECT * FROM a CROSS JOIN LATERAL (SELECT ...) AS x";
    return;
  }

  if (upper.find("EXPECTED END TO CLOSE CASE EXPRESSION") != std::string::npos) {
    d.why = "CASE expressions in MarkQL must end with END.";
    d.expected = "END";
    set_typo_aware_help(
        d,
        encountered.text,
        {"END"},
        "Close the CASE expression with END.",
        "SELECT CASE WHEN tag = 'div' THEN 'yes' ELSE 'no' END FROM doc");
    d.example = "SELECT CASE WHEN tag = 'div' THEN 'yes' ELSE 'no' END FROM doc";
    return;
  }

  d.help = "Check the syntax near the highlighted token. If this is a full query, the canonical order is WITH ... SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ... TO ...";
  d.example = "SELECT div FROM doc";
}

void set_semantic_details(Diagnostic& d) {
  d.category = "semantic";
  d.code = "MQL-SEM-0999";
  d.why = "The query parsed, but it violates a MarkQL validation rule.";
  d.help = "Review the failing clause and adjust query shape to match MarkQL validation rules.";
  d.example.clear();
  d.expected.clear();
  d.encountered.clear();
  d.doc_ref = kCliDoc;
  const std::string m = d.message;
  if (contains_icase(m, "Duplicate source alias")) {
    d.category = "binding";
    d.code = "MQL-SEM-0101";
    d.why = "Each FROM/JOIN source alias must be unique within the same query scope.";
    d.help = "Use unique aliases for each FROM/JOIN source in the same scope.";
    d.example = "SELECT n.node_id FROM doc AS n JOIN doc AS m ON n.node_id = m.node_id";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Duplicate CTE name")) {
    d.category = "binding";
    d.code = "MQL-SEM-0102";
    d.why = "Each WITH binding name must be unique within the same WITH clause.";
    d.help = "Rename one CTE so each WITH binding name is unique.";
    d.example = "WITH left_rows AS (SELECT * FROM doc), right_rows AS (SELECT * FROM doc) SELECT * FROM left_rows";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Unknown identifier")) {
    d.category = "binding";
    d.code = "MQL-SEM-0103";
    d.why = "Identifiers must resolve to a bound FROM/JOIN alias or a supported legacy tag binding.";
    d.help = "Reference a bound FROM alias (or legacy tag binding) and check spelling.";
    d.example = "SELECT n.node_id FROM doc AS n WHERE n.tag = 'div'";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Identifier 'doc' is not bound")) {
    d.category = "binding";
    d.code = "MQL-SEM-0104";
    d.why = "Once FROM doc AS <alias> is used, that alias becomes the bound name for the row source.";
    d.help = "When FROM doc AS <alias> is used, reference only that alias (not doc.*).";
    d.example = "SELECT n.node_id FROM doc AS n WHERE n.tag = 'div'";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Derived table requires an alias")) {
    d.category = "binding";
    d.code = "MQL-SEM-0105";
    d.why = "A derived subquery in FROM/JOIN must be named so later clauses can reference it.";
    d.help = "Add AS <alias> after the derived subquery source.";
    d.example = "SELECT rows.node_id FROM (SELECT self.node_id FROM doc) AS rows";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "TO LIST()")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0201";
    d.why = "TO LIST() emits one list value per row, so the SELECT shape must contain exactly one projected column.";
    d.help = "TO LIST() requires exactly one projected column.";
    d.example = "SELECT a.href FROM doc WHERE href IS NOT NULL TO LIST()";
    d.doc_ref = kCliDoc;
    return;
  }
  if (contains_icase(m, "TO TABLE()")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0202";
    d.why = "TO TABLE() is only defined for table-node extraction, not for arbitrary projections or relation queries.";
    d.help = "Use TO TABLE() only with SELECT table tag-only queries.";
    d.example = "SELECT table FROM doc TO TABLE()";
    d.doc_ref = kCliDoc;
    return;
  }
  if (contains_icase(m, "Export")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0203";
    d.why = "Export targets require a valid MarkQL TO <format>(...) sink shape.";
    d.help = "Check export sink syntax and ensure required path arguments are present.";
    d.example = "SELECT a.href FROM doc TO CSV('links.csv')";
    d.doc_ref = kCliDoc;
    return;
  }
  if (contains_icase(m, "PROJECT()/FLATTEN_EXTRACT()") ||
      contains_icase(m, "FLATTEN_TEXT()/PROJECT()/FLATTEN_EXTRACT()")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0204";
    d.why = "MarkQL restricts PROJECT()/FLATTEN_EXTRACT()/FLATTEN_TEXT() to specific query shapes.";
    d.help = "Use one extraction helper per query and provide the required AS mapping.";
    d.example = "SELECT PROJECT(li) AS (title: TEXT(h2)) FROM doc WHERE tag = 'li'";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Cannot mix tag-only and projected fields in SELECT") ||
      contains_icase(m, "Projected fields must use a single tag") ||
      contains_icase(m, "EXCLUDE requires SELECT *")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0205";
    d.why = "MarkQL SELECT output shape must stay internally consistent within one query.";
    d.help = "Use one SELECT shape at a time: tag-only, wildcard, or projected fields.";
    d.example = "SELECT a.href FROM doc WHERE tag = 'a'";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "TEXT()/INNER_HTML()/RAW_INNER_HTML()")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0301";
    d.why = "These node extraction helpers require a WHERE clause that narrows rows with more than a plain tag check.";
    d.help = "Add a WHERE clause with a non-tag filter (attributes/parent/etc.) before projecting TEXT()/INNER_HTML().";
    d.example = "SELECT TEXT(self) FROM doc WHERE attributes.id IS NOT NULL";
    d.doc_ref = kFunctionsDoc;
    return;
  }
  if (contains_icase(m, "ORDER BY")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0401";
    d.why = "ORDER BY is only supported for specific result shapes and field sets in the current engine.";
    d.help = "ORDER BY supports a restricted field set; adjust ORDER BY fields or aggregate usage.";
    d.example = "SELECT self.node_id FROM doc WHERE self.tag = 'div' ORDER BY node_id DESC";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "LIMIT")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0402";
    d.why = "LIMIT must fit the engine's supported numeric range and query shape.";
    d.help = "Reduce LIMIT to a supported value.";
    d.example = "SELECT div FROM doc LIMIT 10";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "PARSE()") || contains_icase(m, "FRAGMENTS()") || contains_icase(m, "RAW()")) {
    d.category = "semantic";
    d.code = "MQL-SEM-0501";
    d.why = "Source constructors accept only supported argument shapes and valid text inputs.";
    d.help = "Ensure source constructors receive valid HTML strings or supported subqueries.";
    d.example = "SELECT self FROM PARSE('<div>ok</div>') AS n";
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

Diagnostic make_runtime_diagnostic(const std::string& query,
                                   const std::string& runtime_message) {
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

Diagnostic make_select_alias_ambiguity_warning(const std::string& query,
                                               size_t byte_start,
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

std::string render_diagnostics_text(const std::vector<Diagnostic>& diagnostics) {
  return render_diagnostics_text(diagnostics, DiagnosticTextRenderOptions{});
}

std::string render_diagnostics_text(const std::vector<Diagnostic>& diagnostics,
                                    const DiagnosticTextRenderOptions& options) {
  std::ostringstream out;
  for (size_t i = 0; i < diagnostics.size(); ++i) {
    const auto& d = diagnostics[i];
    out << style_token(severity_name(d.severity), severity_ansi_color(d.severity), options)
        << "[" << d.code << "]: " << d.message << "\n";
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
      out << style_token("note:", kAnsiBlue, options) << " " << related.message
          << " (line " << related.span.start_line << ", col " << related.span.start_col << ")\n";
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
  out << "Result: " << result.summary.error_count << " error(s), "
      << result.summary.warning_count << " warning(s), "
      << result.summary.note_count << " note(s)";
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
  out << "\"relation_style_query\":"
      << (result.summary.relation_style_query ? "true" : "false") << ",";
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

}  // namespace markql
