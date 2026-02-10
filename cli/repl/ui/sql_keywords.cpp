#include "sql_keywords.h"

#include <cctype>
#include <unordered_set>

namespace xsql::cli {

const std::vector<std::string>& markql_reserved_keywords() {
  static const std::vector<std::string> keywords = {
      "select", "from", "where", "and", "or", "in", "is", "null", "not", "exists", "like",
      "limit", "order", "by", "asc", "desc", "exclude", "as", "to", "list", "csv", "parquet",
      "json", "ndjson", "raw", "fragments", "contains", "all", "any", "has_direct_text",
      "project", "show", "describe", "input", "inputs", "functions", "axes", "operators",
      "case", "when", "then", "else", "end"};
  return keywords;
}

const std::vector<std::string>& markql_completion_keywords() {
  static const std::vector<std::string> keywords = {
      "select", "from", "where", "and", "or", "in", "is", "null", "not", "exists", "like",
      "limit", "order", "by", "asc", "desc", "exclude", "as", "to", "list", "table", "csv",
      "parquet", "json", "ndjson", "document", "doc", "raw", "fragments", "contains", "all",
      "any", "has_direct_text", "flatten_text", "flatten", "project", "flatten_extract",
      "attributes", "tag", "text", "direct_text", "inner_html", "raw_inner_html", "attr",
      "parent", "child", "ancestor", "descendant", "node_id", "parent_id", "sibling_pos",
      "max_depth", "doc_order", "source_uri", "count", "summarize", "tfidf", "top_terms",
      "min_df", "max_df", "stopwords", "english", "none", "default", "header", "noheader",
      "no_header", "on", "off", "export", "show", "describe", "input", "inputs", "functions",
      "axes", "operators", "language", "case", "when", "then", "else", "end", "coalesce",
      "concat", "substring", "substr", "length", "char_length", "position", "locate", "replace",
      "lower", "upper", "trim", "ltrim", "rtrim", "first_text", "last_text", "first_attr",
      "last_attr"};
  return keywords;
}

bool is_sql_keyword_token(std::string_view word) {
  // WHY: the renderer checks tokens frequently while typing; keep lookup O(1).
  static const std::unordered_set<std::string> keywords(
      markql_reserved_keywords().begin(), markql_reserved_keywords().end());
  std::string lower;
  lower.reserve(word.size());
  for (char c : word) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return keywords.find(lower) != keywords.end();
}

}  // namespace xsql::cli
