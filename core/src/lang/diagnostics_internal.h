#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "markql/diagnostics.h"

namespace markql::diagnostics_internal {

inline constexpr const char* kGrammarDoc = "docs/book/appendix-grammar.md";
inline constexpr const char* kFunctionsDoc = "docs/book/appendix-function-reference.md";
inline constexpr const char* kSourcesDoc = "docs/book/ch04-sources-and-loading.md";
inline constexpr const char* kCliDoc = "docs/markql-cli-guide.md";
inline constexpr const char* kSelectSelfDoc =
    "docs/book/appendix-grammar.md#select-self-for-current-row-nodes";

std::string to_upper_ascii(std::string_view in);
std::string to_lower_ascii(std::string_view in);
bool contains_icase(std::string_view haystack, std::string_view needle);
size_t find_icase(std::string_view haystack, std::string_view needle);
bool is_ident_char(char c);
std::optional<std::string> extract_single_quoted(std::string_view message);
bool is_typo_candidate_token(std::string_view token);
size_t bounded_edit_distance(std::string_view lhs, std::string_view rhs, size_t max_distance);
std::optional<std::string> best_typo_match(std::string_view encountered,
                                           const std::vector<std::string>& candidates,
                                           size_t max_distance = 2);
void set_typo_aware_help(Diagnostic& d, std::string_view encountered_raw,
                         const std::vector<std::string>& candidates,
                         const std::string& fallback_help, const std::string& fallback_example);

struct TokenSlice {
  std::string text;
  size_t start = 0;
  size_t end = 0;
};

std::string normalize_token_name(std::string token);
TokenSlice token_at_or_after(const std::string& query, size_t byte_pos);
std::string trim_ascii(std::string_view in);
std::vector<std::string> split_expected_terms(std::string fragment);
std::string format_expected_choices(const std::vector<std::string>& choices);
bool contains_recent_icase(const std::string& query, size_t byte_pos, std::string_view needle,
                           size_t window = 96);

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

std::string extract_expected_subject(std::string_view message);
ParseExpectation classify_parse_expectation(std::string_view parser_message);
bool is_table_option_subject(std::string_view subject);
std::string example_for_table_option(std::string_view subject);
void set_choice_help(Diagnostic& d, std::string_view encountered_raw,
                     const std::vector<std::string>& candidates, const std::string& fallback_help,
                     const std::string& example);
bool apply_family_based_syntax_details(Diagnostic& d, const std::string& query,
                                       const std::string& parser_message, size_t error_byte,
                                       const TokenSlice& encountered,
                                       const ParseExpectation& expectation);

DiagnosticSpan span_from_bytes(const std::string& query, size_t byte_start, size_t byte_end);
std::optional<DiagnosticSpan> find_keyword_span(const std::string& query,
                                                const std::string& keyword);
std::optional<DiagnosticSpan> find_identifier_span(const std::string& query,
                                                   const std::string& identifier);
DiagnosticSpan best_effort_semantic_span(const std::string& query, const std::string& message);
void set_syntax_details(Diagnostic& d, const std::string& query, const std::string& parser_message,
                        size_t error_byte);
void set_semantic_details(Diagnostic& d);
bool looks_like_runtime_io(std::string_view message);

std::string severity_name(DiagnosticSeverity severity);
std::string coverage_name(LintCoverageLevel coverage);
std::string lint_status_name(const LintSummary& summary);
std::string lint_summary_message(const LintSummary& summary);
std::string lint_coverage_note(const LintSummary& summary);
std::string json_escape(std::string_view s);
std::string render_code_frame(const std::string& query, const DiagnosticSpan& span,
                              const std::string& label);

}  // namespace markql::diagnostics_internal
