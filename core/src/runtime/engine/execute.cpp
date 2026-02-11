#include "markql/markql.h"

#include <memory>
#include <stdexcept>
#include <string>

#include "engine_execution_internal.h"

namespace markql {

struct ParsedDocumentHandle {
  HtmlDocument doc;
  std::string html;
  std::string source_uri;
};

QueryResult execute_query_with_source(const Query& query,
                                      const std::string* default_html,
                                      const HtmlDocument* default_document,
                                      const std::string& default_source_uri) {
  return execute_query_with_source_relation_entry(
      query, default_html, default_document, default_source_uri);
}

std::shared_ptr<const ParsedDocumentHandle> prepare_document(const std::string& html,
                                                             const std::string& source_uri) {
  auto prepared = std::make_shared<ParsedDocumentHandle>();
  prepared->doc = parse_html(html);
  prepared->html = html;
  prepared->source_uri = source_uri.empty() ? "document" : source_uri;
  return prepared;
}

QueryResult execute_query_from_prepared_document(const std::shared_ptr<const ParsedDocumentHandle>& prepared,
                                                 const std::string& query) {
  if (prepared == nullptr) {
    throw std::runtime_error("Prepared document handle is null");
  }
  auto parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    throw std::runtime_error("Query parse error: " + parsed.error->message);
  }
  validate_query_for_execution(*parsed.query);
  if (parsed.query->kind != Query::Kind::Select) {
    return execute_meta_query(*parsed.query, prepared->source_uri);
  }
  if (parsed.query->source.kind == Source::Kind::Document) {
    return execute_query_ast(*parsed.query, prepared->doc, prepared->source_uri);
  }
  return execute_query_with_source(*parsed.query, &prepared->html, &prepared->doc, prepared->source_uri);
}

}  // namespace markql
