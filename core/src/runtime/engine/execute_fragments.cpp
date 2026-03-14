#include "markql/markql.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "../../dom/html_parser.h"
#include "../../util/string_util.h"
#include "engine_execution_internal.h"
#include "markql_internal.h"

namespace markql {

std::optional<std::string> field_value_string(const QueryResultRow& row, const std::string& field) {
  if (field == "node_id") return std::to_string(row.node_id);
  if (field == "count") return std::to_string(row.node_id);
  if (field == "tag") return row.tag;
  if (field == "text") return row.text;
  if (field == "inner_html") return row.inner_html;
  if (field == "parent_id") {
    if (!row.parent_id.has_value()) return std::nullopt;
    return std::to_string(*row.parent_id);
  }
  if (field == "sibling_pos") return std::to_string(row.sibling_pos);
  if (field == "max_depth") return std::to_string(row.max_depth);
  if (field == "doc_order") return std::to_string(row.doc_order);
  if (field == "source_uri") return row.source_uri;
  if (field == "attributes") return std::nullopt;
  auto computed = row.computed_fields.find(field);
  if (computed != row.computed_fields.end()) return computed->second;
  auto it = row.attributes.find(field);
  if (it == row.attributes.end()) return std::nullopt;
  return it->second;
}

bool looks_like_html_fragment(const std::string& value) {
  return value.find('<') != std::string::npos && value.find('>') != std::string::npos;
}

void append_document(HtmlDocument& target, const HtmlDocument& source) {
  const int64_t offset = static_cast<int64_t>(target.nodes.size());
  target.nodes.reserve(target.nodes.size() + source.nodes.size());
  for (const auto& node : source.nodes) {
    HtmlNode copy = node;
    copy.id = node.id + offset;
    copy.doc_order = node.doc_order + offset;
    if (node.parent_id.has_value()) {
      copy.parent_id = *node.parent_id + offset;
    }
    target.nodes.push_back(std::move(copy));
  }
}

HtmlDocument build_fragments_document(const FragmentSource& fragments) {
  HtmlDocument merged;
  for (const auto& fragment : fragments.fragments) {
    HtmlDocument doc = parse_html(fragment);
    append_document(merged, doc);
  }
  return merged;
}

FragmentSource collect_html_fragments(const QueryResult& result, const std::string& source_name) {
  if (result.to_table || !result.tables.empty()) {
    throw std::runtime_error(source_name + " does not accept TO TABLE() results");
  }
  if (result.columns.size() != 1) {
    throw std::runtime_error(source_name + " expects a single HTML string column");
  }
  const std::string& field = result.columns[0];
  FragmentSource out;
  size_t total_bytes = 0;
  for (const auto& row : result.rows) {
    std::optional<std::string> value = field_value_string(row, field);
    if (!value.has_value()) {
      throw std::runtime_error(
          source_name + " expects HTML strings (use inner_html(...) or RAW('<...>'))");
    }
    std::string trimmed = util::trim_ws(*value);
    if (trimmed.empty()) {
      continue;
    }
    if (!looks_like_html_fragment(trimmed)) {
      throw std::runtime_error(
          source_name + " expects HTML strings (use inner_html(...) or RAW('<...>'))");
    }
    if (trimmed.size() > markql_internal::kMaxFragmentHtmlBytes) {
      throw std::runtime_error(source_name + " HTML fragment exceeds maximum size");
    }
    total_bytes += trimmed.size();
    if (out.fragments.size() >= markql_internal::kMaxFragmentCount) {
      throw std::runtime_error(source_name + " exceeds maximum fragment count");
    }
    if (total_bytes > markql_internal::kMaxFragmentBytes) {
      throw std::runtime_error(source_name + " exceeds maximum total HTML size");
    }
    out.fragments.push_back(std::move(trimmed));
  }
  if (out.fragments.empty()) {
    throw std::runtime_error(source_name + " produced no HTML fragments");
  }
  return out;
}

}  // namespace markql
