#include "xsql/column_names.h"

#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace xsql {

namespace {

std::string trim_copy(const std::string& value) {
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

bool is_reserved_keyword(const std::string& value) {
  static const std::unordered_set<std::string> kReserved = {
      "select", "from", "where", "group", "order", "join", "limit"};
  return kReserved.find(value) != kReserved.end();
}

}  // namespace

std::string normalize_colname(const std::string& raw, bool lowercase) {
  std::string value = trim_copy(raw);
  if (lowercase) {
    for (char& c : value) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }

  std::string normalized;
  normalized.reserve(value.size());
  bool last_was_underscore = false;
  for (char c : value) {
    const unsigned char uc = static_cast<unsigned char>(c);
    const bool is_ident =
        std::isalnum(uc) ||
        c == '_';
    if (is_ident) {
      normalized.push_back(c);
      last_was_underscore = false;
      continue;
    }
    if (!last_was_underscore) {
      normalized.push_back('_');
      last_was_underscore = true;
    }
  }

  std::string collapsed;
  collapsed.reserve(normalized.size());
  last_was_underscore = false;
  for (char c : normalized) {
    if (c == '_') {
      if (!last_was_underscore) {
        collapsed.push_back(c);
      }
      last_was_underscore = true;
      continue;
    }
    collapsed.push_back(c);
    last_was_underscore = false;
  }

  size_t start = 0;
  while (start < collapsed.size() && collapsed[start] == '_') {
    ++start;
  }
  size_t end = collapsed.size();
  while (end > start && collapsed[end - 1] == '_') {
    --end;
  }
  collapsed = collapsed.substr(start, end - start);

  if (collapsed.empty()) {
    collapsed = "col";
  }
  if (!collapsed.empty() &&
      std::isdigit(static_cast<unsigned char>(collapsed[0]))) {
    collapsed = "c_" + collapsed;
  }
  if (is_reserved_keyword(collapsed)) {
    collapsed.push_back('_');
  }
  return collapsed;
}

std::vector<ColumnNameMapping> build_column_name_map(
    const std::vector<std::string>& raw_columns,
    ColumnNameMode mode,
    bool lowercase) {
  std::vector<ColumnNameMapping> out;
  out.reserve(raw_columns.size());
  std::unordered_map<std::string, size_t> seen;
  for (const auto& raw : raw_columns) {
    std::string base =
        mode == ColumnNameMode::Normalize ? normalize_colname(raw, lowercase)
                                          : trim_copy(raw);
    // WHY: empty/blank headers break downstream tabular tools.
    if (base.empty()) {
      base = "col";
    }
    size_t count = ++seen[base];
    if (count > 1) {
      base += "__" + std::to_string(count);
    }
    out.push_back(ColumnNameMapping{raw, base});
  }
  return out;
}

}  // namespace xsql
