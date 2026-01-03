#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "xsql/xsql.h"

xsql::QueryResult run_query(const std::string& html, const std::string& query);
xsql::QueryResult make_result(const std::vector<std::string>& columns,
                              const std::vector<std::vector<std::string>>& values);
std::string read_file_to_string(const std::filesystem::path& path);
