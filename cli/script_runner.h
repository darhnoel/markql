#pragma once

#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace xsql::cli {

struct ScriptStatement {
  std::string text;
  size_t start_pos = 0;
};

struct ScriptSplitResult {
  std::vector<ScriptStatement> statements;
  std::optional<std::string> error_message;
  size_t error_position = 0;
};

struct ScriptRunOptions {
  bool continue_on_error = false;
  bool quiet = false;
};

using ScriptExecutor = std::function<void(const std::string&)>;

/// Splits an SQL script into executable statements.
/// MUST ignore empty statements and preserve statement start offsets.
ScriptSplitResult split_sql_script(const std::string& script);
/// Executes script statements sequentially using the existing query executor callback.
/// MUST stop on first error unless continue_on_error is enabled.
int run_sql_script(const std::string& script,
                   const ScriptRunOptions& options,
                   const ScriptExecutor& execute_statement,
                   std::ostream& out,
                   std::ostream& err);

}  // namespace xsql::cli
