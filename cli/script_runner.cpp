#include "script_runner.h"

#include <stdexcept>

#include "cli_utils.h"
#include "xsql/diagnostics.h"
#include "lang/markql_parser.h"
#include "lang/parser/lexer.h"

namespace xsql::cli {

ScriptSplitResult split_sql_script(const std::string& script) {
  ScriptSplitResult out;
  xsql::Lexer lexer(script);
  bool in_statement = false;
  size_t statement_start = 0;

  while (true) {
    Token token = lexer.next();
    if (token.type == TokenType::Invalid) {
      out.error_message = token.text;
      out.error_position = token.pos;
      return out;
    }
    if (token.type == TokenType::End) {
      if (in_statement) {
        out.statements.push_back(
            ScriptStatement{script.substr(statement_start), statement_start});
      }
      return out;
    }
    if (token.type == TokenType::Semicolon) {
      if (in_statement) {
        out.statements.push_back(
            ScriptStatement{script.substr(statement_start, token.pos - statement_start),
                            statement_start});
        in_statement = false;
      }
      continue;
    }
    if (!in_statement) {
      in_statement = true;
      statement_start = token.pos;
    }
  }
}

int run_sql_script(const std::string& script,
                   const ScriptRunOptions& options,
                   const ScriptExecutor& execute_statement,
                   std::ostream& out,
                   std::ostream& err) {
  ScriptSplitResult split = split_sql_script(script);
  if (split.error_message.has_value()) {
    auto [line, col] = line_col_from_offset(script, split.error_position);
    err << "Error: " << *split.error_message
        << " at line " << line << ", column " << col << "\n";
    return 1;
  }
  if (split.statements.empty()) {
    return 0;
  }

  bool had_error = false;
  const size_t total = split.statements.size();
  for (size_t i = 0; i < total; ++i) {
    const ScriptStatement& statement = split.statements[i];
    const size_t statement_index = i + 1;
    if (!options.quiet) {
      out << "== stmt " << statement_index << "/" << total << " ==\n";
    }

    auto parsed = xsql::parse_query(statement.text);
    if (!parsed.query.has_value()) {
      size_t error_pos = statement.start_pos;
      if (parsed.error.has_value()) {
        error_pos += parsed.error->position;
      }
      auto [line, col] = line_col_from_offset(script, error_pos);
      err << "Error: statement " << statement_index << "/" << total
          << " at line " << line << ", column " << col << "\n";
      const std::string parse_message =
          parsed.error.has_value() ? parsed.error->message : "Query parse error";
      const size_t parse_pos = parsed.error.has_value() ? parsed.error->position : 0;
      std::vector<xsql::Diagnostic> diagnostics;
      diagnostics.push_back(xsql::make_syntax_diagnostic(statement.text, parse_message, parse_pos));
      err << xsql::render_diagnostics_text(diagnostics) << "\n";
      had_error = true;
      if (!options.continue_on_error) return 1;
      continue;
    }

    try {
      execute_statement(statement.text);
    } catch (const std::exception& ex) {
      auto [line, col] = line_col_from_offset(script, statement.start_pos);
      err << "Error: statement " << statement_index << "/" << total
          << " at line " << line << ", column " << col << "\n";
      std::vector<xsql::Diagnostic> diagnostics =
          xsql::diagnose_query_failure(statement.text, ex.what());
      if (!diagnostics.empty()) {
        err << xsql::render_diagnostics_text(diagnostics) << "\n";
      } else {
        err << ex.what() << "\n";
      }
      had_error = true;
      if (!options.continue_on_error) return 1;
    }
  }

  return had_error ? 1 : 0;
}

}  // namespace xsql::cli
