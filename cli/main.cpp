#include <iostream>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "xsql/xsql.h"
#include "export/export_sinks.h"
#include "render/duckbox_renderer.h"
#include "cli_args.h"
#include "cli_utils.h"
#include "explore/dom_explorer.h"
#include "script_runner.h"
#include "repl/core/repl.h"
#include "ui/color.h"

using namespace xsql::cli;

/// Entry point that parses CLI options and dispatches to batch or REPL modes.
/// MUST preserve exit codes for script usage and MUST not hide fatal errors.
/// Inputs are argc/argv; outputs are process status with IO side effects.
int main(int argc, char** argv) {
  CliOptions options;
  if (argc == 1) {
    print_startup_help(std::cout);
    return 0;
  }
  if (std::string(argv[1]) == "explore") {
    if (argc == 3 && std::string(argv[2]) == "--help") {
      std::cout << "Usage: markql explore <input.html>\n";
      std::cout << "Keybindings: Up/Down move, Right/Enter expand, Left collapse, q quit.\n";
      return 0;
    }
    if (argc != 3) {
      std::cerr << "Usage: markql explore <input.html>\n";
      return 2;
    }
    return run_dom_explorer_from_input(argv[2], std::cerr);
  }

  std::string arg_error;
  if (!parse_cli_args(argc, argv, options, arg_error)) {
    std::cerr << arg_error << "\n";
    return 2;
  }
  if (options.show_help) {
    print_help(std::cout);
    return 0;
  }
  if (options.show_version) {
    std::cout << "markql " << xsql::version_string() << std::endl;
    return 0;
  }

  std::string query = options.query;
  std::string query_file = options.query_file;
  std::string input = options.input;
  bool interactive = options.interactive;
  bool color = options.color;
  std::string output_mode = options.output_mode;
  // Assumption: default highlight is on (per CLI flag requirement); auto-disabled on non-TTY.
  bool highlight = options.highlight;
  bool display_full = options.display_mode_set ? options.display_full : false;
  int timeout_ms = options.timeout_ms;
  const xsql::ColumnNameMode colname_mode = xsql::ColumnNameMode::Normalize;

  // WHY: reject unknown modes to avoid silently changing output contracts.
  if (output_mode != "duckbox" && output_mode != "json" && output_mode != "plain") {
    std::cerr << "Invalid --mode value (use duckbox|json|plain)\n";
    return 2;
  }

  try {
    if (options.lint) {
      std::vector<xsql::Diagnostic> diagnostics;

      auto collect_statement_diagnostics = [&](const std::string& statement,
                                               size_t statement_index,
                                               size_t total_statements) {
        std::vector<xsql::Diagnostic> statement_diags = xsql::lint_query(statement);
        if (total_statements > 1) {
          for (auto& diag : statement_diags) {
            diag.message = "statement " + std::to_string(statement_index) + "/" +
                           std::to_string(total_statements) + ": " + diag.message;
          }
        }
        diagnostics.insert(diagnostics.end(), statement_diags.begin(), statement_diags.end());
      };

      if (!query_file.empty()) {
        std::string script;
        try {
          script = read_file(query_file);
        } catch (const std::exception& ex) {
          std::cerr << "Error: " << ex.what() << std::endl;
          return 2;
        }
        if (!is_valid_utf8(script)) {
          std::cerr << "Error: query file is not valid UTF-8: " << query_file << std::endl;
          return 2;
        }
        ScriptSplitResult split = split_sql_script(script);
        if (split.error_message.has_value()) {
          diagnostics.push_back(xsql::make_syntax_diagnostic(
              script, *split.error_message, split.error_position));
        } else {
          const size_t total = split.statements.size();
          for (size_t i = 0; i < total; ++i) {
            collect_statement_diagnostics(split.statements[i].text, i + 1, total);
          }
        }
      } else {
        if (query.empty()) {
          std::cerr << "Missing query for --lint (use --lint \"...\" or --query/--query-file)\n";
          return 2;
        }
        collect_statement_diagnostics(query, 1, 1);
      }

      if (options.lint_format == "json") {
        std::cout << xsql::render_diagnostics_json(diagnostics) << std::endl;
      } else if (diagnostics.empty()) {
        std::cout << "No diagnostics." << std::endl;
      } else {
        std::cout << xsql::render_diagnostics_text(diagnostics) << std::endl;
      }
      return xsql::has_error_diagnostics(diagnostics) ? 1 : 0;
    }

    if (interactive) {
      ReplConfig repl_config;
      repl_config.input = input;
      repl_config.color = color;
      repl_config.highlight = highlight;
      repl_config.display_full = options.display_mode_set ? options.display_full : true;
      repl_config.output_mode = output_mode;
      repl_config.timeout_ms = timeout_ms;
      return run_repl(repl_config);
    }

    std::optional<std::string> stdin_cache;
    auto execute_and_render = [&](const std::string& raw_query) {
      const auto started_at = std::chrono::steady_clock::now();
      const auto rss_before_bytes = read_process_rss_bytes();
      bool runtime_summary_printed = false;
      auto emit_runtime_summary = [&]() {
        if (runtime_summary_printed) return;
        runtime_summary_printed = true;
        const auto finished_at = std::chrono::steady_clock::now();
        const long long elapsed_ms = static_cast<long long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(finished_at - started_at).count());
        const auto rss_after_bytes = read_process_rss_bytes();
        print_query_runtime_summary(rss_before_bytes, rss_after_bytes, elapsed_ms);
      };

      std::string statement = rewrite_from_path_if_needed(raw_query);
      xsql::QueryResult result;
      auto source = parse_query_source(statement);
      if (source.has_value() && source->statement_kind != xsql::Query::Kind::Select) {
        std::string meta_error;
        if (source->statement_kind == xsql::Query::Kind::ShowInput) {
          if (!build_show_input_result(input, result, meta_error)) {
            throw std::runtime_error(meta_error);
          }
        } else if (source->statement_kind == xsql::Query::Kind::ShowInputs) {
          if (!build_show_inputs_result({}, input, result, meta_error)) {
            throw std::runtime_error(meta_error);
          }
        } else {
          result = xsql::execute_query_from_document("", statement);
        }
      } else {
        if (source.has_value() && source->kind == xsql::Source::Kind::Url) {
          result = xsql::execute_query_from_url(source->value, statement, timeout_ms);
        } else if (source.has_value() && source->kind == xsql::Source::Kind::Path) {
          result = xsql::execute_query_from_file(source->value, statement);
        } else if (source.has_value() && source->kind == xsql::Source::Kind::RawHtml) {
          result = xsql::execute_query_from_document("", statement);
        } else if (source.has_value() && !source->needs_input) {
          result = xsql::execute_query_from_document("", statement);
        } else if (input.empty() || input == "document") {
          if (!stdin_cache.has_value()) {
            stdin_cache = read_stdin();
          }
          result = xsql::execute_query_from_document(*stdin_cache, statement);
        } else {
          if (is_url(input)) {
            result = xsql::execute_query_from_url(input, statement, timeout_ms);
          } else {
            result = xsql::execute_query_from_file(input, statement);
          }
        }
        auto sources = collect_source_uris(result);
        apply_source_uri_policy(result, sources);
      }
      for (const auto& warning : result.warnings) {
        if (color) std::cerr << kColor.yellow;
        std::cerr << "Warning: " << warning << std::endl;
        if (color) std::cerr << kColor.reset;
      }

      if (result.export_sink.kind != xsql::QueryResult::ExportSink::Kind::None) {
        std::string export_error;
        if (!xsql::cli::export_result(result, export_error, colname_mode)) {
          throw std::runtime_error(export_error);
        }
        if (!result.export_sink.path.empty()) {
          std::cout << "Wrote " << export_kind_label(result.export_sink.kind)
                    << ": " << result.export_sink.path << std::endl;
        }
        return;
      }

      if (output_mode == "duckbox") {
        if (result.to_table) {
          if (result.tables.empty()) {
            std::cout << "(empty table)" << std::endl;
            std::cout << "Rows: 0" << std::endl;
            emit_runtime_summary();
          } else if (result.table_options.format ==
                     xsql::QueryResult::TableOptions::Format::Sparse) {
            std::string json_out = build_table_json(result);
            if (display_full) {
              std::cout << colorize_json(json_out, color) << std::endl;
            } else {
              TruncateResult truncated = truncate_output(json_out, 10, 10);
              std::cout << colorize_json(truncated.output, color) << std::endl;
            }
            size_t sparse_rows = 0;
            for (const auto& table : result.tables) {
              if (result.table_options.sparse_shape ==
                  xsql::QueryResult::TableOptions::SparseShape::Long) {
                sparse_rows += table.rows.size();
              } else {
                sparse_rows += table.sparse_wide_rows.size();
              }
            }
            std::cout << "Rows: " << sparse_rows << std::endl;
            emit_runtime_summary();
          } else {
            for (size_t i = 0; i < result.tables.size(); ++i) {
              if (result.tables.size() > 1) {
                std::cout << "Table node_id=" << result.tables[i].node_id << std::endl;
              }
              std::cout << render_table_duckbox(result.tables[i], result.table_has_header, highlight,
                                                color, 40)
                        << std::endl;
              std::cout << "Rows: "
                        << count_table_rows(result.tables[i], result.table_has_header)
                        << std::endl;
            }
            emit_runtime_summary();
          }
        } else if (!result.to_list) {
          xsql::render::DuckboxOptions options;
          options.max_width = 0;
          options.max_rows = 40;
          options.highlight = highlight;
          options.is_tty = color;
          options.colname_mode = colname_mode;
          std::cout << xsql::render::render_duckbox(result, options) << std::endl;
          std::cout << "Rows: " << count_result_rows(result) << std::endl;
          emit_runtime_summary();
        } else {
          std::string json_out = build_json_list(result, colname_mode);
          if (display_full) {
            std::cout << colorize_json(json_out, color) << std::endl;
          } else {
            TruncateResult truncated = truncate_output(json_out, 10, 10);
            std::cout << colorize_json(truncated.output, color) << std::endl;
          }
          std::cout << "Rows: " << count_result_rows(result) << std::endl;
          emit_runtime_summary();
        }
      } else {
        std::string json_out =
            result.to_table
                ? build_table_json(result)
                : (result.to_list ? build_json_list(result, colname_mode)
                                  : build_json(result, colname_mode));
        if (output_mode == "plain") {
          std::cout << json_out << std::endl;
        } else if (display_full) {
          std::cout << colorize_json(json_out, color) << std::endl;
        } else {
          TruncateResult truncated = truncate_output(json_out, 10, 10);
          std::cout << colorize_json(truncated.output, color) << std::endl;
        }
      }
    };

    if (!query_file.empty()) {
      std::string script;
      try {
        script = read_file(query_file);
      } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 2;
      }
      if (!is_valid_utf8(script)) {
        std::cerr << "Error: query file is not valid UTF-8: " << query_file << std::endl;
        return 2;
      }
      ScriptRunOptions script_options;
      script_options.continue_on_error = options.continue_on_error;
      script_options.quiet = options.quiet;
      return run_sql_script(script, script_options, execute_and_render, std::cout, std::cerr);
    }

    if (query.empty()) {
      std::cerr << "Missing --query or --query-file\n";
      return 2;
    }
    execute_and_render(query);
    return 0;
  } catch (const std::exception& ex) {
    if (options.lint) {
      std::cerr << "Error: " << ex.what() << std::endl;
      return 2;
    }
    if (!query.empty()) {
      std::vector<xsql::Diagnostic> diagnostics = xsql::diagnose_query_failure(query, ex.what());
      if (!diagnostics.empty()) {
        std::cerr << xsql::render_diagnostics_text(diagnostics) << std::endl;
        return 1;
      }
    }
    if (color) std::cerr << kColor.red;
    std::cerr << "Error: " << ex.what() << std::endl;
    if (color) std::cerr << kColor.reset;
    return 1;
  }
}
