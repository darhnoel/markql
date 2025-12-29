#pragma once

#include <cstddef>
#include <string>

#include "xsql/xsql.h"

namespace xsql::render {

struct DuckboxOptions {
  size_t max_width = 0;
  size_t max_rows = 40;
  bool highlight = false;
  bool is_tty = true;
};

std::string render_duckbox(const xsql::QueryResult& result, const DuckboxOptions& options);

}  // namespace xsql::render
