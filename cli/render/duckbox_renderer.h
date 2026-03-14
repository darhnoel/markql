#pragma once

#include <cstddef>
#include <string>

#include "markql/column_names.h"
#include "markql/markql.h"

namespace markql::render {

struct DuckboxOptions {
  size_t max_width = 0;
  size_t max_rows = 40;
  bool highlight = false;
  bool is_tty = true;
  markql::ColumnNameMode colname_mode = markql::ColumnNameMode::Normalize;
};

std::string render_duckbox(const markql::QueryResult& result, const DuckboxOptions& options);

}  // namespace markql::render
