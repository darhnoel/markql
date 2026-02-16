#include <optional>
#include <string>
#include <vector>

#include "test_harness.h"

#include "explore/inner_html_search.h"

namespace {

void test_fuzzy_match_score_basic() {
  size_t pos = 0;
  int score = 0;
  bool ok = xsql::cli::fuzzy_match_score("<div>International scheduled flight</div>",
                                         "intsch",
                                         &pos,
                                         &score);
  expect_true(ok, "fuzzy score should match ordered subsequence");
  expect_true(pos < 20, "fuzzy first position should point near first letters");
  expect_true(score > 0, "fuzzy score should be positive for valid match");
}

void test_fuzzy_search_orders_and_snippet() {
  xsql::HtmlDocument doc;
  xsql::HtmlNode n0;
  n0.id = 0;
  n0.inner_html = "<span>noise</span>";
  xsql::HtmlNode n1;
  n1.id = 1;
  n1.inner_html = "<p>t a r g e t scattered text</p>";
  xsql::HtmlNode n2;
  n2.id = 2;
  n2.inner_html = "<p>target appears contiguously</p>";
  doc.nodes = {n0, n1, n2};

  auto matches = xsql::cli::fuzzy_search_inner_html(doc, "target", 10);
  expect_true(matches.size() == 2, "search should return matching nodes only");
  expect_true(matches[0].node_id == 2, "contiguous match should rank first");
  expect_true(matches[0].snippet.find("target") != std::string::npos,
              "snippet should include matched term");
}

void test_make_inner_html_snippet_context() {
  std::string html = "<div>alpha beta gamma delta epsilon zeta eta theta iota</div>";
  std::string snippet = xsql::cli::make_inner_html_snippet(html, 20, 5, 24);
  expect_true(!snippet.empty(), "snippet should not be empty");
  expect_true(snippet.find("gamma") != std::string::npos ||
                  snippet.find("delta") != std::string::npos,
              "snippet should include nearby context around match");
}

void test_fuzzy_search_candidate_subset() {
  xsql::HtmlDocument doc;
  xsql::HtmlNode n0;
  n0.id = 0;
  n0.inner_html = "<div>target appears here</div>";
  xsql::HtmlNode n1;
  n1.id = 1;
  n1.inner_html = "<div>no match</div>";
  xsql::HtmlNode n2;
  n2.id = 2;
  n2.inner_html = "<div>target is also here</div>";
  doc.nodes = {n0, n1, n2};

  std::vector<int64_t> candidates = {2};
  auto matches = xsql::cli::fuzzy_search_inner_html(doc, "target", 10, false, false, &candidates);
  expect_true(matches.size() == 1, "candidate subset should restrict search to listed nodes");
  expect_true(matches[0].node_id == 2, "candidate subset should return only candidate match");
}

void test_fuzzy_search_prioritizes_word_level_match() {
  xsql::HtmlDocument doc;
  xsql::HtmlNode n0;
  n0.id = 0;
  n0.inner_html = "<p>xflightx in token</p>";
  xsql::HtmlNode n1;
  n1.id = 1;
  n1.inner_html = "<p>flight appears as a word</p>";
  xsql::HtmlNode n2;
  n2.id = 2;
  n2.inner_html = "<p>prefix flightDeck word-start but not whole</p>";
  doc.nodes = {n0, n1, n2};

  auto matches = xsql::cli::fuzzy_search_inner_html(doc, "flight", 10, false, true);
  expect_true(matches.size() == 3, "all nodes should match query");
  expect_true(matches[0].node_id == 1,
              "whole-word contiguous match should rank above mid-word contiguous match");
}

void test_exact_search_case_insensitive_contiguous_only() {
  xsql::HtmlDocument doc;
  xsql::HtmlNode n0;
  n0.id = 0;
  n0.inner_html = "<p>t a r g e t letters split</p>";
  xsql::HtmlNode n1;
  n1.id = 1;
  n1.inner_html = "<p>TARGET appears contiguously</p>";
  xsql::HtmlNode n2;
  n2.id = 2;
  n2.inner_html = "<p>unrelated text</p>";
  doc.nodes = {n0, n1, n2};

  auto matches = xsql::cli::exact_search_inner_html(doc, "target", 10, false, true);
  expect_true(matches.size() == 1, "exact search should require contiguous query match");
  expect_true(matches[0].node_id == 1, "exact search should be case-insensitive");
}

void test_exact_search_prioritizes_whole_word() {
  xsql::HtmlDocument doc;
  xsql::HtmlNode n0;
  n0.id = 0;
  n0.inner_html = "<p>xflightx in token</p>";
  xsql::HtmlNode n1;
  n1.id = 1;
  n1.inner_html = "<p>flight appears as a word</p>";
  xsql::HtmlNode n2;
  n2.id = 2;
  n2.inner_html = "<p>prefix flightDeck word-start but not whole</p>";
  doc.nodes = {n0, n1, n2};

  auto matches = xsql::cli::exact_search_inner_html(doc, "flight", 10, false, true);
  expect_true(matches.size() == 3, "exact search should match all contiguous occurrences");
  expect_true(matches[0].node_id == 1,
              "whole-word contiguous match should rank above mid-word contiguous match");
}

void test_exact_search_prioritizes_closer_container_depth() {
  xsql::HtmlDocument doc;
  xsql::HtmlNode n0;
  n0.id = 0;
  n0.tag = "tbody";
  n0.inner_html = "<tr><th><div id=\"Lists_of_Unicode\">Lists</div></th></tr>";
  n0.parent_id = std::nullopt;

  xsql::HtmlNode n1;
  n1.id = 1;
  n1.tag = "th";
  n1.inner_html = "<div id=\"Lists_of_Unicode\">Lists</div>";
  n1.parent_id = 0;

  xsql::HtmlNode n2;
  n2.id = 2;
  n2.tag = "div";
  n2.attributes = {{"id", "Lists_of_Unicode"}};
  n2.inner_html = "<a href=\"/wiki/Unicode\">Unicode</a>";
  n2.parent_id = 1;

  doc.nodes = {n0, n1, n2};

  auto matches = xsql::cli::exact_search_inner_html(doc, "lists_of_unicode", 10, false, true);
  expect_true(matches.size() == 3, "all ancestor containers should match by inner_html");
  expect_true(matches[0].node_id == 2, "direct div attribute hit should rank before ancestor descendants");
  expect_true(matches[0].source_priority > matches[1].source_priority,
              "self-attribute source priority should outrank descendant source priority");
}

void test_exact_search_source_priority_overrides_depth() {
  xsql::HtmlDocument doc;
  xsql::HtmlNode n0;
  n0.id = 0;
  n0.tag = "div";
  n0.attributes = {{"id", "lists_of_unicode"}};
  n0.inner_html = "<section><span>lists_of_unicode</span></section>";
  n0.parent_id = std::nullopt;

  xsql::HtmlNode n1;
  n1.id = 1;
  n1.tag = "section";
  n1.inner_html = "<span>lists_of_unicode</span>";
  n1.parent_id = 0;

  doc.nodes = {n0, n1};

  auto matches = xsql::cli::exact_search_inner_html(doc, "lists_of_unicode", 10, false, true);
  expect_true(matches.size() == 2, "both nodes should match across supported search scopes");
  expect_true(matches[0].node_id == 0, "self-attribute hit should rank above deeper descendant-only hit");
}

}  // namespace

void register_explore_search_tests(std::vector<TestCase>& tests) {
  tests.push_back({"fuzzy_match_score_basic", test_fuzzy_match_score_basic});
  tests.push_back({"fuzzy_search_orders_and_snippet", test_fuzzy_search_orders_and_snippet});
  tests.push_back({"make_inner_html_snippet_context", test_make_inner_html_snippet_context});
  tests.push_back({"fuzzy_search_candidate_subset", test_fuzzy_search_candidate_subset});
  tests.push_back({"fuzzy_search_prioritizes_word_level_match",
                   test_fuzzy_search_prioritizes_word_level_match});
  tests.push_back({"exact_search_case_insensitive_contiguous_only",
                   test_exact_search_case_insensitive_contiguous_only});
  tests.push_back({"exact_search_prioritizes_whole_word", test_exact_search_prioritizes_whole_word});
  tests.push_back({"exact_search_prioritizes_closer_container_depth",
                   test_exact_search_prioritizes_closer_container_depth});
  tests.push_back({"exact_search_source_priority_overrides_depth",
                   test_exact_search_source_priority_overrides_depth});
}
