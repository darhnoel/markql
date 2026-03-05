#include "xsql/xsql.h"

#include <string>
#include <utility>
#include <vector>

#include "../../lang/markql_parser.h"
#include "engine_execution_internal.h"

namespace xsql {

namespace {

QueryResult build_meta_result(const std::vector<std::string>& columns,
                              const std::vector<std::vector<std::string>>& rows) {
  QueryResult out;
  out.columns = columns;
  for (const auto& values : rows) {
    QueryResultRow row;
    for (size_t i = 0; i < columns.size() && i < values.size(); ++i) {
      const auto& col = columns[i];
      const auto& value = values[i];
      if (col == "source_uri") {
        row.source_uri = value;
      } else {
        row.attributes[col] = value;
      }
    }
    out.rows.push_back(std::move(row));
  }
  return out;
}

}  // namespace

QueryResult execute_meta_query(const Query& query, const std::string& source_uri) {
  switch (query.kind) {
    case Query::Kind::ShowInput: {
      return build_meta_result({"key", "value"},
                               {{"source_uri", source_uri}});
    }
    case Query::Kind::ShowInputs: {
      return build_meta_result({"source_uri"},
                               {{source_uri}});
    }
    case Query::Kind::ShowFunctions: {
      return build_meta_result(
          {"function", "returns", "description"},
          {
              {"text(tag|self)", "string", "Text content of a tag or current row node"},
              {"direct_text(tag|self)", "string", "Immediate text content of a tag or current row node"},
              {"first_text(tag WHERE ...)", "string", "First scoped TEXT match (alias of TEXT(..., 1))"},
              {"last_text(tag WHERE ...)", "string", "Last scoped TEXT match"},
              {"first_attr(tag, attr WHERE ...)", "string", "First scoped ATTR match"},
              {"last_attr(tag, attr WHERE ...)", "string", "Last scoped ATTR match"},
              {"concat(a, b, ...)", "string", "Concatenate strings; NULL if any arg is NULL"},
              {"substring(str, start, len)", "string", "1-based substring"},
              {"substr(str, start, len)", "string", "Alias of substring"},
              {"length(str)", "int64", "String length in UTF-8 bytes"},
              {"char_length(str)", "int64", "Alias of length"},
              {"position(substr IN str)", "int64", "1-based position, 0 if not found"},
              {"locate(substr, str[, start])", "int64", "1-based position, 0 if not found"},
              {"replace(str, from, to)", "string", "Replace substring"},
              {"lower(str)", "string", "Lowercase"},
              {"upper(str)", "string", "Uppercase"},
              {"ltrim(str)", "string", "Trim left whitespace"},
              {"rtrim(str)", "string", "Trim right whitespace"},
              {"coalesce(a, b, ...)", "scalar", "First non-NULL value"},
              {"case when ... then ... else ... end", "scalar", "Conditional expression"},
              {"inner_html(tag|self[, depth|MAX_DEPTH])", "string", "Minified HTML inside a tag/current row node"},
              {"raw_inner_html(tag|self[, depth|MAX_DEPTH])", "string", "Raw inner HTML without minification"},
              {"flatten_text(tag[, depth])", "string[]", "Flatten descendant text at depth into columns"},
              {"flatten(tag[, depth])", "string[]", "Alias of flatten_text"},
              {"project(tag)", "mixed[]", "Evaluate named extraction expressions per row"},
              {"flatten_extract(tag)", "mixed[]", "Compatibility alias of project(tag)"},
              {"trim(inner_html(...))", "string", "Trim whitespace in inner_html"},
              {"count(tag|*)", "int64", "Aggregate node count"},
              {"summarize(*)", "table<tag,count>", "Tag counts summary"},
              {"tfidf(tag|*)", "map<string,double>", "TF-IDF term scores"},
          });
    }
    case Query::Kind::ShowAxes: {
      return build_meta_result(
          {"axis", "description"},
          {
              {"parent", "Parent node"},
              {"child", "Direct child nodes"},
              {"ancestor", "Any ancestor node"},
              {"descendant", "Any descendant node"},
          });
    }
    case Query::Kind::ShowOperators: {
      return build_meta_result(
          {"operator", "description"},
          {
              {"=", "Equality"},
              {"<>", "Not equal"},
              {"<, <=, >, >=", "Ordered comparison"},
              {"IN (...)", "Membership"},
              {"LIKE", "SQL-style wildcard match (% and _)"},
              {"CONTAINS", "Substring or list contains"},
              {"CONTAINS ALL", "Contains all values"},
              {"CONTAINS ANY", "Contains any value"},
              {"IS NULL", "Null check"},
              {"IS NOT NULL", "Not-null check"},
              {"HAS_DIRECT_TEXT", "Direct text predicate"},
              {"~", "Regex match"},
              {"AND", "Logical AND"},
              {"OR", "Logical OR"},
          });
    }
    case Query::Kind::DescribeDoc: {
      return build_meta_result(
          {"column_name", "type", "nullable", "notes"},
          {
              {"node_id", "int64", "false", "Stable node identifier"},
              {"tag", "string", "false", "Lowercase tag name"},
              {"attributes", "map<string,string>", "false", "HTML attributes"},
              {"parent_id", "int64", "true", "Null for root"},
              {"max_depth", "int64", "false", "Max element depth under node"},
              {"doc_order", "int64", "false", "Preorder document index"},
              {"sibling_pos", "int64", "false", "1-based among siblings"},
              {"source_uri", "string", "true", "Empty for RAW/STDIN"},
          });
    }
    case Query::Kind::DescribeLanguage: {
      return build_meta_result(
          {"category", "name", "syntax", "notes"},
          {
              {"clause", "SELECT", "SELECT <tag|self|*>[, ...]",
               "Tag list, self row-node projection, or *"},
              {"clause", "FROM", "FROM <source>", "Defaults to document in REPL"},
              {"clause", "WHERE", "WHERE <expr>", "Predicate expression"},
              {"clause", "ORDER BY", "ORDER BY <field> [ASC|DESC]",
               "node_id, tag, text, parent_id, sibling_pos, max_depth, doc_order; SUMMARIZE uses tag/count"},
              {"clause", "LIMIT", "LIMIT <n>", "n >= 0, max enforced"},
              {"clause", "EXCLUDE", "EXCLUDE <field>[, ...]", "Only with SELECT *"},
              {"output", "TO LIST", "TO LIST()", "Requires one projected column"},
              {"output", "TO TABLE",
               "TO TABLE([HEADER|NOHEADER][, TRIM_EMPTY_ROWS=ON][, TRIM_EMPTY_COLS=TRAILING|ALL]"
               "[, EMPTY_IS=...][, STOP_AFTER_EMPTY_ROWS=n][, FORMAT=SPARSE][, SPARSE_SHAPE=LONG|WIDE]"
               "[, HEADER_NORMALIZE=ON][, EXPORT='file.csv'])",
               "Select table tags only"},
              {"output", "TO CSV", "TO CSV('file.csv')", "Export result"},
              {"output", "TO PARQUET", "TO PARQUET('file.parquet')", "Export result"},
              {"output", "TO JSON", "TO JSON(['file.json'])", "Export rows as a JSON array"},
              {"output", "TO NDJSON", "TO NDJSON(['file.ndjson'])", "Export rows as newline-delimited JSON"},
              {"source", "document", "FROM document", "Active input in REPL"},
              {"source", "alias", "FROM doc", "Alias for document"},
              {"source", "path", "FROM 'file.html'", "Local file"},
              {"source", "url", "FROM 'https://example.com'", "Requires libcurl"},
              {"source", "raw", "FROM RAW('<html>')", "Inline HTML"},
              {"source", "parse", "FROM PARSE('<ul><li>...</li></ul>') AS frag",
               "Parse HTML strings into a node source"},
              {"source", "fragments", "FROM FRAGMENTS(<raw|subquery>)",
               "Concatenate HTML fragments (deprecated; use PARSE)"},
              {"source", "fragments_raw", "FRAGMENTS(RAW('<ul>...</ul>'))", "Raw fragment input"},
              {"source", "fragments_query",
               "FRAGMENTS(SELECT inner_html(...) FROM doc)", "Subquery returns HTML strings"},
              {"field", "node_id", "node_id", "int64"},
              {"field", "tag", "tag", "lowercase"},
              {"field", "attributes", "attributes", "map<string,string>"},
              {"field", "parent_id", "parent_id", "int64 or null"},
              {"field", "sibling_pos", "sibling_pos", "1-based among siblings"},
              {"field", "source_uri", "source_uri", "Hidden unless multi-source"},
              {"function", "text", "text(tag|self)", "Direct text content; requires WHERE"},
              {"function", "inner_html", "inner_html(tag|self[, depth|MAX_DEPTH])",
               "Minified inner HTML; depth defaults to 1; requires WHERE"},
              {"function", "raw_inner_html", "raw_inner_html(tag|self[, depth|MAX_DEPTH])",
               "Raw inner HTML (no minify); depth defaults to 1; requires WHERE"},
              {"function", "trim", "trim(text(...)) | trim(inner_html(...))",
               "Trim whitespace"},
              {"function", "direct_text", "direct_text(tag|self)", "Immediate text children only"},
              {"function", "concat", "concat(a, b, ...)", "NULL if any arg is NULL"},
              {"function", "substring", "substring(str, start, len)", "1-based slicing"},
              {"function", "length", "length(str)", "UTF-8 byte length"},
              {"function", "position", "position(substr IN str)", "1-based; 0 if not found"},
              {"function", "replace", "replace(str, from, to)", "Substring replacement"},
              {"function", "case expression",
               "CASE WHEN <expr> THEN <value> [ELSE <value>] END",
               "Evaluates WHEN clauses top-to-bottom"},
              {"function", "trim family", "ltrim/rtrim/trim(str)", "Whitespace trimming"},
              {"function", "first_text", "first_text(tag WHERE ...)", "First scoped text match"},
              {"function", "last_text", "last_text(tag WHERE ...)", "Last scoped text match"},
              {"function", "first_attr", "first_attr(tag, attr WHERE ...)", "First scoped attr match"},
              {"function", "last_attr", "last_attr(tag, attr WHERE ...)", "Last scoped attr match"},
              {"function", "project",
               "project(tag) AS (alias: expr, ...)",
               "Expressions: TEXT/ATTR/DIRECT_TEXT/COALESCE plus SQL string functions"},
              {"function", "flatten_extract",
               "flatten_extract(tag) AS (alias: expr, ...)",
               "Expressions: TEXT/ATTR/DIRECT_TEXT/COALESCE plus SQL string functions"},
              {"aggregate", "count", "count(tag|*)", "int64"},
              {"aggregate", "summarize", "summarize(*)", "tag counts table"},
              {"aggregate", "tfidf", "tfidf(tag|*)", "map<string,double>"},
              {"axis", "parent", "parent.<field>", "Direct parent"},
              {"axis", "child", "child.<field>", "Direct child"},
              {"axis", "ancestor", "ancestor.<field>", "Any ancestor"},
              {"axis", "descendant", "descendant.<field>", "Any descendant"},
              {"predicate", "exists", "EXISTS(axis [WHERE expr])", "Existential axis predicate"},
              {"operator", "=", "lhs = rhs", "Equality"},
              {"operator", "<>", "lhs <> rhs", "Not equal"},
              {"operator", "<, <=, >, >=", "lhs > rhs", "Ordered comparison"},
              {"operator", "IN", "lhs IN ('a','b')", "Membership"},
              {"operator", "LIKE", "lhs LIKE '%x%'", "SQL-style wildcard match"},
              {"operator", "CONTAINS", "lhs CONTAINS 'x'", "Substring or list contains"},
              {"operator", "CONTAINS ALL", "lhs CONTAINS ALL ('a','b')", "All values"},
              {"operator", "CONTAINS ANY", "lhs CONTAINS ANY ('a','b')", "Any value"},
              {"operator", "IS NULL", "lhs IS NULL", "Null check"},
              {"operator", "IS NOT NULL", "lhs IS NOT NULL", "Not-null check"},
              {"operator", "HAS_DIRECT_TEXT", "HAS_DIRECT_TEXT", "Predicate on direct text"},
              {"operator", "~", "lhs ~ 're'", "Regex match"},
              {"operator", "AND", "expr AND expr", "Logical AND"},
              {"operator", "OR", "expr OR expr", "Logical OR"},
              {"meta", "SHOW INPUT", "SHOW INPUT", "Active source"},
              {"meta", "SHOW INPUTS", "SHOW INPUTS", "Last result sources or active"},
              {"meta", "SHOW FUNCTIONS", "SHOW FUNCTIONS", "Function list"},
              {"meta", "SHOW AXES", "SHOW AXES", "Axis list"},
              {"meta", "SHOW OPERATORS", "SHOW OPERATORS", "Operator list"},
              {"meta", "DESCRIBE doc", "DESCRIBE doc", "Document schema"},
              {"meta", "DESCRIBE language", "DESCRIBE language", "Language spec"},
          });
    }
    case Query::Kind::Select:
    default:
      return QueryResult{};
  }
}

}  // namespace xsql
