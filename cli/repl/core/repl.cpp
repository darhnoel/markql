#include "repl.h"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <unistd.h>

#include "cli_utils.h"
#include "script_runner.h"
#include "repl/config.h"
#include "export/export_sinks.h"
#include "render/duckbox_renderer.h"
#include "ui/color.h"
#include "repl/commands/registry.h"
#include "repl/core/editor_setup.h"
#include "repl/core/line_editor.h"
#include "repl/plugin_manager.h"

namespace xsql::cli {

int run_repl(ReplConfig& config) {
  ReplSettings settings;
  std::string config_error;
  std::string config_path = resolve_repl_config_path();
  bool config_loaded = load_repl_config(config_path, settings, config_error);
  size_t history_max_entries = settings.history_max_entries.value_or(200);

  std::unordered_map<std::string, LoadedSource> sources;
  std::string active_alias = "doc";
  if (!config.input.empty()) {
    sources[active_alias] = LoadedSource{config.input, std::nullopt};
  }
  std::string last_full_output;
  bool display_full = config.display_full;
  size_t max_rows = 40;
  std::vector<std::string> last_sources;
  std::vector<xsql::ColumnNameMapping> last_schema_map;
  std::string line;

  if (!isatty(fileno(stdout))) {
    config.color = false;
    config.highlight = false;
  }

  LineEditor editor(history_max_entries,
                    make_normal_repl_prompt(config.color),
                    normal_prompt_visible_len());
  configure_repl_editor(editor, config.color, config.highlight);
  CommandRegistry registry;
  register_default_commands(registry);
  PluginManager plugin_manager(registry);
  CommandContext command_ctx{
      config,
      editor,
      sources,
      active_alias,
      last_full_output,
      display_full,
      max_rows,
      last_schema_map,
      plugin_manager,
  };

  if (!config_error.empty()) {
    if (config.color) std::cerr << kColor.red;
    std::cerr << "Error: " << config_error << std::endl;
    if (config.color) std::cerr << kColor.reset;
  }
  if (config_loaded) {
    std::string apply_error;
    if (!apply_repl_settings(settings, config, editor, display_full, max_rows, apply_error)) {
      if (config.color) std::cerr << kColor.red;
      std::cerr << "Error: " << apply_error << std::endl;
      if (config.color) std::cerr << kColor.reset;
    }
  } else {
    std::string apply_error;
    ReplSettings defaults;
    defaults.history_path = resolve_default_history_path();
    if (!apply_repl_settings(defaults, config, editor, display_full, max_rows, apply_error) &&
        !apply_error.empty()) {
      if (config.color) std::cerr << kColor.red;
      std::cerr << "Error: " << apply_error << std::endl;
      if (config.color) std::cerr << kColor.reset;
    }
  }

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

    std::string query_text = rewrite_from_path_if_needed(raw_query);
    xsql::QueryResult result;
    auto source = parse_query_source(query_text);
    if (source.has_value() && source->statement_kind != xsql::Query::Kind::Select) {
      std::string active_source;
      auto active_it = sources.find(active_alias);
      if (active_it != sources.end()) {
        active_source = active_it->second.source;
      }
      std::string meta_error;
      if (source->statement_kind == xsql::Query::Kind::ShowInput) {
        if (!build_show_input_result(active_source, result, meta_error)) {
          throw std::runtime_error(meta_error);
        }
      } else if (source->statement_kind == xsql::Query::Kind::ShowInputs) {
        if (!build_show_inputs_result(last_sources, active_source, result, meta_error)) {
          throw std::runtime_error(meta_error);
        }
      } else {
        result = xsql::execute_query_from_document("", query_text);
      }
    } else {
      if (source.has_value() && source->kind == xsql::Source::Kind::Url) {
        result = xsql::execute_query_from_url(source->value, query_text, config.timeout_ms);
      } else if (source.has_value() && source->kind == xsql::Source::Kind::Path) {
        result = xsql::execute_query_from_file(source->value, query_text);
      } else if (source.has_value() && source->kind == xsql::Source::Kind::RawHtml) {
        result = xsql::execute_query_from_document("", query_text);
      } else if (source.has_value() && !source->needs_input) {
        result = xsql::execute_query_from_document("", query_text);
      } else {
        std::string alias = active_alias;
        if (source.has_value() && source->alias.has_value()) {
          alias = *source->alias;
        }
        auto it = sources.find(alias);
        if ((it == sources.end() || it->second.source.empty()) &&
            source.has_value() &&
            source->kind == xsql::Source::Kind::Document &&
            source->source_token.has_value() &&
            (*source->source_token == "doc" || *source->source_token == "document")) {
          auto active_it = sources.find(active_alias);
          if (active_it != sources.end() && !active_it->second.source.empty()) {
            it = active_it;
          }
        }
        if (it == sources.end() || it->second.source.empty()) {
          if (config.color) std::cerr << kColor.red;
          if (source.has_value() && source->alias.has_value()) {
            std::cerr << "No input loaded for alias '" << alias
                      << "'. Use .load <path|url> --alias " << alias << "." << std::endl;
          } else {
            std::cerr << "No input loaded. Use :load <path|url> or start with --input <path|url>."
                      << std::endl;
          }
          if (config.color) std::cerr << kColor.reset;
          return;
        }
        if (!it->second.html.has_value()) {
          it->second.html = load_html_input(it->second.source, config.timeout_ms);
        }
        result = xsql::execute_query_from_document(*it->second.html, query_text);
        if (!it->second.source.empty() &&
            (!source.has_value() || source->kind == xsql::Source::Kind::Document)) {
          for (auto& row : result.rows) {
            row.source_uri = it->second.source;
          }
        }
      }
      last_sources = collect_source_uris(result);
      apply_source_uri_policy(result, last_sources);
    }
    for (const auto& warning : result.warnings) {
      if (config.color) std::cerr << kColor.yellow;
      std::cerr << "Warning: " << warning << std::endl;
      if (config.color) std::cerr << kColor.reset;
    }
    last_schema_map = xsql::build_column_name_map(result.columns, config.colname_mode);
    if (result.export_sink.kind != xsql::QueryResult::ExportSink::Kind::None) {
      std::string export_error;
      if (!xsql::cli::export_result(result, export_error, config.colname_mode)) {
        throw std::runtime_error(export_error);
      }
      if (!result.export_sink.path.empty()) {
        std::cout << "Wrote " << export_kind_label(result.export_sink.kind)
                  << ": " << result.export_sink.path << std::endl;
      }
      return;
    }
    if (config.output_mode == "duckbox") {
      if (result.to_table) {
        if (result.tables.empty()) {
          std::cout << "(empty table)" << std::endl;
          std::cout << "Rows: 0" << std::endl;
          emit_runtime_summary();
        } else if (result.table_options.format ==
                   xsql::QueryResult::TableOptions::Format::Sparse) {
          std::string json_out = build_table_json(result);
          last_full_output = json_out;
          if (display_full) {
            std::cout << colorize_json(json_out, config.color) << std::endl;
          } else {
            TruncateResult truncated = truncate_output(json_out, 10, 10);
            std::cout << colorize_json(truncated.output, config.color) << std::endl;
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
            std::cout << render_table_duckbox(result.tables[i], result.table_has_header,
                                              config.highlight, config.color, max_rows)
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
        options.max_rows = max_rows;
        options.highlight = config.highlight;
        options.is_tty = config.color;
        options.colname_mode = config.colname_mode;
        std::cout << xsql::render::render_duckbox(result, options) << std::endl;
        std::cout << "Rows: " << count_result_rows(result) << std::endl;
        emit_runtime_summary();
      } else {
        std::string json_out = build_json_list(result, config.colname_mode);
        last_full_output = json_out;
        if (display_full) {
          std::cout << colorize_json(json_out, config.color) << std::endl;
        } else {
          TruncateResult truncated = truncate_output(json_out, 10, 10);
          std::cout << colorize_json(truncated.output, config.color) << std::endl;
        }
        std::cout << "Rows: " << count_result_rows(result) << std::endl;
        emit_runtime_summary();
      }
    } else {
      std::string json_out =
          result.to_table
              ? build_table_json(result)
              : (result.to_list ? build_json_list(result, config.colname_mode)
                                : build_json(result, config.colname_mode));
      last_full_output = json_out;
      if (config.output_mode == "plain") {
        std::cout << json_out << std::endl;
      } else if (display_full) {
        std::cout << colorize_json(json_out, config.color) << std::endl;
      } else {
        TruncateResult truncated = truncate_output(json_out, 10, 10);
        std::cout << colorize_json(truncated.output, config.color) << std::endl;
      }
    }
  };

  while (true) {
    if (!editor.read_line(line)) {
      break;
    }
    line = sanitize_pasted_line(line);
    if (line == ":quit" || line == ":exit" || line == ".quit" || line == ".q") {
      break;
    }
    if (registry.try_handle(line, command_ctx)) {
      if (!line.empty()) {
        editor.add_history(line);
      }
      continue;
    }
#ifndef XSQL_ENABLE_KHMER_NUMBER
    if (line.rfind(".number_to_khmer", 0) == 0 || line.rfind(".khmer_to_number", 0) == 0) {
      std::cerr << "Khmer number module not enabled. "
                   "Run: .plugin install number_to_khmer (then .plugin load number_to_khmer)"
                << std::endl;
      continue;
    }
#endif
    if (line.empty()) {
      continue;
    }
    std::string query_text = trim_semicolon(line);
    if (query_text.empty()) {
      continue;
    }
    LexInspection inspection = inspect_sql_input(query_text);
    if (inspection.has_error) {
      auto [line_no, col_no] = line_col_from_offset(query_text, inspection.error_position);
      if (config.color) std::cerr << kColor.red;
      std::cerr << "Error: " << inspection.error_message
                << " at line " << line_no << ", column " << col_no << std::endl;
      if (config.color) std::cerr << kColor.reset;
      editor.reset_render_state();
      continue;
    }
    if (inspection.empty_after_comments) {
      continue;
    }
    editor.add_history(query_text);
    try {
      ScriptSplitResult split = split_sql_script(query_text);
      if (split.error_message.has_value()) {
        auto [line_no, col_no] = line_col_from_offset(query_text, split.error_position);
        if (config.color) std::cerr << kColor.red;
        std::cerr << "Error: " << *split.error_message
                  << " at line " << line_no << ", column " << col_no << std::endl;
        if (config.color) std::cerr << kColor.reset;
        editor.reset_render_state();
        continue;
      }

      if (split.statements.size() > 1) {
        ScriptRunOptions options;
        options.quiet = true;
        int code = run_sql_script(query_text, options, execute_and_render, std::cout, std::cerr);
        if (code != 0) {
          if (config.color) std::cerr << kColor.yellow;
          std::cerr << "Tip: Check statement syntax around the reported statement index."
                    << std::endl;
          if (config.color) std::cerr << kColor.reset;
        }
      } else {
        execute_and_render(query_text);
      }
      editor.reset_render_state();
    } catch (const std::exception& ex) {
      std::vector<xsql::Diagnostic> diagnostics =
          xsql::diagnose_query_failure(query_text, ex.what());
      if (!diagnostics.empty()) {
        if (config.color) std::cerr << kColor.red;
        std::cerr << xsql::render_diagnostics_text(diagnostics) << std::endl;
        if (config.color) std::cerr << kColor.reset;
      } else {
        if (config.color) std::cerr << kColor.red;
        std::cerr << "Error: " << ex.what() << std::endl;
        if (config.color) std::cerr << kColor.reset;
      }
      editor.reset_render_state();
    }
  }
  return 0;
}

}  // namespace xsql::cli
