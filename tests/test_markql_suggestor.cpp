#include <optional>
#include <string>
#include <vector>

#include "test_harness.h"

#include "explore/markql_suggestor.h"

namespace {

void test_suggestor_prefers_project_for_repeated_rows() {
  xsql::HtmlDocument doc;
  xsql::HtmlNode row0;
  row0.id = 0;
  row0.tag = "li";
  row0.attributes = {{"class", "card"}};
  row0.parent_id = std::nullopt;

  xsql::HtmlNode row1;
  row1.id = 1;
  row1.tag = "li";
  row1.attributes = {{"class", "card"}};
  row1.parent_id = std::nullopt;

  xsql::HtmlNode h2_0;
  h2_0.id = 2;
  h2_0.tag = "h2";
  h2_0.text = "First";
  h2_0.parent_id = 0;

  xsql::HtmlNode a_0;
  a_0.id = 3;
  a_0.tag = "a";
  a_0.text = "Read";
  a_0.attributes = {{"href", "/first"}};
  a_0.parent_id = 0;

  xsql::HtmlNode h2_1;
  h2_1.id = 4;
  h2_1.tag = "h2";
  h2_1.text = "Second";
  h2_1.parent_id = 1;

  xsql::HtmlNode a_1;
  a_1.id = 5;
  a_1.tag = "a";
  a_1.text = "Read";
  a_1.attributes = {{"href", "/second"}};
  a_1.parent_id = 1;

  doc.nodes = {row0, row1, h2_0, a_0, h2_1, a_1};

  auto suggestion = xsql::cli::suggest_markql_statement(doc, 2);
  expect_true(suggestion.strategy == xsql::cli::MarkqlSuggestionStrategy::Project,
              "suggestor should choose PROJECT for repeated row shape");
  expect_true(suggestion.statement.find("PROJECT(li)") != std::string::npos,
              "project suggestion should include PROJECT(li)");
  expect_true(suggestion.statement.find("link_href") != std::string::npos,
              "project suggestion should include link_href field");
}

void test_suggestor_falls_back_to_flatten_for_weak_shape() {
  xsql::HtmlDocument doc;
  xsql::HtmlNode node;
  node.id = 0;
  node.tag = "article";
  node.text = "single block";
  node.attributes = {{"id", "solo"}};
  node.parent_id = std::nullopt;
  doc.nodes = {node};

  auto suggestion = xsql::cli::suggest_markql_statement(doc, 0);
  expect_true(suggestion.strategy == xsql::cli::MarkqlSuggestionStrategy::Flatten,
              "suggestor should fallback to FLATTEN for non-repeating shape");
  expect_true(suggestion.statement.find("FLATTEN(article, 2) AS (flat_text)") != std::string::npos,
              "fallback suggestion should emit valid FLATTEN alias syntax");
}

}  // namespace

void register_markql_suggestor_tests(std::vector<TestCase>& tests) {
  tests.push_back({"suggestor_prefers_project_for_repeated_rows",
                   test_suggestor_prefers_project_for_repeated_rows});
  tests.push_back({"suggestor_falls_back_to_flatten_for_weak_shape",
                   test_suggestor_falls_back_to_flatten_for_weak_shape});
}
