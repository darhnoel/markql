#include "explore/dom_explorer.h"

#include <utility>

namespace markql::cli {

std::vector<std::vector<int64_t>> build_dom_children_index(const markql::HtmlDocument& doc) {
  std::vector<std::vector<int64_t>> children(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (!node.parent_id.has_value()) continue;
    if (*node.parent_id < 0) continue;
    if (static_cast<size_t>(*node.parent_id) >= children.size()) continue;
    children[static_cast<size_t>(*node.parent_id)].push_back(node.id);
  }
  return children;
}

std::vector<int64_t> collect_dom_root_ids(const markql::HtmlDocument& doc) {
  std::vector<int64_t> roots;
  roots.reserve(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (!node.parent_id.has_value() || *node.parent_id < 0 ||
        static_cast<size_t>(*node.parent_id) >= doc.nodes.size()) {
      roots.push_back(node.id);
    }
  }
  return roots;
}

std::vector<VisibleTreeRow> flatten_visible_tree(
    const std::vector<int64_t>& roots, const std::vector<std::vector<int64_t>>& children,
    const std::unordered_set<int64_t>& expanded_node_ids) {
  std::vector<VisibleTreeRow> out;
  out.reserve(roots.size() * 2);
  std::vector<std::pair<int64_t, int>> stack;
  for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
    stack.push_back({*it, 0});
  }
  while (!stack.empty()) {
    auto [node_id, depth] = stack.back();
    stack.pop_back();
    out.push_back({node_id, depth});
    if (expanded_node_ids.find(node_id) == expanded_node_ids.end()) continue;
    if (node_id < 0 || static_cast<size_t>(node_id) >= children.size()) continue;
    const auto& kids = children[static_cast<size_t>(node_id)];
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
      stack.push_back({*it, depth + 1});
    }
  }
  return out;
}

}  // namespace markql::cli
