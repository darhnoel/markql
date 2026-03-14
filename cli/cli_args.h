#pragma once

#include <string>
#include <ostream>

namespace xsql::cli {

enum class ColorMode {
  Legacy,
  Auto,
  Always,
  Never,
};

/// Captures CLI arguments so main can dispatch without re-parsing raw argv.
/// MUST keep defaults consistent with CLI behavior and MUST validate after parsing.
/// Inputs are argv; outputs are populated fields with no side effects by itself.
struct CliOptions {
  std::string query;
  std::string query_file;
  std::string input;
  std::string write_mqd;
  std::string write_mqp;
  std::string artifact_info;
  bool interactive = false;
  bool color = true;
  std::string output_mode = "duckbox";
  bool highlight = true;
  bool display_full = false;
  bool display_mode_set = false;
  int timeout_ms = 5000;
  bool show_help = false;
  bool show_version = false;
  bool continue_on_error = false;
  bool quiet = false;
  bool lint = false;
  std::string lint_format = "text";
  ColorMode color_mode = ColorMode::Legacy;
};

/// Prints the brief startup help shown when no arguments are provided.
/// MUST remain user-facing and MUST not throw on stream failures.
/// Inputs are the output stream; side effects are writing help text.
void print_startup_help(std::ostream& os);
/// Prints the full help text for explicit --help.
/// MUST remain accurate to supported flags and MUST not throw on stream failures.
/// Inputs are the output stream; side effects are writing help text.
void print_help(std::ostream& os);
/// Parses CLI flags into options and reports a user-facing error string.
/// MUST leave options in a valid state and MUST return false on invalid flags.
/// Inputs are argc/argv; outputs are options/error with no external side effects.
bool parse_cli_args(int argc, char** argv, CliOptions& options, std::string& error);
/// Returns true when a result output mode is supported by CLI and REPL.
bool is_supported_output_mode(const std::string& mode);

/// Resolves whether general CLI color output should be enabled.
/// MUST honor NO_COLOR and TTY-aware auto mode.
bool resolve_output_color_enabled(const CliOptions& options,
                                  bool is_tty,
                                  bool no_color_env);
/// Resolves whether lint text diagnostics should render ANSI color.
/// MUST keep legacy/default lint rendering plain unless explicitly opted in.
bool resolve_diagnostics_color_enabled(const CliOptions& options,
                                       bool is_tty,
                                       bool no_color_env);
/// Returns true when NO_COLOR is set and non-empty.
bool no_color_env_present();

}  // namespace xsql::cli
