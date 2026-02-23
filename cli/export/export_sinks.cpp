#include "export/export_sinks.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef XSQL_USE_ARROW
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/writer.h>
#endif

namespace xsql::cli {

namespace {

std::string attributes_to_string(const std::unordered_map<std::string, std::string>& attrs) {
  if (attrs.empty()) return "{}";
  std::vector<std::string> keys;
  keys.reserve(attrs.size());
  for (const auto& kv : attrs) {
    keys.push_back(kv.first);
  }
  std::sort(keys.begin(), keys.end());
  std::ostringstream oss;
  oss << "{";
  for (size_t i = 0; i < keys.size(); ++i) {
    if (i > 0) oss << ",";
    const auto& key = keys[i];
    oss << key << "=" << attrs.at(key);
  }
  oss << "}";
  return oss.str();
}

struct CellValue {
  std::string value;
  bool is_null = false;
};

/// Serializes TFIDF term scores for CSV export cells.
std::string term_scores_to_string(const std::unordered_map<std::string, double>& scores) {
  if (scores.empty()) return "{}";
  std::vector<std::pair<std::string, double>> items(scores.begin(), scores.end());
  std::sort(items.begin(), items.end(),
            [](const auto& a, const auto& b) {
              if (a.first != b.first) return a.first < b.first;
              return a.second < b.second;
            });
  std::ostringstream oss;
  oss << "{";
  oss << std::fixed << std::setprecision(6);
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0) oss << ",";
    oss << "\"" << items[i].first << "\":" << items[i].second;
  }
  oss << "}";
  return oss.str();
}

CellValue field_value(const xsql::QueryResultRow& row, const std::string& field) {
  if (field == "node_id") return {std::to_string(row.node_id), false};
  if (field == "count") return {std::to_string(row.node_id), false};
  if (field == "tag") return {row.tag, false};
  if (field == "text") return {row.text, false};
  if (field == "inner_html") return {row.inner_html, false};
  if (field == "parent_id") {
    if (!row.parent_id.has_value()) return {"", true};
    return {std::to_string(*row.parent_id), false};
  }
  if (field == "max_depth") return {std::to_string(row.max_depth), false};
  if (field == "doc_order") return {std::to_string(row.doc_order), false};
  if (field == "source_uri") return {row.source_uri, false};
  if (field == "attributes") return {attributes_to_string(row.attributes), false};
  if (field == "terms_score") return {term_scores_to_string(row.term_scores), false};
  auto computed = row.computed_fields.find(field);
  if (computed != row.computed_fields.end()) return {computed->second, false};
  auto it = row.attributes.find(field);
  if (it == row.attributes.end()) return {"", true};
  return {it->second, false};
}

std::string csv_escape(const std::string& value) {
  bool needs_quotes = false;
  for (char c : value) {
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      needs_quotes = true;
      break;
    }
  }
  if (!needs_quotes) return value;
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for (char c : value) {
    if (c == '"') {
      out.push_back('"');
      out.push_back('"');
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
  return out;
}

std::string json_escape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '\"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          std::ostringstream hex;
          hex << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(static_cast<unsigned char>(c));
          out += hex.str();
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  return out;
}

void write_json_row(std::ostream& out,
                    const xsql::QueryResultRow& row,
                    const std::vector<xsql::ColumnNameMapping>& schema) {
  out << "{";
  for (size_t i = 0; i < schema.size(); ++i) {
    if (i > 0) out << ",";
    out << "\"" << json_escape(schema[i].output_name) << "\":";
    CellValue cell = field_value(row, schema[i].raw_name);
    if (cell.is_null) {
      out << "null";
    } else {
      out << "\"" << json_escape(cell.value) << "\"";
    }
  }
  out << "}";
}

bool validate_rectangular(const xsql::QueryResult& result, std::string& error) {
  if (result.to_table || !result.tables.empty()) {
    error = "TO CSV/PARQUET/JSON/NDJSON does not support TO TABLE() results";
    return false;
  }
  if (result.columns.empty()) {
    error = "Export requires a rectangular result with columns";
    return false;
  }
  return true;
}

std::vector<xsql::ColumnNameMapping> result_schema(const xsql::QueryResult& result,
                                                   xsql::ColumnNameMode colname_mode) {
  return xsql::build_column_name_map(result.columns, colname_mode);
}

}  // namespace

static std::vector<std::string> table_columns(const xsql::QueryResult::TableResult& table);

bool write_csv(const xsql::QueryResult& result,
               const std::string& path,
               std::string& error,
               xsql::ColumnNameMode colname_mode) {
  if (!validate_rectangular(result, error)) return false;
  std::vector<xsql::ColumnNameMapping> schema = result_schema(result, colname_mode);
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "Failed to open file for writing: " + path;
    return false;
  }
  for (size_t i = 0; i < schema.size(); ++i) {
    if (i > 0) out << ",";
    out << csv_escape(schema[i].output_name);
  }
  out << "\n";
  for (const auto& row : result.rows) {
    for (size_t i = 0; i < schema.size(); ++i) {
      if (i > 0) out << ",";
      CellValue cell = field_value(row, schema[i].raw_name);
      const std::string& value = cell.is_null ? "" : cell.value;
      out << csv_escape(value);
    }
    out << "\n";
  }
  return true;
}

bool write_json(const xsql::QueryResult& result,
                const std::string& path,
                std::string& error,
                xsql::ColumnNameMode colname_mode) {
  if (!validate_rectangular(result, error)) return false;
  std::vector<xsql::ColumnNameMapping> schema = result_schema(result, colname_mode);
  std::ofstream file;
  std::ostream* out = &std::cout;
  if (!path.empty()) {
    file.open(path, std::ios::binary);
    if (!file) {
      error = "Failed to open file for writing: " + path;
      return false;
    }
    out = &file;
  }
  // WHY: write delimiters incrementally so large results do not require buffering.
  *out << "[";
  bool first = true;
  for (const auto& row : result.rows) {
    if (!first) *out << ",";
    first = false;
    write_json_row(*out, row, schema);
  }
  *out << "]\n";
  return true;
}

bool write_ndjson(const xsql::QueryResult& result,
                  const std::string& path,
                  std::string& error,
                  xsql::ColumnNameMode colname_mode) {
  if (!validate_rectangular(result, error)) return false;
  std::vector<xsql::ColumnNameMapping> schema = result_schema(result, colname_mode);
  std::ofstream file;
  std::ostream* out = &std::cout;
  if (!path.empty()) {
    file.open(path, std::ios::binary);
    if (!file) {
      error = "Failed to open file for writing: " + path;
      return false;
    }
    out = &file;
  }
  for (const auto& row : result.rows) {
    write_json_row(*out, row, schema);
    *out << "\n";
  }
  return true;
}

bool write_table_csv(const xsql::QueryResult::TableResult& table,
                     const std::string& path,
                     std::string& error,
                     bool table_has_header) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "Failed to open file for writing: " + path;
    return false;
  }
  if (!table_has_header) {
    std::vector<std::string> cols = table_columns(table);
    if (!cols.empty()) {
      for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) out << ",";
        out << csv_escape(cols[i]);
      }
      out << "\n";
    }
  }
  for (const auto& row : table.rows) {
    for (size_t i = 0; i < row.size(); ++i) {
      if (i > 0) out << ",";
      out << csv_escape(row[i]);
    }
    out << "\n";
  }
  return true;
}

static std::vector<std::string> table_columns(const xsql::QueryResult::TableResult& table) {
  size_t max_cols = 0;
  for (const auto& row : table.rows) {
    if (row.size() > max_cols) {
      max_cols = row.size();
    }
  }
  std::vector<std::string> cols;
  cols.reserve(max_cols);
  for (size_t i = 0; i < max_cols; ++i) {
    cols.push_back("col" + std::to_string(i + 1));
  }
  return cols;
}

bool write_parquet(const xsql::QueryResult& result,
                   const std::string& path,
                   std::string& error,
                   xsql::ColumnNameMode colname_mode) {
  if (!validate_rectangular(result, error)) return false;
  std::vector<xsql::ColumnNameMapping> col_schema = result_schema(result, colname_mode);
#ifdef XSQL_USE_ARROW
  std::vector<std::shared_ptr<arrow::ArrayBuilder>> builders;
  builders.reserve(col_schema.size());
  for (size_t i = 0; i < col_schema.size(); ++i) {
    builders.push_back(std::make_shared<arrow::StringBuilder>());
  }
  for (const auto& row : result.rows) {
    for (size_t i = 0; i < col_schema.size(); ++i) {
      CellValue cell = field_value(row, col_schema[i].raw_name);
      auto builder = std::static_pointer_cast<arrow::StringBuilder>(builders[i]);
      if (cell.is_null) {
        auto st = builder->AppendNull();
        if (!st.ok()) {
          error = st.ToString();
          return false;
        }
      } else {
        auto st = builder->Append(cell.value);
        if (!st.ok()) {
          error = st.ToString();
          return false;
        }
      }
    }
  }
  std::vector<std::shared_ptr<arrow::Field>> fields;
  std::vector<std::shared_ptr<arrow::Array>> arrays;
  fields.reserve(col_schema.size());
  arrays.reserve(col_schema.size());
  for (size_t i = 0; i < col_schema.size(); ++i) {
    fields.push_back(arrow::field(col_schema[i].output_name, arrow::utf8(), true));
    auto builder = std::static_pointer_cast<arrow::StringBuilder>(builders[i]);
    std::shared_ptr<arrow::Array> array;
    auto st = builder->Finish(&array);
    if (!st.ok()) {
      error = st.ToString();
      return false;
    }
    arrays.push_back(array);
  }
  auto arrow_schema = arrow::schema(fields);
  auto table = arrow::Table::Make(arrow_schema, arrays);
  auto output_res = arrow::io::FileOutputStream::Open(path);
  if (!output_res.ok()) {
    error = output_res.status().ToString();
    return false;
  }
  auto output = *output_res;
  auto st = parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), output, 1024);
  if (!st.ok()) {
    error = st.ToString();
    return false;
  }
  return true;
#else
  (void)result;
  (void)path;
  (void)colname_mode;
  error = "TO PARQUET requires Apache Arrow feature";
  return false;
#endif
}

bool write_table_parquet(const xsql::QueryResult::TableResult& table,
                         const std::string& path,
                         std::string& error) {
#ifdef XSQL_USE_ARROW
  std::vector<std::string> cols = table_columns(table);
  if (cols.empty()) {
    error = "Table export has no rows";
    return false;
  }
  std::vector<std::shared_ptr<arrow::ArrayBuilder>> builders;
  builders.reserve(cols.size());
  for (size_t i = 0; i < cols.size(); ++i) {
    builders.push_back(std::make_shared<arrow::StringBuilder>());
  }
  for (const auto& row : table.rows) {
    for (size_t i = 0; i < cols.size(); ++i) {
      auto builder = std::static_pointer_cast<arrow::StringBuilder>(builders[i]);
      if (i < row.size()) {
        auto st = builder->Append(row[i]);
        if (!st.ok()) {
          error = st.ToString();
          return false;
        }
      } else {
        auto st = builder->AppendNull();
        if (!st.ok()) {
          error = st.ToString();
          return false;
        }
      }
    }
  }
  std::vector<std::shared_ptr<arrow::Field>> fields;
  std::vector<std::shared_ptr<arrow::Array>> arrays;
  fields.reserve(cols.size());
  arrays.reserve(cols.size());
  for (size_t i = 0; i < cols.size(); ++i) {
    fields.push_back(arrow::field(cols[i], arrow::utf8(), true));
    auto builder = std::static_pointer_cast<arrow::StringBuilder>(builders[i]);
    std::shared_ptr<arrow::Array> array;
    auto st = builder->Finish(&array);
    if (!st.ok()) {
      error = st.ToString();
      return false;
    }
    arrays.push_back(array);
  }
  auto schema = arrow::schema(fields);
  auto table_out = arrow::Table::Make(schema, arrays);
  auto output_res = arrow::io::FileOutputStream::Open(path);
  if (!output_res.ok()) {
    error = output_res.status().ToString();
    return false;
  }
  auto output = *output_res;
  auto st = parquet::arrow::WriteTable(*table_out, arrow::default_memory_pool(), output, 1024);
  if (!st.ok()) {
    error = st.ToString();
    return false;
  }
  return true;
#else
  (void)table;
  (void)path;
  error = "TO PARQUET requires Apache Arrow feature";
  return false;
#endif
}

bool export_result(const xsql::QueryResult& result,
                   std::string& error,
                   xsql::ColumnNameMode colname_mode) {
  if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::None) {
    return false;
  }
  if (!result.tables.empty()) {
    if (result.tables.size() != 1) {
      error = "Export requires a single table result; add a filter to select one table";
      return false;
    }
    const bool sparse = result.table_options.format == xsql::QueryResult::TableOptions::Format::Sparse;
    const bool sparse_long =
        result.table_options.sparse_shape == xsql::QueryResult::TableOptions::SparseShape::Long;
    if (sparse && !sparse_long) {
      error = "TO TABLE(FORMAT=SPARSE, SPARSE_SHAPE=WIDE) does not support EXPORT";
      return false;
    }
    if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Csv) {
      if (sparse && sparse_long) {
        xsql::QueryResult::TableResult table = result.tables[0];
        std::vector<std::string> header_row = {"row_index", "col_index"};
        if (result.table_has_header) {
          header_row.push_back("header");
        }
        header_row.push_back("value");
        table.rows.insert(table.rows.begin(), std::move(header_row));
        return write_table_csv(table, result.export_sink.path, error, true);
      }
      return write_table_csv(result.tables[0], result.export_sink.path, error,
                             result.table_has_header);
    }
    if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Parquet) {
      return write_table_parquet(result.tables[0], result.export_sink.path, error);
    }
    if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Json ||
        result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Ndjson) {
      error = "TO JSON/NDJSON does not support TO TABLE() results";
      return false;
    }
  }
  if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Csv) {
    return write_csv(result, result.export_sink.path, error, colname_mode);
  }
  if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Parquet) {
    return write_parquet(result, result.export_sink.path, error, colname_mode);
  }
  if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Json) {
    return write_json(result, result.export_sink.path, error, colname_mode);
  }
  if (result.export_sink.kind == xsql::QueryResult::ExportSink::Kind::Ndjson) {
    return write_ndjson(result, result.export_sink.path, error, colname_mode);
  }
  error = "Unknown export sink";
  return false;
}

}  // namespace xsql::cli
