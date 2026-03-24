#include "diagnostics_internal.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

namespace markql::diagnostics_internal {

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
  return std::all_of(token.begin(), token.end(),
                     [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
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
                                           size_t max_distance) {
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

void set_typo_aware_help(Diagnostic& d, std::string_view encountered_raw,
                         const std::vector<std::string>& candidates,
                         const std::string& fallback_help, const std::string& fallback_example) {
  if (auto suggestion = best_typo_match(encountered_raw, candidates); suggestion.has_value()) {
    d.help = "Replace '" + std::string(encountered_raw) + "' with '" + *suggestion + "'.";
    d.example = fallback_example;
    return;
  }
  d.help = fallback_help;
  d.example = fallback_example;
}

std::string normalize_token_name(std::string token) {
  if (token.empty()) return token;
  if (token == "<end of input>") return "end of input";
  if (token.front() == '\'' || token.front() == '"') return token;
  const bool alpha = std::all_of(token.begin(), token.end(), [](unsigned char c) {
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
  const auto replace_all = [&](std::string& value, std::string_view needle,
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

bool contains_recent_icase(const std::string& query, size_t byte_pos, std::string_view needle,
                           size_t window) {
  const size_t end = std::min(byte_pos, query.size());
  const size_t start = end > window ? end - window : 0;
  return contains_icase(std::string_view(query).substr(start, end - start), needle);
}

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
    out.candidates = {
        "=", "<>", "<", "<=", ">=", ">", "~", "LIKE", "IN", "CONTAINS", "HAS_DIRECT_TEXT", "IS"};
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
  std::string fragment =
      trim_ascii(parser_message.substr(expected_pos + 9, end - (expected_pos + 9)));
  if (fragment.empty()) return out;

  const size_t paren_pos = fragment.find('(');
  if (paren_pos != std::string::npos && fragment.back() == ')') {
    out.label = trim_ascii(fragment.substr(0, paren_pos));
    out.candidates =
        split_expected_terms(fragment.substr(paren_pos + 1, fragment.size() - paren_pos - 2));
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

void set_choice_help(Diagnostic& d, std::string_view encountered_raw,
                     const std::vector<std::string>& candidates, const std::string& fallback_help,
                     const std::string& example) {
  set_typo_aware_help(d, encountered_raw, candidates, fallback_help, example);
}

bool apply_family_based_syntax_details(Diagnostic& d, const std::string& query,
                                       const std::string& parser_message, size_t error_byte,
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
        "A MarkQL comparison must place an operator between the left-hand expression and the "
        "right-hand value.";
    d.expected = "comparison operator";
    set_choice_help(
        d, encountered.text, expectation.candidates,
        "Use a supported operator such as =, LIKE, IN, CONTAINS, HAS_DIRECT_TEXT, or IS.",
        "SELECT div FROM doc WHERE tag LIKE 'div'");
    return true;
  }

  if (expectation.family == ParseExpectationFamily::AxisName && exists_context) {
    d.message = "Malformed EXISTS(...) predicate";
    d.why =
        "EXISTS(axis [WHERE ...]) requires one of MarkQL's supported axes: self, parent, child, "
        "ancestor, or descendant.";
    d.expected = "axis name inside EXISTS(...)";
    set_choice_help(d, encountered.text, expectation.candidates,
                    "Use EXISTS(self|parent|child|ancestor|descendant [WHERE ...]).",
                    "SELECT div FROM doc WHERE EXISTS(descendant WHERE tag = 'a')");
    return true;
  }

  if (expectation.family == ParseExpectationFamily::KeywordSet) {
    if (contains_icase(expectation.subject, "POSITION")) {
      d.message = "POSITION(...) requires the keyword IN";
      d.why = "MarkQL uses the SQL-style POSITION(substr IN str) form.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(d, encountered.text, expectation.candidates, "Use POSITION(substr IN str).",
                      "SELECT div FROM doc WHERE POSITION('a' IN text) > 0");
      return true;
    }
    if (contains_icase(expectation.subject, "CASE EXPRESSION")) {
      d.message = "CASE expression is missing " + format_expected_choices(expectation.candidates);
      d.why =
          "Each CASE WHEN branch in MarkQL must use the expected keyword before its result "
          "expression.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(d, encountered.text, expectation.candidates,
                      "Write CASE WHEN <condition> THEN <value> [ELSE <value>] END.",
                      "SELECT CASE WHEN tag = 'div' THEN 'yes' ELSE 'no' END AS flag FROM doc");
      return true;
    }
    if (contains_icase(expectation.subject, "ORDER")) {
      d.code = "MQL-SYN-0005";
      d.message = "ORDER must be followed by BY";
      d.why = "MarkQL uses ORDER BY to introduce sort fields.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(d, encountered.text, expectation.candidates, "Write ORDER BY <field>.",
                      "SELECT div FROM doc ORDER BY node_id");
      return true;
    }
    if (contains_icase(expectation.subject, "TO")) {
      d.message = "TO requires an output target";
      d.why =
          "After TO, MarkQL expects a supported output target such as LIST(), TABLE(), CSV(...), "
          "JSON(), or NDJSON().";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(
          d, encountered.text, expectation.candidates,
          "Use TO LIST(), TO TABLE(), TO CSV(...), TO PARQUET(...), TO JSON(), or TO NDJSON().",
          "SELECT a.href FROM doc WHERE href IS NOT NULL TO LIST()");
      return true;
    }
    if (contains_icase(expectation.subject, "SHOW")) {
      d.message = "SHOW requires a supported metadata category";
      d.why = "SHOW in the current MarkQL grammar only accepts supported metadata categories.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(d, encountered.text, expectation.candidates,
                      "Use SHOW FUNCTIONS, SHOW AXES, SHOW OPERATORS, SHOW INPUT, or SHOW INPUTS.",
                      "SHOW FUNCTIONS");
      return true;
    }
    if (contains_icase(expectation.subject, "DESCRIBE")) {
      d.message = "DESCRIBE requires a supported target";
      d.why = "DESCRIBE in the current MarkQL grammar only accepts supported metadata targets.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(d, encountered.text, expectation.candidates,
                      "Use DESCRIBE doc, DESCRIBE document, or DESCRIBE language.",
                      "DESCRIBE language");
      return true;
    }
    if (is_table_option_subject(expectation.subject)) {
      d.message = "Invalid value for TABLE() option " + trim_ascii(expectation.subject);
      d.why = "This TABLE() option accepts only the listed values in the current MarkQL grammar.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(d, encountered.text, expectation.candidates,
                      "Use one of: " + format_expected_choices(expectation.candidates) + ".",
                      example_for_table_option(expectation.subject));
      return true;
    }
    if (!expectation.subject.empty()) {
      d.message = parser_message;
      d.why = "This MarkQL construct only accepts the listed keyword values at this position.";
      d.expected = format_expected_choices(expectation.candidates);
      set_choice_help(d, encountered.text, expectation.candidates,
                      "Use one of: " + format_expected_choices(expectation.candidates) + ".", "");
      return true;
    }
  }

  return false;
}

}  // namespace markql::diagnostics_internal
