#pragma once

#include <optional>
#include <string>

namespace markql {

struct FormatResult {
  std::optional<std::string> text;
  std::string error;
};

/// Parse and format one MarkQL query into the canonical textual representation.
FormatResult format_query_text(const std::string& input);

}  // namespace markql
