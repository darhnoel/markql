#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "test_harness.h"

#include "repl/commands/registry.h"
#include "repl/commands/describe_last_command.h"
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

}  // namespace

static void test_summarize_content_basic() {
  StreamCapture capture(std::cout);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  xsql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  sources["doc"] = xsql::cli::LoadedSource{
      "inline", std::optional<std::string>("<html><body><div>Hello Khmer World</div></body></html>")};
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<xsql::ColumnNameMapping> last_schema_map;

  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
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

  auto handler = xsql::cli::make_summarize_content_command();
  bool handled = handler(".summarize_content", ctx);
  expect_true(handled, "summarize_content should handle command");
  std::string output = capture.str();
  expect_true(output.find("hello") != std::string::npos, "output should include token 'hello'");
}

static void test_summarize_content_khmer_requires_plugin() {
  StreamCapture capture(std::cerr);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  xsql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  sources["doc"] = xsql::cli::LoadedSource{
      "inline", std::optional<std::string>("<html><body><div>សូមអរគុណ</div></body></html>")};
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<xsql::ColumnNameMapping> last_schema_map;

  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
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

  auto handler = xsql::cli::make_summarize_content_command();
  bool handled = handler(".summarize_content --lang khmer", ctx);
  expect_true(handled, "summarize_content should handle khmer command");
  std::string output = capture.str();
  expect_true(output.find(".plugin install khmer_segmenter") != std::string::npos,
              "missing plugin should suggest .plugin install khmer_segmenter");
}

static void test_summarize_content_max_tokens() {
  StreamCapture capture(std::cout);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  config.input = "";

  xsql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  sources["doc"] = xsql::cli::LoadedSource{
      "inline",
      std::optional<std::string>("<html><body><h3>Alpha Alpha Beta</h3><p>Gamma</p></body></html>")};
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<xsql::ColumnNameMapping> last_schema_map;

  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
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

  auto handler = xsql::cli::make_summarize_content_command();
  bool handled = handler(".summarize_content --max_tokens 1", ctx);
  expect_true(handled, "summarize_content should handle max_tokens");
  std::string output = capture.str();
  expect_true(output.find("alpha") != std::string::npos, "output should include token 'alpha'");
  expect_true(output.find("beta") == std::string::npos, "output should not include token 'beta'");
  expect_true(output.find("gamma") == std::string::npos, "output should not include token 'gamma'");
}

static void test_sql_keyword_catalog_includes_new_tokens() {
  expect_true(xsql::cli::is_sql_keyword_token("case"), "CASE should be highlighted as keyword");
  expect_true(xsql::cli::is_sql_keyword_token("WHEN"), "WHEN should be highlighted as keyword");
  expect_true(xsql::cli::is_sql_keyword_token("ndjson"), "NDJSON should be highlighted as keyword");
  expect_true(!xsql::cli::is_sql_keyword_token("first_text"),
              "FIRST_TEXT is a function-like identifier, not a reserved keyword");
  expect_true(!xsql::cli::is_sql_keyword_token("doc"),
              "doc should be treated as a source name, not a reserved keyword");
  expect_true(!xsql::cli::is_sql_keyword_token("document"),
              "document should be treated as a source name, not a reserved keyword");
  expect_true(!xsql::cli::is_sql_keyword_token("table"),
              "table should not be highlighted as a reserved SQL keyword");
  expect_true(!xsql::cli::is_sql_keyword_token("view"),
              "view names should not be highlighted as reserved keywords");
}

static void test_set_colnames_command() {
  StreamCapture capture(std::cout);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  xsql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<xsql::ColumnNameMapping> last_schema_map;
  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
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
  auto handler = xsql::cli::make_set_command();
  bool handled = handler(".set colnames raw", ctx);
  expect_true(handled, "set command handles colnames raw");
  expect_true(config.colname_mode == xsql::ColumnNameMode::Raw, "set command updates mode");
}

static void test_describe_last_command_outputs_map() {
  StreamCapture capture(std::cout);
  xsql::cli::ReplConfig config;
  config.output_mode = "duckbox";
  config.color = false;
  config.highlight = false;
  xsql::cli::LineEditor editor(5, "markql> ", 8);
  std::unordered_map<std::string, xsql::cli::LoadedSource> sources;
  std::string active_alias = "doc";
  std::string last_full_output;
  bool display_full = true;
  size_t max_rows = 40;
  std::vector<xsql::ColumnNameMapping> last_schema_map = {
      {"data-id", "data_id"},
      {"data_id", "data_id__2"},
  };
  xsql::cli::CommandRegistry registry;
  xsql::cli::PluginManager plugin_manager(registry);
  xsql::cli::CommandContext ctx{
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
  auto handler = xsql::cli::make_describe_last_command();
  bool handled = handler("DESCRIBE LAST;", ctx);
  expect_true(handled, "describe last command handles statement");
  std::string output = capture.str();
  expect_true(output.find("raw_name") != std::string::npos, "describe last header");
  expect_true(output.find("data-id") != std::string::npos, "describe last raw value");
  expect_true(output.find("data_id__2") != std::string::npos, "describe last output value");
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
  tests.push_back({"describe_last_command_outputs_map", test_describe_last_command_outputs_map});
}
