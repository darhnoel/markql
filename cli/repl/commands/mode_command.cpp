#include "mode_command.h"

#include <iostream>
#include <sstream>

#include "../../cli_args.h"

namespace xsql::cli {

CommandHandler make_mode_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    if (line.rfind(".mode", 0) != 0) {
      return false;
    }
    std::istringstream iss(line);
    std::string cmd;
    std::string mode;
    iss >> cmd >> mode;
    if (is_supported_output_mode(mode)) {
      ctx.config.output_mode = mode;
      std::cout << "Output mode: " << ctx.config.output_mode << std::endl;
    } else {
      std::cerr << "Usage: .mode duckbox|json|plain|csv" << std::endl;
    }
    return true;
  };
}

}  // namespace xsql::cli
