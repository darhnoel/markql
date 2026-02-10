#pragma once

#include <cstddef>
#include <string>

#include "xsql/column_names.h"
#include "xsql/xsql.h"

namespace xsql::render {

struct DuckboxOptions {
  size_t max_width = 0;
  size_t max_rows = 40;
  bool highlight = false;
  bool is_tty = true;
  xsql::ColumnNameMode colname_mode = xsql::ColumnNameMode::Normalize;
};

std::string render_duckbox(const xsql::QueryResult& result, const DuckboxOptions& options);

}  // namespace xsql::render
