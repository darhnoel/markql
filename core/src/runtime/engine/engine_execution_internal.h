#pragma once

#include "markql/markql.h"

#include <optional>
#include <string>
#include <vector>

#include "../../dom/html_parser.h"
#include "../../lang/markql_parser.h"

namespace markql {

struct FragmentSource {
  std::vector<std::string> fragments;
};

QueryResult::TableOptions to_result_table_options(const Query::TableOptions& options);
bool table_uses_default_output(const Query& query);
void materialize_table_result(const std::vector<std::vector<std::string>>& raw_rows,
                              bool has_header, const Query::TableOptions& options,
                              QueryResult::TableResult& table);

QueryResult execute_meta_query(const Query& query, const std::string& source_uri);
void validate_query_for_execution(const Query& query);
std::optional<std::string> eval_parse_source_expr(const ScalarExpr& expr);
QueryResult execute_query_ast(const Query& query, const HtmlDocument& doc,
                              const std::string& source_uri);
QueryResult execute_query_with_source_legacy(const Query& query, const std::string* default_html,
                                             const HtmlDocument* default_document,
                                             const std::string& default_source_uri);
QueryResult execute_query_with_source(const Query& query, const std::string* default_html,
                                      const HtmlDocument* default_document,
                                      const std::string& default_source_uri);
QueryResult execute_query_with_source_relation_entry(const Query& query,
                                                     const std::string* default_html,
                                                     const HtmlDocument* default_document,
                                                     const std::string& default_source_uri);

std::optional<int64_t> parse_int64_value(const std::string& value);
bool contains_ci(const std::string& haystack, const std::string& needle);
bool like_match_ci(const std::string& text, const std::string& pattern);
bool contains_all_ci(const std::string& haystack, const std::vector<std::string>& tokens);
bool contains_any_ci(const std::string& haystack, const std::vector<std::string>& tokens);
std::vector<std::string> split_ws(const std::string& s);

std::optional<std::string> field_value_string(const QueryResultRow& row, const std::string& field);
bool looks_like_html_fragment(const std::string& value);
void append_document(HtmlDocument& target, const HtmlDocument& source);
HtmlDocument build_fragments_document(const FragmentSource& fragments);
FragmentSource collect_html_fragments(const QueryResult& result, const std::string& source_name);

}  // namespace markql
