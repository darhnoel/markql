#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace xsql::cli {

/// Returns reserved MarkQL SQL keywords (used for syntax coloring).
const std::vector<std::string>& markql_reserved_keywords();

/// Returns completion vocabulary (reserved keywords + common MarkQL identifiers/functions).
const std::vector<std::string>& markql_completion_keywords();

/// Case-insensitive keyword lookup for token coloring.
bool is_sql_keyword_token(std::string_view word);

}  // namespace xsql::cli
