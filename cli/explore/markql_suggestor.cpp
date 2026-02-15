#include "explore/markql_suggestor.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace xsql::cli {

namespace {

bool is_valid_markql_identifier(std::string_view text) {
  if (text.empty()) return false;
  unsigned char first = static_cast<unsigned char>(text.front());
  if (!(std::isalpha(first) || first == '_')) return false;
  for (char c : text) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (!(std::isalnum(uc) || c == '_')) return false;
  }
  return true;
}

std::string escape_single_quotes(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    out.push_back(c);
    if (c == '\'') out.push_back('\'');
  }
  return out;
}

std::string sql_quote(std::string_view text) { return "'" + escape_single_quotes(text) + "'"; }

std::string first_class_token(const xsql::HtmlNode& node) {
  auto it = node.attributes.find("class");
  if (it == node.attributes.end()) return "";
  const std::string& cls = it->second;
  size_t i = 0;
  while (i < cls.size() && std::isspace(static_cast<unsigned char>(cls[i]))) ++i;
  size_t start = i;
  while (i < cls.size() && !std::isspace(static_cast<unsigned char>(cls[i]))) ++i;
  if (i <= start) return "";
  return cls.substr(start, i - start);
}

std::string to_snake_case(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  bool prev_sep = false;
  for (char c : input) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc)) {
      out.push_back(static_cast<char>(std::tolower(uc)));
      prev_sep = false;
    } else if (!prev_sep) {
      out.push_back('_');
      prev_sep = true;
    }
  }
  while (!out.empty() && out.front() == '_') out.erase(out.begin());
  while (!out.empty() && out.back() == '_') out.pop_back();
  return out;
}

bool contains_ci(std::string_view text, std::string_view needle) {
  if (needle.empty() || text.empty()) return false;
  auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
  for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
    bool ok = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (lower(text[i + j]) != lower(needle[j])) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

std::vector<std::vector<int64_t>> build_children_index(const xsql::HtmlDocument& doc) {
  std::vector<std::vector<int64_t>> out(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (!node.parent_id.has_value()) continue;
    int64_t parent = *node.parent_id;
    if (parent < 0 || static_cast<size_t>(parent) >= out.size()) continue;
    out[static_cast<size_t>(parent)].push_back(node.id);
  }
  return out;
}

std::vector<int64_t> ancestor_chain(const xsql::HtmlDocument& doc, int64_t node_id) {
  std::vector<int64_t> chain;
  if (node_id < 0 || static_cast<size_t>(node_id) >= doc.nodes.size()) return chain;
  int64_t cur = node_id;
  size_t guard = 0;
  while (cur >= 0 && static_cast<size_t>(cur) < doc.nodes.size() && guard < doc.nodes.size()) {
    chain.push_back(cur);
    const auto& node = doc.nodes[static_cast<size_t>(cur)];
    if (!node.parent_id.has_value()) break;
    cur = *node.parent_id;
    ++guard;
  }
  return chain;
}

}  // namespace

MarkqlSuggestion suggest_markql_statement(const xsql::HtmlDocument& doc, int64_t selected_node_id) {
  MarkqlSuggestion suggestion;
  if (doc.nodes.empty()) {
    suggestion.reason = "empty document";
    return suggestion;
  }
  if (selected_node_id < 0 || static_cast<size_t>(selected_node_id) >= doc.nodes.size()) {
    suggestion.reason = "invalid selected node";
    return suggestion;
  }

  const auto children = build_children_index(doc);
  std::vector<int64_t> roots;
  roots.reserve(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (!node.parent_id.has_value() || *node.parent_id < 0 ||
        static_cast<size_t>(*node.parent_id) >= doc.nodes.size()) {
      roots.push_back(node.id);
    }
  }
  const auto chain = ancestor_chain(doc, selected_node_id);
  if (chain.empty()) {
    suggestion.reason = "unable to resolve node ancestry";
    return suggestion;
  }

  int64_t row_id = chain.front();
  int repeated_rows = 1;
  // WHY: choose the nearest ancestor that repeats among siblings; this is usually the "row" scope.
  for (int64_t candidate_id : chain) {
    const auto& candidate = doc.nodes[static_cast<size_t>(candidate_id)];

    int same_tag_count = 0;
    if (candidate.parent_id.has_value() && *candidate.parent_id >= 0 &&
        static_cast<size_t>(*candidate.parent_id) < children.size()) {
      // WHY: sibling repetition is the strongest signal of a list/table row container.
      int64_t parent_id = *candidate.parent_id;
      for (int64_t sibling_id : children[static_cast<size_t>(parent_id)]) {
        if (sibling_id < 0 || static_cast<size_t>(sibling_id) >= doc.nodes.size()) continue;
        if (doc.nodes[static_cast<size_t>(sibling_id)].tag == candidate.tag) ++same_tag_count;
      }
    } else {
      // WHY: for roots, use repetition across roots as a weaker fallback.
      for (int64_t root_id : roots) {
        if (root_id < 0 || static_cast<size_t>(root_id) >= doc.nodes.size()) continue;
        if (doc.nodes[static_cast<size_t>(root_id)].tag == candidate.tag) ++same_tag_count;
      }
    }
    if (same_tag_count >= 2) {
      row_id = candidate_id;
      repeated_rows = same_tag_count;
      break;
    }
  }

  const auto& row = doc.nodes[static_cast<size_t>(row_id)];
  const auto& selected = doc.nodes[static_cast<size_t>(selected_node_id)];
  const bool row_tag_valid = is_valid_markql_identifier(row.tag);
  const bool selected_tag_valid = is_valid_markql_identifier(selected.tag);

  std::vector<std::string> where_clauses;
  where_clauses.push_back("tag = " + sql_quote(row.tag));
  std::string row_class = first_class_token(row);
  if (!row_class.empty() && row_class.size() >= 3) {
    // WHY: class token is often stable across repeated rows and keeps query reusable.
    where_clauses.push_back("attributes.class CONTAINS " + sql_quote(row_class));
  } else {
    // WHY: id fallback gives determinism when class is missing/noisy.
    auto id_it = row.attributes.find("id");
    if (id_it != row.attributes.end() && !id_it->second.empty()) {
      where_clauses.push_back("attributes.id = " + sql_quote(id_it->second));
    }
  }

  std::vector<std::pair<std::string, std::string>> fields;
  std::unordered_set<std::string> names;
  auto add_field = [&](std::string name, std::string expr) {
    if (name.empty() || expr.empty()) return;
    name = to_snake_case(name);
    if (name.empty()) return;
    std::string final_name = name;
    int suffix = 2;
    // WHY: force unique aliases so generated statements parse without user edits.
    while (names.find(final_name) != names.end()) {
      final_name = name + "_" + std::to_string(suffix++);
    }
    names.insert(final_name);
    fields.push_back({final_name, expr});
  };

  auto selected_id_it = selected.attributes.find("id");
  if (selected_tag_valid && selected_id_it != selected.attributes.end() && !selected_id_it->second.empty()) {
    add_field(selected.tag + "_id", "ATTR(" + selected.tag + ", id)");
  }

  std::optional<std::string> title_selector;
  std::optional<std::string> title_predicate;
  auto classify_title_candidate = [&](const xsql::HtmlNode& node) {
    std::string cls = first_class_token(node);
    // WHY: mix tag hints + class hints because many pages encode titles via either structure.
    bool is_title_like = contains_ci(node.tag, "h1") || contains_ci(node.tag, "h2") ||
                         contains_ci(node.tag, "h3") || contains_ci(node.tag, "th") ||
                         contains_ci(node.tag, "strong") || contains_ci(node.tag, "b") ||
                         contains_ci(cls, "title") || contains_ci(cls, "header") ||
                         contains_ci(cls, "name");
    return std::pair<bool, std::string>{is_title_like, cls};
  };

  if (selected_tag_valid) {
    auto [title_like, cls] = classify_title_candidate(selected);
    if (title_like || !selected.text.empty()) {
      title_selector = selected.tag;
      if (!cls.empty() && cls.size() >= 3) title_predicate = cls;
    }
  }
  if (!title_selector.has_value()) {
    // WHY: if selected node is not a good title carrier, prefer a direct child before deeper search.
    for (int64_t child_id : children[static_cast<size_t>(row_id)]) {
      if (child_id < 0 || static_cast<size_t>(child_id) >= doc.nodes.size()) continue;
      const auto& child = doc.nodes[static_cast<size_t>(child_id)];
      if (!is_valid_markql_identifier(child.tag)) continue;
      auto [title_like, cls] = classify_title_candidate(child);
      if (!title_like && child.text.empty()) continue;
      title_selector = child.tag;
      if (!cls.empty() && cls.size() >= 3) title_predicate = cls;
      break;
    }
  }
  if (title_selector.has_value()) {
    if (title_predicate.has_value()) {
      add_field("title",
                "TEXT(" + *title_selector + " WHERE attributes.class CONTAINS " +
                    sql_quote(*title_predicate) + ")");
    } else {
      add_field("title", "TEXT(" + *title_selector + ")");
    }
  }

  bool has_anchor = false;
  size_t guard = 0;
  std::vector<int64_t> stack;
  stack.push_back(row_id);
  // WHY: bounded DFS cheaply detects common link fields without full-feature schema inference.
  while (!stack.empty() && guard < doc.nodes.size() * 2) {
    int64_t cur = stack.back();
    stack.pop_back();
    if (cur >= 0 && static_cast<size_t>(cur) < doc.nodes.size()) {
      const auto& node = doc.nodes[static_cast<size_t>(cur)];
      if (node.tag == "a") {
        has_anchor = true;
        break;
      }
      if (cur >= 0 && static_cast<size_t>(cur) < children.size()) {
        for (int64_t child_id : children[static_cast<size_t>(cur)]) {
          stack.push_back(child_id);
        }
      }
    }
    ++guard;
  }
  if (has_anchor) {
    add_field("link_text", "TEXT(a)");
    add_field("link_href", "ATTR(a, href)");
  }

  if (fields.empty()) {
    add_field("content", "TEXT(self)");
  }

  // WHY: PROJECT is only suggested when repeated shape + multiple extractable fields indicate table-like data.
  bool use_project = row_tag_valid && repeated_rows >= 2 && fields.size() >= 2;
  int confidence = 35;
  if (repeated_rows >= 2) confidence += 25;
  if (fields.size() >= 2) confidence += 20;
  if (!row_class.empty()) confidence += 10;
  if (selected_id_it != selected.attributes.end()) confidence += 10;
  if (confidence > 95) confidence = 95;

  std::string where_sql;
  for (size_t i = 0; i < where_clauses.size(); ++i) {
    where_sql += where_clauses[i];
    if (i + 1 < where_clauses.size()) where_sql += "\n  AND ";
  }

  if (use_project) {
    suggestion.strategy = MarkqlSuggestionStrategy::Project;
    suggestion.reason =
        "repeated row shape detected (" + std::to_string(repeated_rows) + ") with extractable fields";
    std::string sql = "SELECT " + row.tag + ".node_id,\n       PROJECT(" + row.tag + ") AS (\n";
    for (size_t i = 0; i < fields.size(); ++i) {
      sql += "         " + fields[i].first + ": " + fields[i].second;
      sql += (i + 1 < fields.size()) ? ",\n" : "\n";
    }
    sql += "       )\nFROM doc\nWHERE " + where_sql + "\nORDER BY node_id;";
    suggestion.statement = std::move(sql);
  } else {
    suggestion.strategy = MarkqlSuggestionStrategy::Flatten;
    suggestion.reason =
        "row pattern is weak for PROJECT; flattening is safer for first-pass extraction";
    if (row_tag_valid) {
      // WHY: include explicit depth + alias tuple because parser expects AS (...) for FLATTEN output.
      suggestion.statement = "SELECT " + row.tag + ".node_id,\n"
                            "       FLATTEN(" +
                            row.tag +
                            ", 2) AS (flat_text)\n"
                            "FROM doc\n"
                            "WHERE " +
                            where_sql +
                            "\n"
                            "ORDER BY node_id;";
    } else {
      suggestion.statement = "SELECT self.node_id,\n"
                            "       TEXT(self) AS text\n"
                            "FROM doc\n"
                            "WHERE " +
                            where_sql +
                            "\n"
                            "ORDER BY node_id;";
    }
  }
  suggestion.confidence = std::max(10, confidence - (use_project ? 0 : 10));
  return suggestion;
}

}  // namespace xsql::cli
