#include "render/query_template_renderer.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "cli_utils.h"

namespace markql::cli {

namespace {

struct CommandResult {
  int exit_code = 0;
  std::string output;
};

std::string shell_quote_single(const std::string& value) {
  std::string quoted;
  quoted.reserve(value.size() + 2);
  quoted.push_back('\'');
  for (char c : value) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(c);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::filesystem::path source_root() {
#ifdef MARKQL_SOURCE_ROOT
  return std::filesystem::path(MARKQL_SOURCE_ROOT);
#else
  return std::filesystem::current_path();
#endif
}

std::string python_executable() {
  const char* env_python = std::getenv("MARKQL_PYTHON");
  if (env_python && *env_python) {
    return env_python;
  }
  return "python3";
}

CommandResult run_command_capture_merged(const std::string& command) {
#ifdef _WIN32
  FILE* pipe = _popen(command.c_str(), "r");
#else
  FILE* pipe = popen(command.c_str(), "r");
#endif
  if (!pipe) {
    throw QueryRenderError("Failed to start template renderer process");
  }

  std::string output;
  char buffer[8192];
  while (true) {
    size_t read = std::fread(buffer, 1, sizeof(buffer), pipe);
    if (read > 0) {
      output.append(buffer, read);
    }
    if (read < sizeof(buffer)) {
      if (std::feof(pipe)) break;
      if (std::ferror(pipe)) {
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        throw QueryRenderError("Failed to read template renderer output");
      }
    }
  }

#ifdef _WIN32
  const int status = _pclose(pipe);
  return {status, output};
#else
  const int status = pclose(pipe);
  if (WIFEXITED(status)) {
    return {WEXITSTATUS(status), output};
  }
  return {status, output};
#endif
}

std::string trim_trailing_newlines(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

std::string render_j2_query_file(const std::string& query_file, const std::string& vars_file) {
  const std::filesystem::path repo_root = source_root();
  const std::filesystem::path python_path = repo_root / "python";

  std::ostringstream command;
  command << "env PYTHONPATH=" << shell_quote_single(python_path.string()) << " "
          << shell_quote_single(python_executable()) << " -m markql._render_cli"
          << " --render " << shell_quote_single("j2")
          << " --template " << shell_quote_single(query_file);
  if (!vars_file.empty()) {
    command << " --vars " << shell_quote_single(vars_file);
  }
  command << " 2>&1";

  CommandResult result = run_command_capture_merged(command.str());
  if (result.exit_code != 0) {
    std::string message = trim_trailing_newlines(result.output);
    if (message.empty()) {
      message = "Template renderer process failed";
    }
    throw QueryRenderError(message);
  }
  return result.output;
}

}  // namespace

QueryRenderResult load_query_file_text(const std::string& query_file,
                                       const std::string& render_mode,
                                       const std::string& vars_file) {
  std::string script;
  try {
    script = read_file(query_file);
  } catch (const std::exception& ex) {
    throw QueryRenderError(ex.what());
  }
  if (!is_valid_utf8(script)) {
    throw QueryRenderError("query file is not valid UTF-8: " + query_file);
  }
  if (render_mode.empty()) {
    return {script, false};
  }
  if (render_mode != "j2") {
    throw QueryRenderError("Invalid --render value (use j2)");
  }
  std::string rendered = render_j2_query_file(query_file, vars_file);
  if (!is_valid_utf8(rendered)) {
    throw QueryRenderError("Rendered query is not valid UTF-8");
  }
  return {rendered, true};
}

void write_rendered_query_output(const std::string& destination, const std::string& rendered_query) {
  if (destination.empty()) return;
  if (destination == "-") {
    std::cout << rendered_query;
    return;
  }
  std::ofstream out(destination, std::ios::binary);
  if (!out) {
    throw QueryRenderError("Failed to open rendered output file: " + destination);
  }
  out << rendered_query;
  if (!out.good()) {
    throw QueryRenderError("Failed to write rendered output file: " + destination);
  }
}

bool render_to_stdout_only(const std::string& destination) {
  return destination == "-";
}

}  // namespace markql::cli
