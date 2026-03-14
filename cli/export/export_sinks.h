#pragma once

#include <ostream>
#include <string>

#include "markql/column_names.h"
#include "markql/markql.h"

namespace markql::cli {

bool export_result(const markql::QueryResult& result,
                   std::string& error,
                   markql::ColumnNameMode colname_mode = markql::ColumnNameMode::Normalize);
bool write_csv(std::ostream& out,
               const markql::QueryResult& result,
               std::string& error,
               markql::ColumnNameMode colname_mode = markql::ColumnNameMode::Normalize);
bool write_csv(const markql::QueryResult& result,
               const std::string& path,
               std::string& error,
               markql::ColumnNameMode colname_mode = markql::ColumnNameMode::Normalize);
bool write_json(const markql::QueryResult& result,
                const std::string& path,
                std::string& error,
                markql::ColumnNameMode colname_mode = markql::ColumnNameMode::Normalize);
bool write_ndjson(const markql::QueryResult& result,
                  const std::string& path,
                  std::string& error,
                  markql::ColumnNameMode colname_mode = markql::ColumnNameMode::Normalize);
bool write_parquet(const markql::QueryResult& result,
                   const std::string& path,
                   std::string& error,
                   markql::ColumnNameMode colname_mode = markql::ColumnNameMode::Normalize);
bool write_table_csv(const markql::QueryResult::TableResult& table,
                     const std::string& path,
                     std::string& error,
                     bool table_has_header);
bool write_table_parquet(const markql::QueryResult::TableResult& table,
                         const std::string& path,
                         std::string& error);

}  // namespace markql::cli
