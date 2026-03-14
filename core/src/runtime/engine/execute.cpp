#include "markql/markql.h"

#include <string>

#include "engine_execution_internal.h"

namespace markql {

QueryResult execute_query_with_source(const Query& query,
                                      const std::string* default_html,
                                      const HtmlDocument* default_document,
                                      const std::string& default_source_uri) {
  return execute_query_with_source_relation_entry(
      query, default_html, default_document, default_source_uri);
}

}  // namespace markql
