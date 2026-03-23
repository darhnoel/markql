#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "test_harness.h"

#include "repl/commands/registry.h"
#include "repl/commands/describe_last_command.h"
#include "repl/commands/explore_command.h"
#include "repl/commands/lint_command.h"
#include "repl/commands/mode_command.h"
#include "repl/commands/plugin_command.h"
#include "repl/commands/plugin_registry.h"
#include "repl/commands/set_command.h"
#include "repl/commands/summarize_content_command.h"
#include "repl/core/line_editor.h"
#include "repl/plugin_manager.h"
#include "repl/core/repl.h"
#include "repl/ui/sql_keywords.h"

namespace {

struct StreamCapture {
  std::ostream* stream = nullptr;
  std::ostringstream buffer;
  std::streambuf* original = nullptr;

  explicit StreamCapture(std::ostream& target)
      : stream(&target), original(target.rdbuf(buffer.rdbuf())) {}
  ~StreamCapture() {
    if (stream && original) {
      buffer.flush();
      stream->rdbuf(original);
    }
  }

  std::string str() const { return buffer.str(); }
};

std::filesystem::path make_temp_dir(const std::string& prefix) {
  static size_t counter = 0;
  const auto base = std::filesystem::temp_directory_path();
  while (true) {
    auto candidate = base / (prefix + "_" + std::to_string(counter++));
    std::error_code ec;
    if (std::filesystem::create_directories(candidate, ec)) {
      return candidate;
    }
    if (ec) {
      throw std::runtime_error("Failed to create temp directory: " + candidate.string());
    }
  }
}

struct ScopedCurrentPath {
  std::filesystem::path original;

  explicit ScopedCurrentPath(const std::filesystem::path& next)
      : original(std::filesystem::current_path()) {
    std::filesystem::current_path(next);
  }

  ~ScopedCurrentPath() {
    std::error_code ec;
    std::filesystem::current_path(original, ec);
  }
};

struct ScopedEnvVar {
  std::string name;
  bool had_value = false;
  std::string original_value;

  ScopedEnvVar(const std::string& key, const std::string& value) : name(key) {
    if (const char* existing = std::getenv(name.c_str())) {
      had_value = true;
      original_value = existing;
    }
#if defined(_WIN32)
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
  }

  ~ScopedEnvVar() {
    if (had_value) {
#if defined(_WIN32)
      _putenv_s(name.c_str(), original_value.c_str());
#else
      setenv(name.c_str(), original_value.c_str(), 1);
#endif
    } else {
#if defined(_WIN32)
      _putenv_s(name.c_str(), "");
#else
      unsetenv(name.c_str());
#endif
    }
  }
};

}  // namespace

static void test_summarize_content_basic() {
  StreamCapture capture(std::cout);
  markql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  markql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, markql::cli::LoadedSource> sources;
  sources["doc"] = markql::cli::LoadedSource{
      "inline", std::optional<std::string>("<html><body><div>Hello Khmer World</div></body></html>")};
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<markql::ColumnNameMapping> last_schema_map;

  markql::cli::CommandRegistry registry;
  markql::cli::PluginManager plugin_manager(registry);
  markql::cli::CommandContext ctx{
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

  auto handler = markql::cli::make_summarize_content_command();
  bool handled = handler(".summarize_content", ctx);
  expect_true(handled, "summarize_content should handle command");
  std::string output = capture.str();
  expect_true(output.find("hello") != std::string::npos, "output should include token 'hello'");
}

static void test_summarize_content_khmer_requires_plugin() {
  StreamCapture capture(std::cerr);
  markql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  markql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, markql::cli::LoadedSource> sources;
  sources["doc"] = markql::cli::LoadedSource{
      "inline", std::optional<std::string>("<html><body><div>សូមអរគុណ</div></body></html>")};
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<markql::ColumnNameMapping> last_schema_map;

  markql::cli::CommandRegistry registry;
  markql::cli::PluginManager plugin_manager(registry);
  markql::cli::CommandContext ctx{
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

  auto handler = markql::cli::make_summarize_content_command();
  bool handled = handler(".summarize_content --lang khmer", ctx);
  expect_true(handled, "summarize_content should handle khmer command");
  std::string output = capture.str();
  expect_true(output.find(".plugin install khmer_segmenter") != std::string::npos,
              "missing plugin should suggest .plugin install khmer_segmenter");
}

static void test_summarize_content_max_tokens() {
  StreamCapture capture(std::cout);
  markql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  markql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, markql::cli::LoadedSource> sources;
  sources["doc"] = markql::cli::LoadedSource{
      "inline",
      std::optional<std::string>("<html><body><h3>Alpha Alpha Beta</h3><p>Gamma</p></body></html>")};
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<markql::ColumnNameMapping> last_schema_map;

  markql::cli::CommandRegistry registry;
  markql::cli::PluginManager plugin_manager(registry);
  markql::cli::CommandContext ctx{
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

  auto handler = markql::cli::make_summarize_content_command();
  bool handled = handler(".summarize_content --max_tokens 1", ctx);
  expect_true(handled, "summarize_content should handle max_tokens");
  std::string output = capture.str();
  expect_true(output.find("alpha") != std::string::npos, "output should include token 'alpha'");
  expect_true(output.find("beta") == std::string::npos, "output should not include token 'beta'");
  expect_true(output.find("gamma") == std::string::npos, "output should not include token 'gamma'");
}

static void test_sql_keyword_catalog_includes_new_tokens() {
  expect_true(markql::cli::is_sql_keyword_token("case"), "CASE should be highlighted as keyword");
  expect_true(markql::cli::is_sql_keyword_token("WHEN"), "WHEN should be highlighted as keyword");
  expect_true(markql::cli::is_sql_keyword_token("ndjson"), "NDJSON should be highlighted as keyword");
  expect_true(!markql::cli::is_sql_keyword_token("first_text"),
              "FIRST_TEXT is a function-like identifier, not a reserved keyword");
  expect_true(!markql::cli::is_sql_keyword_token("doc"),
              "doc should be treated as a source name, not a reserved keyword");
  expect_true(!markql::cli::is_sql_keyword_token("document"),
              "document should be treated as a source name, not a reserved keyword");
  expect_true(!markql::cli::is_sql_keyword_token("table"),
              "table should not be highlighted as a reserved SQL keyword");
  expect_true(!markql::cli::is_sql_keyword_token("view"),
              "view names should not be highlighted as reserved keywords");
}

static void test_set_colnames_command() {
  StreamCapture capture(std::cout);
  markql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  markql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, markql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<markql::ColumnNameMapping> last_schema_map;
  markql::cli::CommandRegistry registry;
  markql::cli::PluginManager plugin_manager(registry);
  markql::cli::CommandContext ctx{
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
  auto handler = markql::cli::make_set_command();
  bool handled = handler(".set colnames raw", ctx);
  expect_true(handled, "set command handles colnames raw");
  expect_true(config.colname_mode == markql::ColumnNameMode::Raw, "set command updates mode");
}

static void test_mode_command_accepts_csv() {
  StreamCapture capture(std::cout);
  markql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  markql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, markql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<markql::ColumnNameMapping> last_schema_map;
  markql::cli::CommandRegistry registry;
  markql::cli::PluginManager plugin_manager(registry);
  markql::cli::CommandContext ctx{
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

  auto handler = markql::cli::make_mode_command();
  bool handled = handler(".mode csv", ctx);
  expect_true(handled, "mode command handles csv");
  expect_true(config.output_mode == "csv", "mode command sets csv mode");
  expect_true(capture.str().find("Output mode: csv") != std::string::npos,
              "mode command confirms csv mode");
}

static void test_lint_command_toggles_on_and_off() {
  StreamCapture capture(std::cout);
  markql::cli::ReplConfig config;
  config.color = false;
  markql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, markql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<markql::ColumnNameMapping> last_schema_map;
  markql::cli::CommandRegistry registry;
  markql::cli::PluginManager plugin_manager(registry);
  markql::cli::CommandContext ctx{
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

  auto handler = markql::cli::make_lint_command();
  bool handled_on = handler(".lint on", ctx);
  expect_true(handled_on, ".lint on should be handled");
  expect_true(config.lint_warnings, ".lint on enables lint warnings");

  bool handled_off = handler(":lint off", ctx);
  expect_true(handled_off, ":lint off should be handled");
  expect_true(!config.lint_warnings, ":lint off disables lint warnings");

  bool handled_status = handler(".lint", ctx);
  expect_true(handled_status, ".lint status should be handled");
  std::string output = capture.str();
  expect_true(output.find("Lint warnings: on") != std::string::npos,
              ".lint on should confirm enabled status");
  expect_true(output.find("Lint warnings: off") != std::string::npos,
              ".lint off should confirm disabled status");
}

static void test_lint_command_rejects_invalid_value() {
  StreamCapture capture(std::cerr);
  markql::cli::ReplConfig config;
  config.color = false;
  config.lint_warnings = true;
  markql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, markql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<markql::ColumnNameMapping> last_schema_map;
  markql::cli::CommandRegistry registry;
  markql::cli::PluginManager plugin_manager(registry);
  markql::cli::CommandContext ctx{
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

  auto handler = markql::cli::make_lint_command();
  bool handled = handler(".lint maybe", ctx);
  expect_true(handled, ".lint invalid value should be handled");
  expect_true(config.lint_warnings, "invalid .lint value should not change state");
  std::string output = capture.str();
  expect_true(output.find("Usage: .lint on|off or :lint on|off") != std::string::npos,
              "invalid .lint value should print usage");
}

static void test_describe_last_command_outputs_map() {
  StreamCapture capture(std::cout);
  markql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  markql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, markql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<markql::ColumnNameMapping> last_schema_map = {
      {"data-id", "data_id"},
      {"data_id", "data_id__2"},
  };
  markql::cli::CommandRegistry registry;
  markql::cli::PluginManager plugin_manager(registry);
  markql::cli::CommandContext ctx{
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
  auto handler = markql::cli::make_describe_last_command();
  bool handled = handler("DESCRIBE LAST;", ctx);
  expect_true(handled, "describe last command handles statement");
  std::string output = capture.str();
  expect_true(output.find("raw_name") != std::string::npos, "describe last header");
  expect_true(output.find("data-id") != std::string::npos, "describe last raw value");
  expect_true(output.find("data_id__2") != std::string::npos, "describe last output value");
}

static void test_describe_last_command_outputs_csv() {
  StreamCapture capture(std::cout);
  markql::cli::ReplConfig config;
  config.output_mode = "csv";
  config.color = false;
  config.highlight = false;
  markql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, markql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<markql::ColumnNameMapping> last_schema_map = {
      {"data-id", "data_id"},
  };
  markql::cli::CommandRegistry registry;
  markql::cli::PluginManager plugin_manager(registry);
  markql::cli::CommandContext ctx{
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
  auto handler = markql::cli::make_describe_last_command();
  bool handled = handler("DESCRIBE LAST;", ctx);
  expect_true(handled, "describe last command handles csv output");
  std::string output = capture.str();
  expect_true(output.find("raw_name,output_name") != std::string::npos, "describe last csv header");
  expect_true(output.find("data-id,data_id") != std::string::npos, "describe last csv row");
  expect_true(output.find("Rows: 1") != std::string::npos, "describe last csv row count");
}

static void test_explore_command_uses_active_alias_by_default() {
  StreamCapture err_capture(std::cerr);
  markql::cli::ReplConfig config;
  config.color = false;
  markql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, markql::cli::LoadedSource> sources;
  sources["doc"] = markql::cli::LoadedSource{"docs/fixtures/basic.html", std::nullopt};
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<markql::ColumnNameMapping> last_schema_map;
  markql::cli::CommandRegistry registry;
  markql::cli::PluginManager plugin_manager(registry);
  markql::cli::CommandContext ctx{
      config, editor, sources, active_alias, last_full_output,
      display_full, max_rows, last_schema_map, plugin_manager,
  };

  std::string explored;
  auto handler = markql::cli::make_explore_command_with_runner(
      [&](const std::string& input, std::ostream&) {
        explored = input;
        return 0;
      });
  bool handled = handler(".explore", ctx);
  expect_true(handled, ".explore should be handled");
  expect_true(explored == "docs/fixtures/basic.html",
              ".explore default should use active alias source");
  expect_true(err_capture.str().empty(), ".explore default should not print errors");
}

static void test_explore_command_accepts_direct_target_and_alias_target() {
  markql::cli::ReplConfig config;
  config.color = false;
  markql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, markql::cli::LoadedSource> sources;
  sources["doc"] = markql::cli::LoadedSource{"docs/fixtures/basic.html", std::nullopt};
  sources["backup"] = markql::cli::LoadedSource{"docs/fixtures/products.html", std::nullopt};
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<markql::ColumnNameMapping> last_schema_map;
  markql::cli::CommandRegistry registry;
  markql::cli::PluginManager plugin_manager(registry);
  markql::cli::CommandContext ctx{
      config, editor, sources, active_alias, last_full_output,
      display_full, max_rows, last_schema_map, plugin_manager,
  };

  std::vector<std::string> explored_targets;
  auto handler = markql::cli::make_explore_command_with_runner(
      [&](const std::string& input, std::ostream&) {
        explored_targets.push_back(input);
        return 0;
      });

  bool handled_alias = handler(".explore backup", ctx);
  bool handled_direct = handler(".explore https://example.com/page.html", ctx);
  expect_true(handled_alias, ".explore <alias> should be handled");
  expect_true(handled_direct, ".explore <direct target> should be handled");
  expect_eq(explored_targets.size(), static_cast<size_t>(2), "explore runner call count");
  expect_true(explored_targets[0] == "docs/fixtures/products.html",
              ".explore alias should resolve to alias source");
  expect_true(explored_targets[1] == "https://example.com/page.html",
              ".explore direct should forward input");
}

static void test_plugin_registry_rejects_unsafe_plugin_name() {
  const auto temp_dir = make_temp_dir("markql_plugin_registry_name");
  const auto registry_path = temp_dir / "registry.json";
  {
    std::ofstream out(registry_path);
    out << R"([
  {
    "name": "../escape",
    "repo": "https://example.com/plugin.git",
    "cmake_subdir": ".",
    "artifact": "libescape{ext}"
  }
])";
  }

  ScopedEnvVar registry_env("MARKQL_PLUGIN_REGISTRY", registry_path.string());
  std::vector<markql::cli::PluginRegistryEntry> entries;
  std::string error;
  bool ok = markql::cli::load_plugin_registry(entries, error);

  expect_true(!ok, "registry rejects unsafe plugin names");
  expect_true(error.find("invalid plugin name") != std::string::npos,
              "registry error reports invalid plugin name");

  std::filesystem::remove_all(temp_dir);
}

static void test_plugin_registry_rejects_path_traversal() {
  const auto temp_dir = make_temp_dir("markql_plugin_registry_path");
  const auto registry_path = temp_dir / "registry.json";
  {
    std::ofstream out(registry_path);
    out << R"([
  {
    "name": "safe_plugin",
    "repo": "https://example.com/plugin.git",
    "cmake_subdir": "../escape",
    "artifact": "libsafe_plugin{ext}"
  }
])";
  }

  ScopedEnvVar registry_env("MARKQL_PLUGIN_REGISTRY", registry_path.string());
  std::vector<markql::cli::PluginRegistryEntry> entries;
  std::string error;
  bool ok = markql::cli::load_plugin_registry(entries, error);

  expect_true(!ok, "registry rejects path traversal in plugin paths");
  expect_true(error.find("invalid plugin path") != std::string::npos,
              "registry error reports invalid plugin path");

  std::filesystem::remove_all(temp_dir);
}

static void test_plugin_remove_rejects_unsafe_name() {
  const auto temp_dir = make_temp_dir("markql_plugin_remove");
  std::filesystem::create_directories(temp_dir / "plugins" / "src");
  std::filesystem::create_directories(temp_dir / "outside-target");

  {
    ScopedCurrentPath current_path(temp_dir);
    StreamCapture capture(std::cerr);
    markql::cli::ReplConfig config;
    config.color = false;
    markql::cli::LineEditor editor(5, "markql> ", 8);
    std::unordered_map<std::string, markql::cli::LoadedSource> sources;
    std::string active_alias = "doc";
    std::string last_full_output;
    bool display_full = true;
    size_t max_rows = 40;
    std::vector<markql::ColumnNameMapping> last_schema_map;
    markql::cli::CommandRegistry registry;
    markql::cli::PluginManager plugin_manager(registry);
    markql::cli::CommandContext ctx{
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

    auto handler = markql::cli::make_plugin_command();
    bool handled = handler(".plugin remove ../../outside-target", ctx);

    expect_true(handled, ".plugin remove with unsafe name should be handled");
    expect_true(capture.str().find("invalid plugin name") != std::string::npos,
                ".plugin remove should reject unsafe names");
    expect_true(std::filesystem::exists(temp_dir / "outside-target"),
                "unsafe plugin remove should not delete traversed targets");
  }

  std::filesystem::remove_all(temp_dir);
}

void register_repl_tests(std::vector<TestCase>& tests) {
  tests.push_back({"summarize_content_basic", test_summarize_content_basic});
  tests.push_back({"summarize_content_khmer_requires_plugin",
                   test_summarize_content_khmer_requires_plugin});
  tests.push_back({"summarize_content_max_tokens",
                   test_summarize_content_max_tokens});
  tests.push_back({"sql_keyword_catalog_includes_new_tokens",
                   test_sql_keyword_catalog_includes_new_tokens});
  tests.push_back({"set_colnames_command", test_set_colnames_command});
  tests.push_back({"mode_command_accepts_csv", test_mode_command_accepts_csv});
  tests.push_back({"lint_command_toggles_on_and_off", test_lint_command_toggles_on_and_off});
  tests.push_back({"lint_command_rejects_invalid_value", test_lint_command_rejects_invalid_value});
  tests.push_back({"describe_last_command_outputs_map", test_describe_last_command_outputs_map});
  tests.push_back({"describe_last_command_outputs_csv", test_describe_last_command_outputs_csv});
  tests.push_back({"explore_command_uses_active_alias_by_default",
                   test_explore_command_uses_active_alias_by_default});
  tests.push_back({"explore_command_accepts_direct_target_and_alias_target",
                   test_explore_command_accepts_direct_target_and_alias_target});
  tests.push_back({"plugin_registry_rejects_unsafe_plugin_name",
                   test_plugin_registry_rejects_unsafe_plugin_name});
  tests.push_back({"plugin_registry_rejects_path_traversal",
                   test_plugin_registry_rejects_path_traversal});
  tests.push_back({"plugin_remove_rejects_unsafe_name",
                   test_plugin_remove_rejects_unsafe_name});
}
