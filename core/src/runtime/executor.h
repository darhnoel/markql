#pragma once

#include <string>

#include "dom/html_parser.h"
#include "lang/ast.h"

namespace xsql {

struct ExecuteError {
  std::string message;
};

struct ExecuteResult {
  std::vector<HtmlNode> nodes;
  std::optional<ExecuteError> error;
};

ExecuteResult execute_query(const Query& query, const HtmlDocument& doc, const std::string& source_uri);

}  // namespace xsql
