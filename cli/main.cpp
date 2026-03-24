#include <iostream>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "markql/markql.h"
#include "artifacts/artifacts.h"
#include "dom/html_parser.h"
#include "export/export_sinks.h"
#include "render/duckbox_renderer.h"
#include "render/query_template_renderer.h"
#include "cli_args.h"
#include "cli_utils.h"
#include "explore/dom_explorer.h"
#include "script_runner.h"
#include "repl/core/repl.h"
#include "runtime/engine/markql_internal.h"
#include "ui/color.h"

using namespace markql::cli;

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
    std::cout << "markql " << markql::version_string() << std::endl;
    return 0;
  }

  std::string query = options.query;
  std::string query_file = options.query_file;
  std::string input = options.input;
  bool interactive = options.interactive;
  const bool no_color_env = no_color_env_present();
  const bool stdout_is_tty = isatty(fileno(stdout));
  const bool stderr_is_tty = isatty(fileno(stderr));
  bool color = resolve_output_color_enabled(options, stdout_is_tty, no_color_env);
  const bool stderr_color = resolve_output_color_enabled(options, stderr_is_tty, no_color_env);
  markql::DiagnosticTextRenderOptions lint_render_options;
  lint_render_options.use_color =
      resolve_diagnostics_color_enabled(options, stdout_is_tty, no_color_env);
  markql::DiagnosticTextRenderOptions error_render_options;
  error_render_options.use_color =
      resolve_diagnostics_color_enabled(options, stderr_is_tty, no_color_env);
  std::string output_mode = options.output_mode;
  // Assumption: default highlight is on (per CLI flag requirement); auto-disabled on non-TTY.
  bool highlight = options.highlight;
  bool display_full = options.display_mode_set ? options.display_full : false;
  int timeout_ms = options.timeout_ms;
  const markql::ColumnNameMode colname_mode = markql::ColumnNameMode::Normalize;

  // WHY: reject unknown modes to avoid silently changing output contracts.
  if (!markql::cli::is_supported_output_mode(output_mode)) {
    std::cerr << "Invalid --mode value (use duckbox|json|plain|csv)\n";
    return 2;
  }

  try {
    std::optional<QueryRenderResult> prepared_query_file;
    auto load_prepared_query_file = [&]() -> const QueryRenderResult& {
      if (prepared_query_file.has_value()) {
        return *prepared_query_file;
      }
      if (query_file.empty()) {
        throw QueryRenderError("Missing query file for rendering");
      }
      if (!options.render_mode.empty() && markql::artifacts::path_has_artifact_magic(query_file)) {
        throw QueryRenderError(
            "--render expects SQL text template files, not prepared query artifacts (.mqp)");
      }
      prepared_query_file =
          load_query_file_text(query_file, options.render_mode, options.render_vars_file);
      if (prepared_query_file->rendered && !options.rendered_out.empty() &&
          !render_to_stdout_only(options.rendered_out)) {
        write_rendered_query_output(options.rendered_out, prepared_query_file->text);
      }
      return *prepared_query_file;
    };

    if (!options.artifact_info.empty()) {
      auto query_kind_name = [](markql::Query::Kind kind) {
        switch (kind) {
          case markql::Query::Kind::Select:
            return "select";
          case markql::Query::Kind::ShowInput:
            return "show_input";
          case markql::Query::Kind::ShowInputs:
            return "show_inputs";
          case markql::Query::Kind::ShowFunctions:
            return "show_functions";
          case markql::Query::Kind::ShowAxes:
            return "show_axes";
          case markql::Query::Kind::ShowOperators:
            return "show_operators";
          case markql::Query::Kind::DescribeDoc:
            return "describe_doc";
          case markql::Query::Kind::DescribeLanguage:
            return "describe_language";
        }
        return "unknown";
      };
      auto source_kind_name = [](markql::Source::Kind kind) {
        switch (kind) {
          case markql::Source::Kind::Document:
            return "document";
          case markql::Source::Kind::Path:
            return "path";
          case markql::Source::Kind::Url:
            return "url";
          case markql::Source::Kind::RawHtml:
            return "raw_html";
          case markql::Source::Kind::Fragments:
            return "fragments";
          case markql::Source::Kind::Parse:
            return "parse";
          case markql::Source::Kind::CteRef:
            return "cte_ref";
          case markql::Source::Kind::DerivedSubquery:
            return "derived_subquery";
        }
        return "unknown";
      };
      markql::artifacts::ArtifactInfo info =
          markql::artifacts::inspect_artifact_file(options.artifact_info);
      const std::string compatibility_error =
          markql::artifacts::artifact_compatibility_error(info.header);
      const bool compatible = compatibility_error.empty();
      std::cout << "Type: "
                << (info.header.kind == markql::artifacts::ArtifactKind::DocumentSnapshot ? "mqd"
                                                                                          : "mqp")
                << std::endl;
      std::cout << "Format: " << info.header.format_major << "." << info.header.format_minor
                << std::endl;
      std::cout << "Producer version: " << escape_control_for_terminal(info.producer_version)
                << std::endl;
      std::cout << "Producer major: " << info.header.producer_major << std::endl;
      std::cout << "Language version: " << escape_control_for_terminal(info.language_version)
                << std::endl;
      std::cout << "Language major: " << info.header.language_major << std::endl;
      std::cout << "Required features: " << info.header.required_features << std::endl;
      std::cout << "Sections: " << info.header.section_count << std::endl;
      std::cout << "Payload bytes: " << info.header.payload_bytes << std::endl;
      std::cout << "Payload checksum: " << info.header.payload_checksum << std::endl;
      std::cout << "Compatible: " << (compatible ? "yes" : "no") << std::endl;
      if (!compatible) {
        std::cout << "Compatibility note: " << compatibility_error << std::endl;
      }
      if (info.metadata_available) {
        if (info.header.kind == markql::artifacts::ArtifactKind::DocumentSnapshot) {
          std::cout << "Source URI: " << escape_control_for_terminal(info.source_uri) << std::endl;
          std::cout << "Nodes: " << info.node_count << std::endl;
        } else {
          std::cout << "Query kind: " << query_kind_name(info.query_kind) << std::endl;
          std::cout << "Source kind: " << source_kind_name(info.source_kind) << std::endl;
          std::cout << "Original query bytes: " << info.original_query.size() << std::endl;
        }
      } else {
        std::cout << "Metadata: unavailable for this artifact format" << std::endl;
      }
      return 0;
    }

    if (options.lint) {
      markql::LintResult lint_result;
      bool have_statement_results = false;

      auto merge_coverage = [](markql::LintCoverageLevel lhs,
                               markql::LintCoverageLevel rhs) -> markql::LintCoverageLevel {
        if (lhs == rhs) return lhs;
        return markql::LintCoverageLevel::Mixed;
      };

      auto collect_statement_diagnostics = [&](const std::string& statement, size_t statement_index,
                                               size_t total_statements) {
        markql::LintResult statement_result = markql::lint_query_detailed(statement);
        if (total_statements > 1) {
          for (auto& diag : statement_result.diagnostics) {
            diag.message = "statement " + std::to_string(statement_index) + "/" +
                           std::to_string(total_statements) + ": " + diag.message;
          }
        }
        lint_result.diagnostics.insert(lint_result.diagnostics.end(),
                                       statement_result.diagnostics.begin(),
                                       statement_result.diagnostics.end());
        if (!have_statement_results) {
          lint_result.summary = statement_result.summary;
          have_statement_results = true;
        } else {
          lint_result.summary.parse_succeeded =
              lint_result.summary.parse_succeeded && statement_result.summary.parse_succeeded;
          lint_result.summary.coverage =
              merge_coverage(lint_result.summary.coverage, statement_result.summary.coverage);
          lint_result.summary.relation_style_query = lint_result.summary.relation_style_query ||
                                                     statement_result.summary.relation_style_query;
          lint_result.summary.used_reduced_validation =
              lint_result.summary.used_reduced_validation ||
              statement_result.summary.used_reduced_validation;
          lint_result.summary.error_count += statement_result.summary.error_count;
          lint_result.summary.warning_count += statement_result.summary.warning_count;
          lint_result.summary.note_count += statement_result.summary.note_count;
        }
      };

      if (!query_file.empty()) {
        if (markql::artifacts::path_has_artifact_magic(query_file)) {
          std::cerr << "Error: --lint expects SQL text, not prepared query artifacts (.mqp)"
                    << std::endl;
          return 2;
        }
        std::string script = load_prepared_query_file().text;
        ScriptSplitResult split = split_sql_script(script);
        if (split.error_message.has_value()) {
          lint_result.diagnostics.push_back(
              markql::make_syntax_diagnostic(script, *split.error_message, split.error_position));
          lint_result.summary.parse_succeeded = false;
          lint_result.summary.coverage = markql::LintCoverageLevel::ParseOnly;
          lint_result.summary.error_count = 1;
          have_statement_results = true;
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
      } else {
        std::cout << markql::render_lint_result_text(lint_result, lint_render_options) << std::endl;
      }
      if (options.lint_format == "json") {
        std::cout << markql::render_lint_result_json(lint_result) << std::endl;
      }
      return lint_result.summary.error_count > 0 ? 1 : 0;
    }

    auto load_default_html = [&]() -> std::pair<std::string, std::string> {
      if (input.empty() || input == "document") {
        return {read_stdin(), "document"};
      }
      if (is_url(input)) {
        return {markql::markql_internal::fetch_url(input, timeout_ms), input};
      }
      return {markql::markql_internal::read_file(input), input};
    };

    if (!options.write_mqd.empty()) {
      auto [html, source_uri] = load_default_html();
      markql::HtmlDocument document = markql::parse_html(html);
      markql::artifacts::write_document_artifact_file(document, source_uri, options.write_mqd);
      std::cout << "Wrote MQD: " << options.write_mqd << std::endl;
      return 0;
    }

    if (!options.write_mqp.empty()) {
      if (!query_file.empty() && markql::artifacts::path_has_artifact_magic(query_file)) {
        std::cerr << "Error: --write-mqp expects SQL text, not an existing artifact" << std::endl;
        return 2;
      }
      std::string artifact_query = query;
      if (!query_file.empty()) {
        std::string script = load_prepared_query_file().text;
        ScriptSplitResult split = split_sql_script(script);
        if (split.error_message.has_value()) {
          auto [line, col] = line_col_from_offset(script, split.error_position);
          std::cerr << "Error: " << *split.error_message << " at line " << line << ", column "
                    << col << std::endl;
          return 1;
        }
        if (split.statements.size() != 1) {
          std::cerr << "Error: --write-mqp requires exactly one SQL statement" << std::endl;
          return 2;
        }
        artifact_query = split.statements.front().text;
      }
      markql::artifacts::PreparedQueryArtifact artifact =
          markql::artifacts::prepare_query_artifact(artifact_query);
      markql::artifacts::write_prepared_query_artifact_file(
          artifact.query, artifact.info.original_query, options.write_mqp);
      std::cout << "Wrote MQP: " << options.write_mqp << std::endl;
      return 0;
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

    if (!query_file.empty() && !options.render_mode.empty() &&
        render_to_stdout_only(options.rendered_out)) {
      std::cout << load_prepared_query_file().text;
      return 0;
    }

    std::optional<std::string> stdin_cache;
    auto render_result = [&](markql::QueryResult& result,
                             const std::chrono::steady_clock::time_point& started_at,
                             const std::optional<size_t>& rss_before_bytes) {
      bool runtime_summary_printed = false;
      auto emit_runtime_summary = [&]() {
        if (runtime_summary_printed) return;
        runtime_summary_printed = true;
        const auto finished_at = std::chrono::steady_clock::now();
        const long long elapsed_ms = static_cast<long long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(finished_at - started_at)
                .count());
        const auto rss_after_bytes = read_process_rss_bytes();
        print_query_runtime_summary(rss_before_bytes, rss_after_bytes, elapsed_ms);
      };

      auto sources = collect_source_uris(result);
      apply_source_uri_policy(result, sources);
      for (const auto& warning : result.warnings) {
        if (stderr_color) std::cerr << kColor.yellow;
        std::cerr << "Warning: " << warning << std::endl;
        if (stderr_color) std::cerr << kColor.reset;
      }

      if (result.export_sink.kind != markql::QueryResult::ExportSink::Kind::None) {
        std::string export_error;
        if (!markql::cli::export_result(result, export_error, colname_mode)) {
          throw std::runtime_error(export_error);
        }
        if (!result.export_sink.path.empty()) {
          std::cout << "Wrote " << export_kind_label(result.export_sink.kind) << ": "
                    << result.export_sink.path << std::endl;
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
                     markql::QueryResult::TableOptions::Format::Sparse) {
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
                  markql::QueryResult::TableOptions::SparseShape::Long) {
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
                                                highlight, color, 40)
                        << std::endl;
              std::cout << "Rows: " << count_table_rows(result.tables[i], result.table_has_header)
                        << std::endl;
            }
            emit_runtime_summary();
          }
        } else if (!result.to_list) {
          markql::render::DuckboxOptions options;
          options.max_width = 0;
          options.max_rows = 40;
          options.highlight = highlight;
          options.is_tty = color;
          options.colname_mode = colname_mode;
          std::cout << markql::render::render_duckbox(result, options) << std::endl;
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
      } else if (output_mode == "csv") {
        if (result.to_table) {
          throw std::runtime_error(
              "CSV output mode does not support TO TABLE() results; use TO "
              "TABLE(EXPORT='file.csv') instead");
        }
        std::string error;
        if (!markql::cli::write_csv(std::cout, result, error, colname_mode)) {
          throw std::runtime_error(error);
        }
      } else {
        std::string json_out = result.to_table
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

    auto execute_and_render = [&](const std::string& raw_query) {
      const auto started_at = std::chrono::steady_clock::now();
      const auto rss_before_bytes = read_process_rss_bytes();
      std::string statement = rewrite_from_path_if_needed(raw_query);
      markql::QueryResult result;
      auto source = parse_query_source(statement);
      if (source.has_value() && source->statement_kind != markql::Query::Kind::Select) {
        std::string meta_error;
        if (source->statement_kind == markql::Query::Kind::ShowInput) {
          if (!build_show_input_result(input, result, meta_error)) {
            throw std::runtime_error(meta_error);
          }
        } else if (source->statement_kind == markql::Query::Kind::ShowInputs) {
          if (!build_show_inputs_result({}, input, result, meta_error)) {
            throw std::runtime_error(meta_error);
          }
        } else {
          result = markql::execute_query_from_document("", statement);
        }
      } else {
        if (source.has_value() && source->kind == markql::Source::Kind::Url) {
          result = markql::execute_query_from_url(source->value, statement, timeout_ms);
        } else if (source.has_value() && source->kind == markql::Source::Kind::Path) {
          result = markql::execute_query_from_file(source->value, statement);
        } else if (source.has_value() && source->kind == markql::Source::Kind::RawHtml) {
          result = markql::execute_query_from_document("", statement);
        } else if (source.has_value() && !source->needs_input) {
          result = markql::execute_query_from_document("", statement);
        } else if (input.empty() || input == "document") {
          if (!stdin_cache.has_value()) {
            stdin_cache = read_stdin();
          }
          result = markql::execute_query_from_document(*stdin_cache, statement);
        } else {
          if (is_url(input)) {
            result = markql::execute_query_from_url(input, statement, timeout_ms);
          } else {
            result = markql::execute_query_from_file(input, statement);
          }
        }
      }
      render_result(result, started_at, rss_before_bytes);
    };

    if (!query_file.empty() && markql::artifacts::path_has_artifact_magic(query_file)) {
      markql::artifacts::PreparedQueryArtifact artifact =
          markql::artifacts::read_prepared_query_artifact_file(query_file);
      const auto started_at = std::chrono::steady_clock::now();
      const auto rss_before_bytes = read_process_rss_bytes();
      markql::QueryResult result;
      if (artifact.query.kind != markql::Query::Kind::Select) {
        result = markql::artifacts::execute_prepared_query_on_html(artifact, "", "document");
      } else if (artifact.query.source.kind == markql::Source::Kind::Url ||
                 artifact.query.source.kind == markql::Source::Kind::Path ||
                 artifact.query.source.kind == markql::Source::Kind::RawHtml ||
                 artifact.query.source.kind == markql::Source::Kind::Fragments ||
                 artifact.query.source.kind == markql::Source::Kind::Parse) {
        result = markql::artifacts::execute_prepared_query_on_html(artifact, "", "document");
      } else if (input.empty() || input == "document") {
        if (!stdin_cache.has_value()) stdin_cache = read_stdin();
        result =
            markql::artifacts::execute_prepared_query_on_html(artifact, *stdin_cache, "document");
      } else if (is_url(input)) {
        std::string html = markql::markql_internal::fetch_url(input, timeout_ms);
        result = markql::artifacts::execute_prepared_query_on_html(artifact, html, input);
      } else if (markql::artifacts::path_has_artifact_magic(input)) {
        markql::artifacts::ArtifactInfo info = markql::artifacts::inspect_artifact_file(input);
        if (info.header.kind != markql::artifacts::ArtifactKind::DocumentSnapshot) {
          throw std::runtime_error(
              "Prepared query artifacts (.mqp) cannot be used as input documents");
        }
        markql::artifacts::DocumentArtifact document =
            markql::artifacts::read_document_artifact_file(input);
        result = markql::artifacts::execute_prepared_query_on_document(artifact, document);
      } else {
        std::string html = markql::markql_internal::read_file(input);
        result = markql::artifacts::execute_prepared_query_on_html(artifact, html, input);
      }
      render_result(result, started_at, rss_before_bytes);
      return 0;
    }

    if (!query_file.empty()) {
      std::string script = load_prepared_query_file().text;
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
  } catch (const QueryRenderError& ex) {
    std::cerr << "Error: " << ex.what() << std::endl;
    return 2;
  } catch (const std::exception& ex) {
    if (options.lint) {
      std::cerr << "Error: " << ex.what() << std::endl;
      return 2;
    }
    if (!query.empty()) {
      std::vector<markql::Diagnostic> diagnostics =
          markql::diagnose_query_failure(query, ex.what());
      if (!diagnostics.empty()) {
        std::cerr << markql::render_diagnostics_text(diagnostics, error_render_options)
                  << std::endl;
        return 1;
      }
    }
    if (stderr_color) std::cerr << kColor.red;
    std::cerr << "Error: " << ex.what() << std::endl;
    if (stderr_color) std::cerr << kColor.reset;
    return 1;
  }
}
