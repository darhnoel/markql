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

}  // namespace

void register_explore_search_tests(std::vector<TestCase>& tests) {
  tests.push_back({"fuzzy_match_score_basic", test_fuzzy_match_score_basic});
  tests.push_back({"fuzzy_search_orders_and_snippet", test_fuzzy_search_orders_and_snippet});
  tests.push_back({"make_inner_html_snippet_context", test_make_inner_html_snippet_context});
  tests.push_back({"fuzzy_search_candidate_subset", test_fuzzy_search_candidate_subset});
  tests.push_back({"fuzzy_search_prioritizes_word_level_match",
                   test_fuzzy_search_prioritizes_word_level_match});
}
