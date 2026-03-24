#pragma once

#include <stdexcept>
#include <string>

namespace markql::cli {

struct QueryRenderResult {
  std::string text;
  bool rendered = false;
};

class QueryRenderError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// Loads a query file and optionally renders it before downstream parsing.
/// MUST preserve existing plain-text file behavior when render_mode is empty.
QueryRenderResult load_query_file_text(const std::string& query_file,
                                       const std::string& render_mode,
                                       const std::string& vars_file);

/// Writes rendered MarkQL exactly as produced, without adding formatting.
/// MUST write to stdout when destination is "-" and to a file otherwise.
void write_rendered_query_output(const std::string& destination, const std::string& rendered_query);

/// Returns true when the rendered output destination is stdout preview mode.
bool render_to_stdout_only(const std::string& destination);

}  // namespace markql::cli
