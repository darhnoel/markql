#include "cli_utils.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#ifdef MARKQL_USE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif

namespace markql::cli {

namespace {

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 4);
  for (char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

#ifndef MARKQL_USE_NLOHMANN_JSON
std::string attributes_to_json(const markql::QueryResultRow& row) {
  std::string out = "{";
  size_t count = 0;
  for (const auto& kv : row.attributes) {
    if (count++ > 0) out += ",";
    out += "\"";
    out += json_escape(kv.first);
    out += "\":\"";
    out += json_escape(kv.second);
    out += "\"";
  }
  out += "}";
  return out;
}

std::string terms_score_to_json(const markql::QueryResultRow& row) {
  std::vector<std::pair<std::string, double>> items(row.term_scores.begin(), row.term_scores.end());
  std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) return a.first < b.first;
    return a.second < b.second;
  });
  std::ostringstream oss;
  oss << "{";
  oss << std::fixed << std::setprecision(6);
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0) oss << ",";
    oss << "\"" << json_escape(items[i].first) << "\":" << items[i].second;
  }
  oss << "}";
  return oss.str();
}
#endif

void print_field(std::ostream& os, const std::string& field, const markql::QueryResultRow& row) {
  if (field == "node_id") {
    os << row.node_id;
  } else if (field == "count") {
    os << row.node_id;
  } else if (field == "tag") {
    os << "\"" << json_escape(row.tag) << "\"";
  } else if (field == "text") {
    os << "\"" << json_escape(row.text) << "\"";
  } else if (field == "inner_html") {
    os << "\"" << json_escape(row.inner_html) << "\"";
  } else if (field == "parent_id") {
    if (row.parent_id.has_value()) {
      os << *row.parent_id;
    } else {
      os << "null";
    }
  } else if (field == "sibling_pos") {
    os << row.sibling_pos;
  } else if (field == "max_depth") {
    os << row.max_depth;
  } else if (field == "doc_order") {
    os << row.doc_order;
  } else if (field == "source_uri") {
    os << "\"" << json_escape(row.source_uri) << "\"";
  } else if (field == "attributes") {
#ifndef MARKQL_USE_NLOHMANN_JSON
    os << attributes_to_json(row);
#else
    os << "null";
#endif
  } else if (field == "terms_score") {
#ifndef MARKQL_USE_NLOHMANN_JSON
    os << terms_score_to_json(row);
#else
    os << "null";
#endif
  } else {
    auto computed = row.computed_fields.find(field);
    if (computed != row.computed_fields.end()) {
      os << "\"" << json_escape(computed->second) << "\"";
    } else {
      auto it = row.attributes.find(field);
      if (it != row.attributes.end()) {
        os << "\"" << json_escape(it->second) << "\"";
      } else {
        os << "null";
      }
    }
  }
}

}  // namespace

std::string build_json(const markql::QueryResult& result, markql::ColumnNameMode colname_mode) {
#ifdef MARKQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  std::vector<std::string> raw_columns = result.columns;
  if (raw_columns.empty()) {
    raw_columns = {"node_id", "tag", "attributes", "parent_id", "max_depth", "doc_order"};
  }
  std::vector<markql::ColumnNameMapping> schema =
      markql::build_column_name_map(raw_columns, colname_mode);
  json out = json::array();
  for (const auto& row : result.rows) {
    json obj = json::object();
    for (const auto& entry : schema) {
      const std::string& raw_field = entry.raw_name;
      const std::string& output_field = entry.output_name;
      if (raw_field == "node_id") {
        obj[output_field] = row.node_id;
      } else if (raw_field == "count") {
        obj[output_field] = row.node_id;
      } else if (raw_field == "tag") {
        obj[output_field] = row.tag;
      } else if (raw_field == "text") {
        obj[output_field] = row.text;
      } else if (raw_field == "inner_html") {
        obj[output_field] = row.inner_html;
      } else if (raw_field == "parent_id") {
        obj[output_field] = row.parent_id.has_value() ? json(*row.parent_id) : json(nullptr);
      } else if (raw_field == "max_depth") {
        obj[output_field] = row.max_depth;
      } else if (raw_field == "doc_order") {
        obj[output_field] = row.doc_order;
      } else if (raw_field == "source_uri") {
        obj[output_field] = row.source_uri;
      } else if (raw_field == "attributes") {
        json attrs = json::object();
        for (const auto& kv : row.attributes) {
          attrs[kv.first] = kv.second;
        }
        obj[output_field] = attrs;
      } else if (raw_field == "terms_score") {
        json scores = json::object();
        for (const auto& kv : row.term_scores) {
          scores[kv.first] = kv.second;
        }
        obj[output_field] = scores;
      } else {
        auto computed = row.computed_fields.find(raw_field);
        if (computed != row.computed_fields.end()) {
          obj[output_field] = computed->second;
        } else {
          auto it = row.attributes.find(raw_field);
          obj[output_field] = (it != row.attributes.end()) ? json(it->second) : json(nullptr);
        }
      }
    }
    out.push_back(obj);
  }
  return out.dump(2);
#else
  std::vector<std::string> raw_columns = result.columns;
  if (raw_columns.empty()) {
    raw_columns = {"node_id", "tag", "attributes", "parent_id", "max_depth", "doc_order"};
  }
  std::vector<markql::ColumnNameMapping> schema =
      markql::build_column_name_map(raw_columns, colname_mode);
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < result.rows.size(); ++i) {
    const auto& row = result.rows[i];
    if (i > 0) oss << ",";
    oss << "{";
    for (size_t c = 0; c < schema.size(); ++c) {
      if (c > 0) oss << ",";
      oss << "\"" << json_escape(schema[c].output_name) << "\":";
      print_field(oss, schema[c].raw_name, row);
    }
    oss << "}";
  }
  oss << "]";
  return oss.str();
#endif
}

std::string build_json_list(const markql::QueryResult& result,
                            markql::ColumnNameMode colname_mode) {
#ifdef MARKQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  std::vector<std::string> raw_columns = result.columns;
  if (raw_columns.size() != 1) {
    throw std::runtime_error("TO LIST() requires a single projected column");
  }
  std::vector<markql::ColumnNameMapping> schema =
      markql::build_column_name_map(raw_columns, colname_mode);
  const std::string& field = schema[0].raw_name;
  json out = json::array();
  for (const auto& row : result.rows) {
    if (field == "node_id") {
      out.push_back(row.node_id);
    } else if (field == "count") {
      out.push_back(row.node_id);
    } else if (field == "tag") {
      out.push_back(row.tag);
    } else if (field == "text") {
      out.push_back(row.text);
    } else if (field == "inner_html") {
      out.push_back(row.inner_html);
    } else if (field == "parent_id") {
      out.push_back(row.parent_id.has_value() ? json(*row.parent_id) : json(nullptr));
    } else if (field == "max_depth") {
      out.push_back(row.max_depth);
    } else if (field == "doc_order") {
      out.push_back(row.doc_order);
    } else if (field == "source_uri") {
      out.push_back(row.source_uri);
    } else if (field == "attributes") {
      json attrs = json::object();
      for (const auto& kv : row.attributes) {
        attrs[kv.first] = kv.second;
      }
      out.push_back(attrs);
    } else {
      auto computed = row.computed_fields.find(field);
      if (computed != row.computed_fields.end()) {
        out.push_back(computed->second);
      } else {
        auto it = row.attributes.find(field);
        out.push_back((it != row.attributes.end()) ? json(it->second) : json(nullptr));
      }
    }
  }
  return out.dump(2);
#else
  std::vector<std::string> raw_columns = result.columns;
  if (raw_columns.size() != 1) {
    throw std::runtime_error("TO LIST() requires a single projected column");
  }
  std::vector<markql::ColumnNameMapping> schema =
      markql::build_column_name_map(raw_columns, colname_mode);
  const std::string& field = schema[0].raw_name;
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < result.rows.size(); ++i) {
    const auto& row = result.rows[i];
    if (i > 0) oss << ",";
    print_field(oss, field, row);
  }
  oss << "]";
  return oss.str();
#endif
}

std::string build_table_json(const markql::QueryResult& result) {
  const bool sparse =
      result.table_options.format == markql::QueryResult::TableOptions::Format::Sparse;
  const bool sparse_long =
      result.table_options.sparse_shape == markql::QueryResult::TableOptions::SparseShape::Long;
#ifdef MARKQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  auto parse_index_or_string = [](const std::string& text) -> json {
    try {
      size_t idx = 0;
      int64_t value = std::stoll(text, &idx);
      if (idx == text.size()) {
        return value;
      }
    } catch (...) {
    }
    return text;
  };
  auto sparse_long_rows_json = [&](const markql::QueryResult::TableResult& table) -> json {
    json rows = json::array();
    const bool include_header = result.table_has_header;
    const size_t expected = include_header ? 4 : 3;
    for (const auto& row : table.rows) {
      if (row.size() < expected) continue;
      json obj = json::object();
      obj["row_index"] = parse_index_or_string(row[0]);
      obj["col_index"] = parse_index_or_string(row[1]);
      if (include_header) {
        obj["header"] = row[2];
        obj["value"] = row[3];
      } else {
        obj["value"] = row[2];
      }
      rows.push_back(std::move(obj));
    }
    return rows;
  };
  auto sparse_wide_rows_json = [](const markql::QueryResult::TableResult& table) -> json {
    json rows = json::array();
    for (const auto& row : table.sparse_wide_rows) {
      json obj = json::object();
      for (const auto& kv : row) {
        obj[kv.first] = kv.second;
      }
      rows.push_back(std::move(obj));
    }
    return rows;
  };
  if (sparse && result.tables.size() == 1) {
    if (sparse_long) return sparse_long_rows_json(result.tables[0]).dump(2);
    return sparse_wide_rows_json(result.tables[0]).dump(2);
  }
  if (sparse) {
    json out = json::array();
    for (const auto& table : result.tables) {
      json rows = sparse_long ? sparse_long_rows_json(table) : sparse_wide_rows_json(table);
      out.push_back({{"node_id", table.node_id}, {"rows", rows}});
    }
    return out.dump(2);
  }
  if (result.tables.size() == 1) {
    json rows = json::array();
    for (const auto& row : result.tables[0].rows) {
      rows.push_back(row);
    }
    return rows.dump(2);
  }
  json out = json::array();
  for (const auto& table : result.tables) {
    json rows = json::array();
    for (const auto& row : table.rows) {
      rows.push_back(row);
    }
    out.push_back({{"node_id", table.node_id}, {"rows", rows}});
  }
  return out.dump(2);
#else
  auto sparse_long_rows_json = [&](const markql::QueryResult::TableResult& table) -> std::string {
    std::ostringstream oss;
    oss << "[";
    const bool include_header = result.table_has_header;
    const size_t expected = include_header ? 4 : 3;
    bool first = true;
    for (const auto& row : table.rows) {
      if (row.size() < expected) continue;
      if (!first) oss << ",";
      first = false;
      oss << "{\"row_index\":";
      bool numeric_row =
          !row[0].empty() && std::all_of(row[0].begin(), row[0].end(),
                                         [](unsigned char c) { return std::isdigit(c) != 0; });
      if (numeric_row)
        oss << row[0];
      else
        oss << "\"" << json_escape(row[0]) << "\"";
      oss << ",\"col_index\":";
      bool numeric_col =
          !row[1].empty() && std::all_of(row[1].begin(), row[1].end(),
                                         [](unsigned char c) { return std::isdigit(c) != 0; });
      if (numeric_col)
        oss << row[1];
      else
        oss << "\"" << json_escape(row[1]) << "\"";
      if (include_header) {
        oss << ",\"header\":\"" << json_escape(row[2]) << "\"";
        oss << ",\"value\":\"" << json_escape(row[3]) << "\"";
      } else {
        oss << ",\"value\":\"" << json_escape(row[2]) << "\"";
      }
      oss << "}";
    }
    oss << "]";
    return oss.str();
  };
  auto sparse_wide_rows_json = [](const markql::QueryResult::TableResult& table) -> std::string {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < table.sparse_wide_rows.size(); ++i) {
      if (i > 0) oss << ",";
      oss << "{";
      const auto& row = table.sparse_wide_rows[i];
      for (size_t j = 0; j < row.size(); ++j) {
        if (j > 0) oss << ",";
        oss << "\"" << json_escape(row[j].first) << "\":" << "\"" << json_escape(row[j].second)
            << "\"";
      }
      oss << "}";
    }
    oss << "]";
    return oss.str();
  };
  if (sparse && result.tables.size() == 1) {
    if (sparse_long) return sparse_long_rows_json(result.tables[0]);
    return sparse_wide_rows_json(result.tables[0]);
  }
  if (sparse) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < result.tables.size(); ++i) {
      if (i > 0) oss << ",";
      oss << "{\"node_id\":" << result.tables[i].node_id << ",\"rows\":";
      if (sparse_long) {
        oss << sparse_long_rows_json(result.tables[i]);
      } else {
        oss << sparse_wide_rows_json(result.tables[i]);
      }
      oss << "}";
    }
    oss << "]";
    return oss.str();
  }
  std::ostringstream oss;
  if (result.tables.size() == 1) {
    oss << "[";
    const auto& rows = result.tables[0].rows;
    for (size_t i = 0; i < rows.size(); ++i) {
      if (i > 0) oss << ",";
      oss << "[";
      for (size_t j = 0; j < rows[i].size(); ++j) {
        if (j > 0) oss << ",";
        oss << "\"" << json_escape(rows[i][j]) << "\"";
      }
      oss << "]";
    }
    oss << "]";
    return oss.str();
  }
  oss << "[";
  for (size_t i = 0; i < result.tables.size(); ++i) {
    if (i > 0) oss << ",";
    oss << "{\"node_id\":" << result.tables[i].node_id << ",\"rows\":[";
    const auto& rows = result.tables[i].rows;
    for (size_t r = 0; r < rows.size(); ++r) {
      if (r > 0) oss << ",";
      oss << "[";
      for (size_t c = 0; c < rows[r].size(); ++c) {
        if (c > 0) oss << ",";
        oss << "\"" << json_escape(rows[r][c]) << "\"";
      }
      oss << "]";
    }
    oss << "]}";
  }
  oss << "]";
  return oss.str();
#endif
}

std::string build_summary_json(const std::vector<std::pair<std::string, size_t>>& summary) {
#ifdef MARKQL_USE_NLOHMANN_JSON
  using nlohmann::json;
  json out = json::array();
  for (const auto& item : summary) {
    out.push_back({{"tag", item.first}, {"count", item.second}});
  }
  return out.dump(2);
#else
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < summary.size(); ++i) {
    if (i > 0) oss << ",";
    oss << "{\"tag\":\"" << json_escape(summary[i].first) << "\",\"count\":" << summary[i].second
        << "}";
  }
  oss << "]";
  return oss.str();
#endif
}

}  // namespace markql::cli
