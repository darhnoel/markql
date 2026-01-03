#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xsql {

struct QueryResultRow {
  int64_t node_id = 0;
  std::string tag;
  std::string text;
  std::string inner_html;
  std::unordered_map<std::string, std::string> attributes;
  std::optional<int64_t> parent_id;
  std::string source_uri;
};

struct QueryResult {
  struct ExportSink {
    enum class Kind { None, Csv, Parquet } kind = Kind::None;
    std::string path;
  };
  std::vector<std::string> columns;
  std::vector<QueryResultRow> rows;
  bool to_list = false;
  struct TableResult {
    int64_t node_id = 0;
    std::vector<std::vector<std::string>> rows;
  };
  std::vector<TableResult> tables;
  bool to_table = false;
  ExportSink export_sink;
};

QueryResult execute_query_from_document(const std::string& html, const std::string& query);
QueryResult execute_query_from_file(const std::string& path, const std::string& query);
QueryResult execute_query_from_url(const std::string& url, const std::string& query, int timeout_ms);

}  // namespace xsql
