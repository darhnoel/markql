#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "html_parser.h"

namespace xsql::cli {

/// Represents one visible row in the tree panel.
/// MUST preserve preorder traversal and indentation depth for rendering.
struct VisibleTreeRow {
  int64_t node_id = 0;
  int depth = 0;
};

/// Builds child adjacency lists indexed by node_id.
/// MUST ignore malformed parent references and preserve document order.
std::vector<std::vector<int64_t>> build_dom_children_index(const xsql::HtmlDocument& doc);
/// Collects root node_ids in document order.
/// MUST include nodes with no parent_id and skip malformed references.
std::vector<int64_t> collect_dom_root_ids(const xsql::HtmlDocument& doc);
/// Flattens currently visible rows based on expansion state.
/// MUST keep preorder traversal and include collapsed nodes without descendants.
std::vector<VisibleTreeRow> flatten_visible_tree(
    const std::vector<int64_t>& roots,
    const std::vector<std::vector<int64_t>>& children,
    const std::unordered_set<int64_t>& expanded_node_ids);
/// Renders attribute panel lines for the selected node.
/// MUST sort attribute keys lexicographically and always include a title line.
std::vector<std::string> render_attribute_lines(const xsql::HtmlNode& node);

/// Runs the standalone DOM explorer UI for a file path or URL input.
/// MUST return 0 on normal exit and non-zero on usage/IO/runtime errors.
int run_dom_explorer_from_input(const std::string& input, std::ostream& err);

}  // namespace xsql::cli
