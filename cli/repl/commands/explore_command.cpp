#include "explore_command.h"

#include <cctype>
#include <iostream>
#include <vector>

#include "../../cli_utils.h"
#include "../../explore/dom_explorer.h"
#include "../../ui/color.h"

namespace xsql::cli {

namespace {

std::vector<std::string> split_args(const std::string& line, std::string& error) {
  std::vector<std::string> out;
  std::string current;
  bool in_single = false;
  bool in_double = false;
  for (size_t i = 0; i < line.size(); ++i) {
    unsigned char ch = static_cast<unsigned char>(line[i]);
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (!in_single && !in_double && std::isspace(ch)) {
      if (!current.empty()) {
        out.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(static_cast<char>(ch));
  }
  if (in_single || in_double) {
    error = "Error: unterminated quoted input in .explore";
    return {};
  }
  if (!current.empty()) out.push_back(current);
  return out;
}

void print_usage() {
  std::cerr << "Usage: .explore [doc|alias|path|url]" << std::endl;
}

}  // namespace

CommandHandler make_explore_command() {
  return make_explore_command_with_runner(
      [](const std::string& input, std::ostream& err) {
        return run_dom_explorer_from_input(input, err);
      });
}

CommandHandler make_explore_command_with_runner(ExploreRunner runner) {
  return [runner = std::move(runner)](const std::string& line, CommandContext& ctx) -> bool {
    if (line.rfind(".explore", 0) != 0 && line.rfind(":explore", 0) != 0) {
      return false;
    }

    std::string error;
    auto args = split_args(trim_semicolon(line), error);
    if (!error.empty()) {
      std::cerr << error << std::endl;
      return true;
    }
    if (args.empty() || args.size() > 2) {
      print_usage();
      return true;
    }

    bool use_alias = false;
    std::string alias;
    std::string target;
    if (args.size() == 1 || args[1] == "doc" || args[1] == "document") {
      alias = ctx.active_alias;
      use_alias = true;
    } else if (ctx.sources.find(args[1]) != ctx.sources.end()) {
      alias = args[1];
      use_alias = true;
    } else {
      target = args[1];
    }

    if (use_alias) {
      auto it = ctx.sources.find(alias);
      if (it == ctx.sources.end() || it->second.source.empty()) {
        if (!alias.empty()) {
          std::cerr << "No input loaded for alias '" << alias
                    << "'. Use .load <path|url> --alias " << alias << "." << std::endl;
        } else {
          std::cerr << "No input loaded. Use .load <path|url> or start with --input <path|url>."
                    << std::endl;
        }
        return true;
      }
      target = it->second.source;
    }

    int code = runner(target, std::cerr);
    if (code != 0 && ctx.config.color) {
      std::cerr << kColor.reset;
    }
    ctx.editor.reset_render_state();
    return true;
  };
}

}  // namespace xsql::cli
