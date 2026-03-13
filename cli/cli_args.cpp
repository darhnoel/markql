#include "cli_args.h"

#include <cstdlib>
#include <string>

namespace xsql::cli {

/// Prints the startup help so users see baseline usage without flags.
/// MUST keep examples aligned with current CLI and MUST not throw on stream errors.
/// Inputs are the output stream; side effects are writing text to stdout/stderr.
void print_startup_help(std::ostream& os) {
  os << "markql - MarkQL command line interface\n\n";
  os << "Usage:\n";
  os << "  markql --query <query> [--input <path>]\n";
  os << "  markql --query-file <file> [--input <path>]\n";
  os << "         [--continue-on-error] [--quiet]\n";
  os << "  markql --lint \"<query>\" [--format text|json]\n";
  os << "  markql --interactive [--input <path>]\n";
  os << "  markql explore <input.html>\n";
  os << "  markql --mode duckbox|json|plain|csv\n";
  os << "  markql --display_mode more|less\n";
  os << "  markql --highlight on|off\n";
  os << "  markql --timeout-ms <n>\n";
  os << "  markql --version\n";
  os << "  markql --color=auto|always|never|disabled\n\n";
  os << "Notes:\n";
  os << "  - Legacy `xsql` binary name is still available for compatibility.\n";
  os << "  - If --input is omitted, HTML is read from stdin.\n";
  os << "  - URLs are supported when libcurl is available.\n";
  os << "  - TO LIST() outputs a JSON list for a single projected column.\n";
  os << "  - TO TABLE() extracts HTML tables into rows.\n";
  os << "  - SQL comments are supported: -- line comments, /* block comments */.\n";
  os << "  - Exit codes: 0=success, 1=parse/runtime error, 2=CLI/IO usage error.\n\n";
  os << "Examples:\n";
  os << "  markql --query \"SELECT table FROM doc\" --input ./data/index.html\n";
  os << "  markql --lint \"SELECT div FROM doc WHERE\"\n";
  os << "  markql --query \"SELECT link.href FROM doc WHERE attributes.rel = 'preload' TO LIST()\" --input ./data/index.html\n";
  os << "  markql --interactive --input ./data/index.html\n";
}

/// Prints the explicit help requested by --help.
/// MUST stay synchronized with supported flags and MUST not throw on stream errors.
/// Inputs are the output stream; side effects are writing text to stdout/stderr.
void print_help(std::ostream& os) {
  os << "Usage: markql --query <query> [--input <path>]\n";
  os << "       markql --query-file <file> [--input <path>]\n";
  os << "              [--continue-on-error] [--quiet]\n";
  os << "       markql --lint \"<query>\" [--format text|json]\n";
  os << "       markql --interactive [--input <path>]\n";
  os << "       markql explore <input.html>\n";
  os << "       markql --mode duckbox|json|plain|csv\n";
  os << "       markql --display_mode more|less\n";
  os << "       markql --highlight on|off\n";
  os << "       markql --timeout-ms <n>\n";
  os << "       markql --version\n";
  os << "       markql --color=auto|always|never|disabled\n";
  os << "Legacy `xsql` command name remains available.\n";
  os << "If --input is omitted, HTML is read from stdin.\n";
  os << "Scripts and REPL input support SQL comments: -- ... and /* ... */.\n";
  os << "Use TO CSV('file.csv'), TO PARQUET('file.parquet'), TO JSON('file.json'), or\n"
        "TO NDJSON('file.ndjson') in queries to export.\n";
  os << "--lint validates syntax + semantic rules without executing the query.\n";
  os << "--format json emits lint diagnostics as a JSON array.\n";
  os << "NO_COLOR disables ANSI color output even when --color=always/auto is set.\n";
  os << "Explore mode keybindings: Up/Down move, Right/Enter expand, Left collapse, / search, n/N next/prev, j/k scroll inner_html, +/- zoom inner_html, q quit.\n";
  os << "Explore mode restores position/expansion per input within the current process session.\n";
  os << "Exit codes: 0=success, 1=parse/runtime error, 2=CLI/IO usage error.\n";
}

/// Parses argv into typed options so main can dispatch consistently.
/// MUST return false for invalid flags and MUST not mutate options on parse failure.
/// Inputs are argc/argv; outputs are options/error and no external side effects.
bool parse_cli_args(int argc, char** argv, CliOptions& options, std::string& error) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--query") {
      if (i + 1 >= argc) {
        error = "Missing value for --query";
        return false;
      }
      options.query = argv[++i];
    } else if (arg == "--query-file") {
      if (i + 1 >= argc) {
        error = "Missing value for --query-file";
        return false;
      }
      options.query_file = argv[++i];
    } else if (arg == "--input") {
      if (i + 1 >= argc) {
        error = "Missing value for --input";
        return false;
      }
      options.input = argv[++i];
    } else if (arg == "--interactive") {
      options.interactive = true;
    } else if (arg == "--lint") {
      options.lint = true;
      if (i + 1 < argc) {
        std::string maybe_query = argv[i + 1];
        if (!maybe_query.empty() && maybe_query[0] != '-') {
          options.query = maybe_query;
          ++i;
        }
      }
    } else if (arg == "--format") {
      if (i + 1 >= argc) {
        error = "Missing value for --format";
        return false;
      }
      options.lint_format = argv[++i];
    } else if (arg == "--mode") {
      if (i + 1 >= argc) {
        error = "Missing value for --mode";
        return false;
      }
      options.output_mode = argv[++i];
    } else if (arg == "--display_mode" || arg == "--display-mode") {
      if (i + 1 >= argc) {
        error = "Missing value for --display_mode";
        return false;
      }
      std::string value = argv[++i];
      if (value == "more") {
        options.display_full = true;
        options.display_mode_set = true;
      } else if (value == "less") {
        options.display_full = false;
        options.display_mode_set = true;
      } else {
        // WHY: invalid display mode values must fail fast for consistent output.
        error = "Invalid --display_mode value (use more|less)";
        return false;
      }
    } else if (arg == "--highlight") {
      if (i + 1 >= argc) {
        error = "Missing value for --highlight";
        return false;
      }
      std::string value = argv[++i];
      if (value == "on") {
        options.highlight = true;
      } else if (value == "off") {
        options.highlight = false;
      } else {
        // WHY: invalid highlight values must fail fast to avoid ambiguous UI state.
        error = "Invalid --highlight value (use on|off)";
        return false;
      }
    } else if (arg.rfind("--color=", 0) == 0) {
      std::string value = arg.substr(std::string("--color=").size());
      if (value == "disabled" || value == "never") {
        options.color = false;
        options.color_mode = ColorMode::Never;
      } else if (value == "always" || value == "enabled") {
        options.color = true;
        options.color_mode = ColorMode::Always;
      } else if (value == "auto") {
        options.color = true;
        options.color_mode = ColorMode::Auto;
      } else {
        error = "Invalid --color value (use auto|always|never|disabled)";
        return false;
      }
    } else if (arg == "--timeout-ms") {
      if (i + 1 >= argc) {
        error = "Missing value for --timeout-ms";
        return false;
      }
      options.timeout_ms = std::stoi(argv[++i]);
    } else if (arg == "--help") {
      options.show_help = true;
    } else if (arg == "--version") {
      options.show_version = true;
    } else if (arg == "--continue-on-error") {
      options.continue_on_error = true;
    } else if (arg == "--quiet") {
      options.quiet = true;
    } else {
      error = "Unknown argument: " + arg;
      return false;
    }
  }
  if (!options.query.empty() && !options.query_file.empty()) {
    error = "Error: --query and --query-file are mutually exclusive";
    return false;
  }
  if (!options.lint && options.lint_format != "text") {
    error = "--format is only supported with --lint";
    return false;
  }
  if (options.lint_format != "text" && options.lint_format != "json") {
    error = "Invalid --format value (use text|json)";
    return false;
  }
  if (options.lint && options.interactive) {
    error = "--lint and --interactive are mutually exclusive";
    return false;
  }
  return true;
}

bool is_supported_output_mode(const std::string& mode) {
  return mode == "duckbox" || mode == "json" || mode == "plain" || mode == "csv";
}

bool resolve_output_color_enabled(const CliOptions& options,
                                  bool is_tty,
                                  bool no_color_env) {
  if (no_color_env) return false;
  switch (options.color_mode) {
    case ColorMode::Legacy:
      return options.color;
    case ColorMode::Auto:
      return is_tty;
    case ColorMode::Always:
      return true;
    case ColorMode::Never:
      return false;
  }
  return options.color;
}

bool resolve_diagnostics_color_enabled(const CliOptions& options,
                                       bool is_tty,
                                       bool no_color_env) {
  if (no_color_env) return false;
  switch (options.color_mode) {
    case ColorMode::Legacy:
      return false;
    case ColorMode::Auto:
      return is_tty;
    case ColorMode::Always:
      return true;
    case ColorMode::Never:
      return false;
  }
  return false;
}

bool no_color_env_present() {
  const char* value = std::getenv("NO_COLOR");
  return value != nullptr && *value != '\0';
}

}  // namespace xsql::cli
