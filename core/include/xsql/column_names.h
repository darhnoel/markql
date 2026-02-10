#pragma once

#include <string>
#include <vector>

namespace xsql {

enum class ColumnNameMode { Normalize, Raw };

struct ColumnNameMapping {
  std::string raw_name;
  std::string output_name;
};

std::string normalize_colname(const std::string& raw, bool lowercase = true);

std::vector<ColumnNameMapping> build_column_name_map(
    const std::vector<std::string>& raw_columns,
    ColumnNameMode mode = ColumnNameMode::Normalize,
    bool lowercase = true);

}  // namespace xsql
