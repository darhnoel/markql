#include "set_command.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

#include "../../cli_utils.h"

namespace xsql::cli {

namespace {

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

}  // namespace

CommandHandler make_set_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    if (line.rfind(".set", 0) != 0) {
      return false;
    }
    std::string value = trim_semicolon(line);
    std::istringstream iss(value);
    std::string cmd;
    std::string key;
    std::string setting;
    std::string extra;
    iss >> cmd >> key >> setting >> extra;
    if (to_lower(key) != "colnames" || setting.empty() || !extra.empty()) {
      std::cerr << "Usage: .set colnames raw|normalize" << std::endl;
      return true;
    }
    std::string mode = to_lower(setting);
    if (mode == "raw") {
      ctx.config.colname_mode = xsql::ColumnNameMode::Raw;
      std::cout << "Column names: raw" << std::endl;
      return true;
    }
    if (mode == "normalize") {
      ctx.config.colname_mode = xsql::ColumnNameMode::Normalize;
      std::cout << "Column names: normalize" << std::endl;
      return true;
    }
    std::cerr << "Usage: .set colnames raw|normalize" << std::endl;
    return true;
  };
}

}  // namespace xsql::cli
