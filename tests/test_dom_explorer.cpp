#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "test_harness.h"

#include "explore/dom_explorer.h"

namespace {

std::string join_lines(const std::vector<std::string>& lines) {
  std::ostringstream oss;
  for (size_t i = 0; i < lines.size(); ++i) {
    oss << lines[i];
    if (i + 1 < lines.size()) oss << "\n";
  }
  return oss.str();
}

void test_flatten_visible_tree_order_and_depth() {
  xsql::HtmlDocument doc;
  doc.nodes = {
      xsql::HtmlNode{0, "html", "", "", {}, std::nullopt, 0, 0},
      xsql::HtmlNode{1, "body", "", "", {}, 0, 0, 0},
      xsql::HtmlNode{2, "div", "", "", {}, 1, 0, 0},
      xsql::HtmlNode{3, "span", "", "", {}, 2, 0, 0},
      xsql::HtmlNode{4, "p", "", "", {}, 1, 0, 0},
  };

  auto children = xsql::cli::build_dom_children_index(doc);
  auto roots = xsql::cli::collect_dom_root_ids(doc);

  std::unordered_set<int64_t> expanded = {0, 1};
  auto rows = xsql::cli::flatten_visible_tree(roots, children, expanded);
  std::vector<std::pair<int64_t, int>> expected = {
      {0, 0}, {1, 1}, {2, 2}, {4, 2},
  };
  expect_eq(rows.size(), expected.size(), "visible row count for partially expanded tree");
  for (size_t i = 0; i < expected.size(); ++i) {
    expect_true(rows[i].node_id == expected[i].first, "visible row node order");
    expect_true(rows[i].depth == expected[i].second, "visible row indentation depth");
  }

  expanded.insert(2);
  rows = xsql::cli::flatten_visible_tree(roots, children, expanded);
  expected = {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 2}};
  expect_eq(rows.size(), expected.size(), "visible row count for deeper expansion");
  for (size_t i = 0; i < expected.size(); ++i) {
    expect_true(rows[i].node_id == expected[i].first, "deeper visible row node order");
    expect_true(rows[i].depth == expected[i].second, "deeper visible row indentation depth");
  }
}

void test_render_attribute_lines_sorted_format() {
  xsql::HtmlNode node;
  node.id = 42;
  node.tag = "div";
  node.inner_html = "<td class=\"x\">  hello  </td>";
  node.attributes = {
      {"id", "offer-1"},
      {"class", "card featured"},
      {"data-testid", "price-main"},
  };
  auto lines = xsql::cli::render_attribute_lines(node);
  std::string actual = join_lines(lines);
  std::string expected =
      "node_id=42 tag=div\n"
      "inner_html_head = <td class=\"x\"> hello </td>\n"
      "class = card featured\n"
      "data-testid = price-main\n"
      "id = offer-1";
  expect_true(actual == expected, "attribute panel lines sorted and formatted");

  xsql::HtmlNode empty_node;
  empty_node.id = 7;
  empty_node.tag = "span";
  lines = xsql::cli::render_attribute_lines(empty_node);
  actual = join_lines(lines);
  expected =
      "node_id=7 tag=span\n"
      "inner_html_head = (empty)\n"
      "(no attributes)";
  expect_true(actual == expected, "attribute panel handles empty attributes");
}

}  // namespace

void register_dom_explorer_tests(std::vector<TestCase>& tests) {
  tests.push_back({"flatten_visible_tree_order_and_depth", test_flatten_visible_tree_order_and_depth});
  tests.push_back({"render_attribute_lines_sorted_format", test_render_attribute_lines_sorted_format});
}
