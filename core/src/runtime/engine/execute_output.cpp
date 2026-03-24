#include "markql/markql.h"

#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../lang/markql_parser.h"
#include "../../util/string_util.h"
#include "engine_execution_internal.h"

namespace markql {

namespace {

bool is_space_or_nbsp(const std::string& text, size_t index, size_t& consumed) {
  const unsigned char c = static_cast<unsigned char>(text[index]);
  if (std::isspace(c)) {
    consumed = 1;
    return true;
  }
  if (c == 0xC2 && index + 1 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0xA0) {
    consumed = 2;
    return true;
  }
  return false;
}

std::string normalize_table_whitespace(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  bool have_non_space = false;
  bool pending_space = false;
  for (size_t i = 0; i < value.size();) {
    size_t consumed = 0;
    if (is_space_or_nbsp(value, i, consumed)) {
      if (have_non_space) {
        pending_space = true;
      }
      i += consumed;
      continue;
    }
    if (pending_space) {
      out.push_back(' ');
      pending_space = false;
    }
    out.push_back(value[i]);
    have_non_space = true;
    ++i;
  }
  return out;
}

std::string normalize_header_text(const std::string& value) {
  const std::string normalized = normalize_table_whitespace(value);
  if (normalized.empty()) return "";
  std::vector<std::string> deduped_tokens;
  size_t start = 0;
  while (start < normalized.size()) {
    size_t end = normalized.find(' ', start);
    if (end == std::string::npos) end = normalized.size();
    std::string token = normalized.substr(start, end - start);
    if (!token.empty() && (deduped_tokens.empty() || deduped_tokens.back() != token)) {
      deduped_tokens.push_back(std::move(token));
    }
    start = end + 1;
  }
  std::string out;
  for (size_t i = 0; i < deduped_tokens.size(); ++i) {
    if (i > 0) out.push_back(' ');
    out += deduped_tokens[i];
  }
  return out;
}

bool table_cell_empty(const std::vector<std::string>& row, size_t col_index,
                      Query::TableOptions::EmptyIs empty_is) {
  if (col_index >= row.size()) {
    return empty_is == Query::TableOptions::EmptyIs::BlankOrNull ||
           empty_is == Query::TableOptions::EmptyIs::NullOnly;
  }
  if (empty_is == Query::TableOptions::EmptyIs::NullOnly) {
    return false;
  }
  return normalize_table_whitespace(row[col_index]).empty();
}

bool table_row_all_empty(const std::vector<std::string>& row, size_t max_cols,
                         Query::TableOptions::EmptyIs empty_is) {
  if (max_cols == 0) return true;
  for (size_t col = 0; col < max_cols; ++col) {
    if (!table_cell_empty(row, col, empty_is)) return false;
  }
  return true;
}

std::vector<size_t> select_table_columns(const std::vector<std::vector<std::string>>& rows,
                                         size_t max_cols, const Query::TableOptions& options) {
  std::vector<size_t> selected;
  if (max_cols == 0) return selected;
  if (options.trim_empty_cols == Query::TableOptions::TrimEmptyCols::Off) {
    selected.reserve(max_cols);
    for (size_t col = 0; col < max_cols; ++col) selected.push_back(col);
    return selected;
  }
  std::vector<bool> empty_cols(max_cols, true);
  for (size_t col = 0; col < max_cols; ++col) {
    for (const auto& row : rows) {
      if (!table_cell_empty(row, col, options.empty_is)) {
        empty_cols[col] = false;
        break;
      }
    }
  }
  if (options.trim_empty_cols == Query::TableOptions::TrimEmptyCols::Trailing) {
    size_t keep_until = max_cols;
    while (keep_until > 0 && empty_cols[keep_until - 1]) {
      --keep_until;
    }
    selected.reserve(keep_until);
    for (size_t col = 0; col < keep_until; ++col) selected.push_back(col);
    return selected;
  }
  selected.reserve(max_cols);
  for (size_t col = 0; col < max_cols; ++col) {
    if (!empty_cols[col]) selected.push_back(col);
  }
  return selected;
}

std::vector<std::string> unique_header_keys(const std::vector<std::string>& headers) {
  std::vector<std::string> keys;
  keys.reserve(headers.size());
  std::unordered_map<std::string, size_t> seen;
  for (const auto& header : headers) {
    auto count_it = seen.find(header);
    if (count_it == seen.end()) {
      seen[header] = 1;
      keys.push_back(header);
      continue;
    }
    const size_t next = ++count_it->second;
    keys.push_back(header + "_" + std::to_string(next));
  }
  return keys;
}

struct MaterializedTable {
  std::vector<std::string> headers;
  std::vector<std::string> header_keys;
  std::vector<std::vector<std::string>> rect_rows;
  std::vector<std::vector<std::string>> sparse_long_rows;
  std::vector<std::vector<std::pair<std::string, std::string>>> sparse_wide_rows;
};

MaterializedTable materialize_table(const std::vector<std::vector<std::string>>& raw_rows,
                                    bool has_header, const Query::TableOptions& options) {
  MaterializedTable out;
  if (raw_rows.empty()) return out;

  size_t max_cols = 0;
  for (const auto& row : raw_rows) {
    if (row.size() > max_cols) max_cols = row.size();
  }

  std::vector<std::vector<std::string>> kept_rows;
  kept_rows.reserve(raw_rows.size());
  size_t consecutive_empty_rows = 0;
  for (const auto& row : raw_rows) {
    const bool all_empty = table_row_all_empty(row, max_cols, options.empty_is);
    if (all_empty) {
      ++consecutive_empty_rows;
    } else {
      consecutive_empty_rows = 0;
    }
    if (!(options.trim_empty_rows && all_empty)) {
      kept_rows.push_back(row);
    }
    if (options.stop_after_empty_rows > 0 &&
        consecutive_empty_rows >= options.stop_after_empty_rows) {
      break;
    }
  }

  max_cols = 0;
  for (const auto& row : kept_rows) {
    if (row.size() > max_cols) max_cols = row.size();
  }
  std::vector<size_t> keep_cols = select_table_columns(kept_rows, max_cols, options);
  const size_t out_cols = keep_cols.size();

  out.rect_rows.reserve(kept_rows.size());
  for (const auto& row : kept_rows) {
    std::vector<std::string> projected;
    projected.reserve(out_cols);
    for (size_t keep_index : keep_cols) {
      if (keep_index < row.size()) {
        projected.push_back(row[keep_index]);
      } else {
        projected.emplace_back();
      }
    }
    out.rect_rows.push_back(std::move(projected));
  }

  if (out_cols == 0) {
    return out;
  }

  const bool apply_header_normalize =
      has_header && options.header_normalize && options.header_normalize_explicit;

  out.headers.resize(out_cols);
  for (size_t col = 0; col < out_cols; ++col) {
    std::string header;
    if (has_header && !out.rect_rows.empty() && col < out.rect_rows[0].size()) {
      header = out.rect_rows[0][col];
    }
    if (apply_header_normalize) {
      header = normalize_header_text(header);
    }
    if (header.empty()) {
      header = "col_" + std::to_string(col + 1);
    }
    out.headers[col] = std::move(header);
  }
  out.header_keys = unique_header_keys(out.headers);

  if (has_header && !out.rect_rows.empty() && apply_header_normalize) {
    out.rect_rows[0] = out.headers;
  }

  if (options.format != Query::TableOptions::Format::Sparse) {
    return out;
  }

  const size_t data_start = (has_header && !out.rect_rows.empty()) ? 1 : 0;
  for (size_t row_idx = data_start; row_idx < kept_rows.size(); ++row_idx) {
    const auto& original = kept_rows[row_idx];
    if (options.sparse_shape == Query::TableOptions::SparseShape::Long) {
      for (size_t col_pos = 0; col_pos < keep_cols.size(); ++col_pos) {
        const size_t source_col = keep_cols[col_pos];
        if (table_cell_empty(original, source_col, options.empty_is)) continue;
        std::vector<std::string> out_row;
        out_row.reserve(has_header ? 4 : 3);
        out_row.push_back(std::to_string((row_idx - data_start) + 1));
        out_row.push_back(std::to_string(col_pos + 1));
        if (has_header) {
          out_row.push_back(out.headers[col_pos]);
        }
        if (source_col < original.size()) {
          out_row.push_back(original[source_col]);
        } else {
          out_row.emplace_back();
        }
        out.sparse_long_rows.push_back(std::move(out_row));
      }
      continue;
    }
    std::vector<std::pair<std::string, std::string>> sparse_row;
    sparse_row.reserve(keep_cols.size());
    for (size_t col_pos = 0; col_pos < keep_cols.size(); ++col_pos) {
      const size_t source_col = keep_cols[col_pos];
      if (table_cell_empty(original, source_col, options.empty_is)) continue;
      std::string key =
          has_header ? out.header_keys[col_pos] : ("col_" + std::to_string(col_pos + 1));
      std::string value = (source_col < original.size()) ? original[source_col] : std::string{};
      sparse_row.emplace_back(std::move(key), std::move(value));
    }
    out.sparse_wide_rows.push_back(std::move(sparse_row));
  }
  return out;
}

}  // namespace

QueryResult::TableOptions to_result_table_options(const Query::TableOptions& options) {
  QueryResult::TableOptions out;
  out.trim_empty_rows = options.trim_empty_rows;
  out.stop_after_empty_rows = options.stop_after_empty_rows;
  out.header_normalize = options.header_normalize;
  out.header_normalize_explicit = options.header_normalize_explicit;
  if (options.trim_empty_cols == Query::TableOptions::TrimEmptyCols::Off) {
    out.trim_empty_cols = QueryResult::TableOptions::TrimEmptyCols::Off;
  } else if (options.trim_empty_cols == Query::TableOptions::TrimEmptyCols::Trailing) {
    out.trim_empty_cols = QueryResult::TableOptions::TrimEmptyCols::Trailing;
  } else {
    out.trim_empty_cols = QueryResult::TableOptions::TrimEmptyCols::All;
  }
  if (options.empty_is == Query::TableOptions::EmptyIs::BlankOrNull) {
    out.empty_is = QueryResult::TableOptions::EmptyIs::BlankOrNull;
  } else if (options.empty_is == Query::TableOptions::EmptyIs::NullOnly) {
    out.empty_is = QueryResult::TableOptions::EmptyIs::NullOnly;
  } else {
    out.empty_is = QueryResult::TableOptions::EmptyIs::BlankOnly;
  }
  if (options.format == Query::TableOptions::Format::Rect) {
    out.format = QueryResult::TableOptions::Format::Rect;
  } else {
    out.format = QueryResult::TableOptions::Format::Sparse;
  }
  if (options.sparse_shape == Query::TableOptions::SparseShape::Long) {
    out.sparse_shape = QueryResult::TableOptions::SparseShape::Long;
  } else {
    out.sparse_shape = QueryResult::TableOptions::SparseShape::Wide;
  }
  return out;
}

bool table_uses_default_output(const Query& query) {
  return query.table_options.format == Query::TableOptions::Format::Rect &&
         !query.table_options.trim_empty_rows &&
         query.table_options.trim_empty_cols == Query::TableOptions::TrimEmptyCols::Off &&
         query.table_options.empty_is == Query::TableOptions::EmptyIs::BlankOrNull &&
         query.table_options.stop_after_empty_rows == 0 &&
         !query.table_options.header_normalize_explicit;
}

void materialize_table_result(const std::vector<std::vector<std::string>>& raw_rows,
                              bool has_header, const Query::TableOptions& options,
                              QueryResult::TableResult& table) {
  MaterializedTable materialized = materialize_table(raw_rows, has_header, options);
  table.headers = std::move(materialized.headers);
  table.header_keys = std::move(materialized.header_keys);
  if (options.format == Query::TableOptions::Format::Sparse) {
    if (options.sparse_shape == Query::TableOptions::SparseShape::Long) {
      table.rows = std::move(materialized.sparse_long_rows);
    } else {
      table.rows.clear();
      table.sparse_wide_rows = std::move(materialized.sparse_wide_rows);
    }
  } else {
    table.rows = std::move(materialized.rect_rows);
  }
}

}  // namespace markql
