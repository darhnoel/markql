#pragma once

#include <functional>
#include <ostream>
#include <string>

#include "registry.h"

namespace xsql::cli {

using ExploreRunner = std::function<int(const std::string&, std::ostream&)>;

CommandHandler make_explore_command();
CommandHandler make_explore_command_with_runner(ExploreRunner runner);

}  // namespace xsql::cli
