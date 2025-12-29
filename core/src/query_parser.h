#pragma once

#include <optional>
#include <string>

#include "ast.h"

namespace xsql {

struct ParseError {
  std::string message;
  size_t position = 0;
};

struct ParseResult {
  std::optional<Query> query;
  std::optional<ParseError> error;
};

ParseResult parse_query(const std::string& input);

}  // namespace xsql
