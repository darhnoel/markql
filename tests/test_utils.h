#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "markql/markql.h"

markql::QueryResult run_query(const std::string& html, const std::string& query);
markql::QueryResult make_result(const std::vector<std::string>& columns,
                                const std::vector<std::vector<std::string>>& values);
std::string read_file_to_string(const std::filesystem::path& path);
