#include "test_harness.h"

#include <filesystem>
#include <string>
#include <vector>

#include "render/query_template_renderer.h"
#include "test_utils.h"

namespace {

std::filesystem::path repo_path(const std::string& relative) {
  return std::filesystem::path(__FILE__).parent_path().parent_path() / relative;
}

void test_query_template_renderer_matches_golden_output() {
  const auto template_path = repo_path("tests/fixtures/render/golden_query.mql.j2");
  const auto vars_path = repo_path("tests/fixtures/render/golden_query.toml");
  const auto golden_path = repo_path("tests/golden/render/golden_query.mql");

  markql::cli::QueryRenderResult result =
      markql::cli::load_query_file_text(template_path.string(), "j2", vars_path.string());

  expect_true(result.rendered, "template query reports rendered state");
  expect_true(result.text == read_file_to_string(golden_path), "rendered query matches golden output");
}

void test_query_template_renderer_writes_exact_output_file() {
  const auto template_path = repo_path("tests/fixtures/render/golden_query.mql.j2");
  const auto vars_path = repo_path("tests/fixtures/render/golden_query.toml");
  const std::filesystem::path out_path =
      std::filesystem::temp_directory_path() / "markql_rendered_query_test.mql";

  markql::cli::QueryRenderResult result =
      markql::cli::load_query_file_text(template_path.string(), "j2", vars_path.string());
  markql::cli::write_rendered_query_output(out_path.string(), result.text);

  expect_true(read_file_to_string(out_path) == result.text,
              "rendered output file preserves exact bytes");
  std::filesystem::remove(out_path);
}

void test_query_template_renderer_requires_defined_variables() {
  const auto template_path = repo_path("tests/fixtures/render/golden_query.mql.j2");
  bool threw = false;
  try {
    (void)markql::cli::load_query_file_text(template_path.string(), "j2", "");
  } catch (const markql::cli::QueryRenderError& ex) {
    threw = true;
    expect_true(std::string(ex.what()).find("Missing template variable") != std::string::npos,
                "missing variable error is explicit");
  }
  expect_true(threw, "strict undefined behavior rejects missing variables");
}

void test_query_template_renderer_rejects_invalid_toml() {
  const auto template_path = repo_path("tests/fixtures/render/golden_query.mql.j2");
  const auto vars_path = repo_path("tests/fixtures/render/invalid_vars.toml");
  bool threw = false;
  try {
    (void)markql::cli::load_query_file_text(template_path.string(), "j2", vars_path.string());
  } catch (const markql::cli::QueryRenderError& ex) {
    threw = true;
    expect_true(std::string(ex.what()).find("Invalid TOML vars file") != std::string::npos,
                "invalid TOML error is explicit");
  }
  expect_true(threw, "invalid TOML is rejected");
}

void test_query_template_renderer_rejects_invalid_template_syntax() {
  const auto template_path = repo_path("tests/fixtures/render/invalid_template.mql.j2");
  const auto vars_path = repo_path("tests/fixtures/render/golden_query.toml");
  bool threw = false;
  try {
    (void)markql::cli::load_query_file_text(template_path.string(), "j2", vars_path.string());
  } catch (const markql::cli::QueryRenderError& ex) {
    threw = true;
    expect_true(std::string(ex.what()).find("Invalid Jinja2 template syntax") != std::string::npos,
                "template syntax error is explicit");
  }
  expect_true(threw, "invalid Jinja syntax is rejected");
}

}  // namespace

void register_query_template_renderer_tests(std::vector<TestCase>& tests) {
  tests.push_back({"query_template_renderer_matches_golden_output",
                   test_query_template_renderer_matches_golden_output});
  tests.push_back({"query_template_renderer_writes_exact_output_file",
                   test_query_template_renderer_writes_exact_output_file});
  tests.push_back({"query_template_renderer_requires_defined_variables",
                   test_query_template_renderer_requires_defined_variables});
  tests.push_back({"query_template_renderer_rejects_invalid_toml",
                   test_query_template_renderer_rejects_invalid_toml});
  tests.push_back({"query_template_renderer_rejects_invalid_template_syntax",
                   test_query_template_renderer_rejects_invalid_template_syntax});
}
