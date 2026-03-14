#include "lint_command.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>

#include "../../cli_utils.h"

namespace markql::cli {

namespace {

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

void print_usage() {
  std::cerr << "Usage: .lint on|off or :lint on|off" << std::endl;
}

}  // namespace

CommandHandler make_lint_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    if (line.rfind(".lint", 0) != 0 && line.rfind(":lint", 0) != 0) {
      return false;
    }

    std::string normalized = trim_semicolon(line);
    std::istringstream iss(normalized);
    std::string cmd;
    std::string mode;
    std::string extra;
    iss >> cmd >> mode >> extra;

    if (mode.empty()) {
      std::cout << "Lint warnings: " << (ctx.config.lint_warnings ? "on" : "off") << std::endl;
      return true;
    }

    if (!extra.empty()) {
      print_usage();
      return true;
    }

    mode = to_lower(mode);
    if (mode == "on") {
      ctx.config.lint_warnings = true;
      std::cout << "Lint warnings: on" << std::endl;
      return true;
    }
    if (mode == "off") {
      ctx.config.lint_warnings = false;
      std::cout << "Lint warnings: off" << std::endl;
      return true;
    }

    print_usage();
    return true;
  };
}

}  // namespace markql::cli
