#include "markql/markql.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../executor/executor_internal.h"
#include "../../dom/html_parser.h"
#include "../../lang/markql_parser.h"
#include "../../util/string_util.h"
#include "dom_descendants_internal.h"
#include "dom_projection_internal.h"
#include "engine_execution_internal.h"
#include "markql_internal.h"

namespace markql {

namespace {

void collect_row_scope_nodes(const std::vector<std::vector<int64_t>>& children, int64_t node_id,
                             std::vector<int64_t>& out) {
  out.push_back(node_id);
  collect_descendants_any_depth(children, node_id, out);
}

std::string normalized_extract_text(const HtmlNode& node) {
  std::string direct = markql_internal::extract_direct_text_strict(node.inner_html);
  std::string normalized = normalize_flatten_text(direct);
  if (!normalized.empty()) return normalized;
  direct = markql_internal::extract_direct_text(node.inner_html);
  normalized = normalize_flatten_text(direct);
  if (!normalized.empty()) return normalized;
  return normalize_flatten_text(node.text);
}

ScalarProjectionValue make_null_projection() {
  return ScalarProjectionValue{};
}

ScalarProjectionValue make_string_projection(std::string value) {
  ScalarProjectionValue out;
  out.kind = ScalarProjectionValue::Kind::String;
  out.string_value = std::move(value);
  return out;
}

ScalarProjectionValue make_number_projection(int64_t value) {
  ScalarProjectionValue out;
  out.kind = ScalarProjectionValue::Kind::Number;
  out.number_value = value;
  return out;
}

std::optional<int64_t> projection_to_int(const ScalarProjectionValue& value) {
  if (value.kind == ScalarProjectionValue::Kind::Number) return value.number_value;
  if (value.kind == ScalarProjectionValue::Kind::String)
    return parse_int64_value(value.string_value);
  return std::nullopt;
}

std::optional<std::string> projection_operand_value(
    const Operand& operand, const HtmlNode& base_node, const HtmlDocument& doc,
    const std::vector<std::vector<int64_t>>& children);

std::optional<std::string> selector_value(
    const std::string& tag, const std::optional<std::string>& attr,
    const std::optional<Expr>& where, const std::optional<int64_t>& selector_index,
    bool selector_last, bool direct_text, const HtmlNode& base_node, const HtmlDocument& doc,
    const std::vector<std::vector<int64_t>>& children, ProjectRowEvalCache* row_cache) {
  const std::string extract_tag = util::to_lower(tag);
  std::vector<int64_t> uncached_scope_nodes;
  const std::vector<int64_t>* candidates = nullptr;
  if (row_cache != nullptr) {
    candidates = &row_cache->nodes_for_tag(extract_tag, doc);
  } else {
    uncached_scope_nodes.reserve(32);
    collect_row_scope_nodes(children, base_node.id, uncached_scope_nodes);
    candidates = &uncached_scope_nodes;
  }
  if (row_cache != nullptr && row_cache->stats != nullptr) {
    ++row_cache->stats->selector_calls;
  }
  int64_t seen = 0;
  std::optional<std::string> last_value;
  int64_t target = selector_index.value_or(1);
  for (int64_t id : *candidates) {
    if (row_cache != nullptr && row_cache->stats != nullptr) {
      ++row_cache->stats->candidate_nodes_examined;
    }
    const HtmlNode& node = doc.nodes.at(static_cast<size_t>(id));
    if (row_cache == nullptr && node.tag != extract_tag) continue;
    if (where.has_value() && !executor_internal::eval_expr(*where, doc, children, node)) {
      continue;
    }
    std::optional<std::string> value;
    if (attr.has_value()) {
      auto it = node.attributes.find(*attr);
      if (it == node.attributes.end() || it->second.empty()) continue;
      value = it->second;
    } else if (direct_text) {
      std::string direct =
          util::trim_ws(markql_internal::extract_direct_text_strict(node.inner_html));
      if (direct.empty()) continue;
      value = std::move(direct);
    } else {
      std::string text = normalized_extract_text(node);
      if (text.empty()) continue;
      value = std::move(text);
    }
    if (!value.has_value()) continue;
    if (selector_last) {
      last_value = std::move(value);
      continue;
    }
    ++seen;
    if (seen == target) return value;
  }
  if (selector_last) return last_value;
  return std::nullopt;
}

int64_t sibling_pos_for_projection(const HtmlDocument& doc,
                                   const std::vector<std::vector<int64_t>>& children,
                                   const HtmlNode& node) {
  if (!node.parent_id.has_value()) return 1;
  const auto& siblings = children.at(static_cast<size_t>(*node.parent_id));
  for (size_t i = 0; i < siblings.size(); ++i) {
    if (siblings[i] == node.id) return static_cast<int64_t>(i + 1);
  }
  return 1;
}

std::vector<const HtmlNode*> projection_axis_nodes(
    const HtmlDocument& doc, const std::vector<std::vector<int64_t>>& children,
    const HtmlNode& node, Operand::Axis axis) {
  std::vector<const HtmlNode*> out;
  if (axis == Operand::Axis::Self) {
    out.push_back(&node);
    return out;
  }
  if (axis == Operand::Axis::Parent) {
    if (node.parent_id.has_value()) {
      out.push_back(&doc.nodes.at(static_cast<size_t>(*node.parent_id)));
    }
    return out;
  }
  if (axis == Operand::Axis::Child) {
    for (int64_t id : children.at(static_cast<size_t>(node.id))) {
      out.push_back(&doc.nodes.at(static_cast<size_t>(id)));
    }
    return out;
  }
  if (axis == Operand::Axis::Ancestor) {
    const HtmlNode* current = &node;
    while (current->parent_id.has_value()) {
      const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*current->parent_id));
      out.push_back(&parent);
      current = &parent;
    }
    return out;
  }
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    out.push_back(&child);
    const auto& next = children.at(static_cast<size_t>(id));
    stack.insert(stack.end(), next.begin(), next.end());
  }
  return out;
}

std::optional<std::string> projection_operand_value(
    const Operand& operand, const HtmlNode& base_node, const HtmlDocument& doc,
    const std::vector<std::vector<int64_t>>& children) {
  std::vector<const HtmlNode*> candidates =
      projection_axis_nodes(doc, children, base_node, operand.axis);
  for (const HtmlNode* candidate : candidates) {
    if (candidate == nullptr) continue;
    switch (operand.field_kind) {
      case Operand::FieldKind::Attribute: {
        auto it = candidate->attributes.find(operand.attribute);
        if (it != candidate->attributes.end()) return it->second;
        break;
      }
      case Operand::FieldKind::Tag:
        return candidate->tag;
      case Operand::FieldKind::Text:
        return candidate->text;
      case Operand::FieldKind::NodeId:
        return std::to_string(candidate->id);
      case Operand::FieldKind::ParentId:
        if (candidate->parent_id.has_value()) return std::to_string(*candidate->parent_id);
        break;
      case Operand::FieldKind::SiblingPos:
        return std::to_string(sibling_pos_for_projection(doc, children, *candidate));
      case Operand::FieldKind::MaxDepth:
        return std::to_string(candidate->max_depth);
      case Operand::FieldKind::DocOrder:
        return std::to_string(candidate->doc_order);
      case Operand::FieldKind::AttributesMap:
        break;
    }
  }
  return std::nullopt;
}

}  // namespace

bool project_bench_stats_enabled() {
  static const bool enabled = []() {
    const char* raw = std::getenv("MARKQL_BENCH_STATS");
    if (raw == nullptr) return false;
    std::string value = util::to_lower(util::trim_ws(raw));
    return !value.empty() && value != "0" && value != "false" && value != "no" && value != "off";
  }();
  return enabled;
}

void maybe_emit_project_bench_stats(const ProjectBenchStats& stats) {
  if (!project_bench_stats_enabled()) return;
  if (stats.selector_calls == 0 && stats.scope_builds == 0 && stats.tag_cache_builds == 0 &&
      stats.candidate_nodes_examined == 0) {
    return;
  }
  std::fprintf(stderr,
               "[markql bench] project_selector_calls=%llu project_scope_builds=%llu "
               "project_tag_cache_builds=%llu project_candidate_nodes_examined=%llu\n",
               static_cast<unsigned long long>(stats.selector_calls),
               static_cast<unsigned long long>(stats.scope_builds),
               static_cast<unsigned long long>(stats.tag_cache_builds),
               static_cast<unsigned long long>(stats.candidate_nodes_examined));
}

void ProjectRowEvalCache::reset_for_row(const std::vector<std::vector<int64_t>>& children,
                                        int64_t node_id) {
  scope_nodes.clear();
  tag_nodes.clear();
  scope_nodes.reserve(32);
  collect_row_scope_nodes(children, node_id, scope_nodes);
  if (stats != nullptr) {
    ++stats->scope_builds;
  }
}

const std::vector<int64_t>& ProjectRowEvalCache::nodes_for_tag(const std::string& extract_tag,
                                                               const HtmlDocument& doc) {
  auto found = tag_nodes.find(extract_tag);
  if (found != tag_nodes.end()) {
    return found->second;
  }
  std::vector<int64_t> matches;
  matches.reserve(8);
  for (int64_t id : scope_nodes) {
    const HtmlNode& node = doc.nodes.at(static_cast<size_t>(id));
    if (node.tag == extract_tag) {
      matches.push_back(id);
    }
  }
  auto [it, _] = tag_nodes.emplace(extract_tag, std::move(matches));
  if (stats != nullptr) {
    ++stats->tag_cache_builds;
  }
  return it->second;
}

std::string normalize_flatten_text(const std::string& value) {
  std::string trimmed = util::trim_ws(value);
  std::string out;
  out.reserve(trimmed.size());
  bool in_space = false;
  for (char c : trimmed) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!in_space) {
        out.push_back(' ');
        in_space = true;
      }
      continue;
    }
    in_space = false;
    out.push_back(c);
  }
  return out;
}

bool projection_is_null(const ScalarProjectionValue& value) {
  return value.kind == ScalarProjectionValue::Kind::Null;
}

std::string projection_to_string(const ScalarProjectionValue& value) {
  if (value.kind == ScalarProjectionValue::Kind::Number) {
    return std::to_string(value.number_value);
  }
  return value.string_value;
}

ScalarProjectionValue eval_select_scalar_expr(const ScalarExpr& expr, const HtmlNode& node,
                                              const HtmlDocument* doc,
                                              const std::vector<std::vector<int64_t>>* children) {
  switch (expr.kind) {
    case ScalarExpr::Kind::NullLiteral:
      return make_null_projection();
    case ScalarExpr::Kind::StringLiteral:
      return make_string_projection(expr.string_value);
    case ScalarExpr::Kind::NumberLiteral:
      return make_number_projection(expr.number_value);
    case ScalarExpr::Kind::Operand: {
      const Operand& op = expr.operand;
      if (op.axis != Operand::Axis::Self || op.field_kind == Operand::FieldKind::SiblingPos) {
        if (doc == nullptr || children == nullptr) return make_null_projection();
        std::optional<std::string> value = projection_operand_value(op, node, *doc, *children);
        if (!value.has_value()) return make_null_projection();
        if (op.field_kind == Operand::FieldKind::NodeId ||
            op.field_kind == Operand::FieldKind::ParentId ||
            op.field_kind == Operand::FieldKind::SiblingPos ||
            op.field_kind == Operand::FieldKind::MaxDepth ||
            op.field_kind == Operand::FieldKind::DocOrder) {
          if (auto parsed = parse_int64_value(*value); parsed.has_value()) {
            return make_number_projection(*parsed);
          }
        }
        return make_string_projection(*value);
      }
      if (op.field_kind == Operand::FieldKind::Tag) return make_string_projection(node.tag);
      if (op.field_kind == Operand::FieldKind::Text) return make_string_projection(node.text);
      if (op.field_kind == Operand::FieldKind::NodeId) return make_number_projection(node.id);
      if (op.field_kind == Operand::FieldKind::ParentId) {
        if (!node.parent_id.has_value()) return make_null_projection();
        return make_number_projection(*node.parent_id);
      }
      if (op.field_kind == Operand::FieldKind::MaxDepth)
        return make_number_projection(node.max_depth);
      if (op.field_kind == Operand::FieldKind::DocOrder)
        return make_number_projection(node.doc_order);
      if (op.field_kind == Operand::FieldKind::Attribute) {
        auto it = node.attributes.find(op.attribute);
        if (it == node.attributes.end()) return make_null_projection();
        return make_string_projection(it->second);
      }
      return make_null_projection();
    }
    case ScalarExpr::Kind::SelfRef:
      return make_null_projection();
    case ScalarExpr::Kind::FunctionCall:
      break;
  }

  std::string fn = util::to_upper(expr.function_name);
  if (fn == "TEXT" || fn == "DIRECT_TEXT" || fn == "INNER_HTML" || fn == "RAW_INNER_HTML" ||
      fn == "ATTR") {
    if ((fn == "TEXT" || fn == "DIRECT_TEXT") && expr.args.size() != 1) {
      return make_null_projection();
    }
    if ((fn == "INNER_HTML" || fn == "RAW_INNER_HTML") &&
        (expr.args.empty() || expr.args.size() > 2)) {
      return make_null_projection();
    }
    if (fn == "ATTR" && expr.args.size() != 2) {
      return make_null_projection();
    }
    if (expr.args.empty()) return make_null_projection();

    const HtmlNode* target = nullptr;
    const ScalarExpr& first_arg = expr.args.front();
    if (first_arg.kind == ScalarExpr::Kind::SelfRef) {
      target = &node;
    } else {
      ScalarProjectionValue arg_value = eval_select_scalar_expr(first_arg, node, doc, children);
      if (projection_is_null(arg_value)) return make_null_projection();
      std::string tag = util::to_lower(projection_to_string(arg_value));
      if (node.tag != tag) return make_null_projection();
      target = &node;
    }
    if (target == nullptr) return make_null_projection();

    if (fn == "TEXT") return make_string_projection(target->text);
    if (fn == "DIRECT_TEXT") {
      return make_string_projection(
          markql_internal::extract_direct_text_strict(target->inner_html));
    }
    if (fn == "ATTR") {
      ScalarProjectionValue attr_value = eval_select_scalar_expr(expr.args[1], node, doc, children);
      if (projection_is_null(attr_value)) return make_null_projection();
      std::string attr = util::to_lower(projection_to_string(attr_value));
      auto it = target->attributes.find(attr);
      if (it == target->attributes.end()) return make_null_projection();
      return make_string_projection(it->second);
    }

    size_t depth = 1;
    if (expr.args.size() == 2) {
      ScalarProjectionValue depth_value =
          eval_select_scalar_expr(expr.args[1], node, doc, children);
      auto parsed = projection_to_int(depth_value);
      if (!parsed.has_value() || *parsed < 0) return make_null_projection();
      depth = static_cast<size_t>(*parsed);
    }
    std::string html = markql_internal::limit_inner_html(target->inner_html, depth);
    if (fn == "RAW_INNER_HTML") return make_string_projection(html);
    return make_string_projection(util::minify_html(html));
  }

  std::vector<ScalarProjectionValue> args;
  args.reserve(expr.args.size());
  for (const auto& arg : expr.args) {
    args.push_back(eval_select_scalar_expr(arg, node, doc, children));
  }

  if (fn == "COALESCE") {
    for (const auto& value : args) {
      if (!projection_is_null(value)) return value;
    }
    return make_null_projection();
  }
  if (fn == "CONCAT") {
    std::string out;
    for (const auto& value : args) {
      if (projection_is_null(value)) return make_null_projection();
      out += projection_to_string(value);
    }
    return make_string_projection(out);
  }
  if (fn == "LOWER" || fn == "UPPER") {
    if (args.size() != 1 || projection_is_null(args[0])) return make_null_projection();
    if (fn == "LOWER") return make_string_projection(util::to_lower(projection_to_string(args[0])));
    return make_string_projection(util::to_upper(projection_to_string(args[0])));
  }
  if (fn == "TRIM" || fn == "LTRIM" || fn == "RTRIM") {
    if (args.size() != 1 || projection_is_null(args[0])) return make_null_projection();
    std::string value = projection_to_string(args[0]);
    if (fn == "TRIM") return make_string_projection(util::trim_ws(value));
    if (fn == "LTRIM") {
      size_t i = 0;
      while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i]))) ++i;
      return make_string_projection(value.substr(i));
    }
    size_t end = value.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return make_string_projection(value.substr(0, end));
  }
  if (fn == "REPLACE") {
    if (args.size() != 3 || projection_is_null(args[0]) || projection_is_null(args[1]) ||
        projection_is_null(args[2])) {
      return make_null_projection();
    }
    std::string out = projection_to_string(args[0]);
    std::string from = projection_to_string(args[1]);
    std::string to = projection_to_string(args[2]);
    if (from.empty()) return make_string_projection(out);
    size_t pos = 0;
    while ((pos = out.find(from, pos)) != std::string::npos) {
      out.replace(pos, from.size(), to);
      pos += to.size();
    }
    return make_string_projection(out);
  }
  if (fn == "REGEX_REPLACE") {
    if (args.size() != 3 || projection_is_null(args[0]) || projection_is_null(args[1]) ||
        projection_is_null(args[2])) {
      return make_null_projection();
    }
    std::optional<std::string> out =
        util::regex_replace_all(projection_to_string(args[0]), projection_to_string(args[1]),
                                projection_to_string(args[2]));
    if (!out.has_value()) return make_null_projection();
    return make_string_projection(*out);
  }
  if (fn == "LENGTH" || fn == "CHAR_LENGTH") {
    if (args.size() != 1 || projection_is_null(args[0])) return make_null_projection();
    return make_number_projection(static_cast<int64_t>(projection_to_string(args[0]).size()));
  }
  if (fn == "SUBSTRING" || fn == "SUBSTR") {
    if (args.size() < 2 || args.size() > 3 || projection_is_null(args[0]) ||
        projection_is_null(args[1])) {
      return make_null_projection();
    }
    std::string text = projection_to_string(args[0]);
    auto start = projection_to_int(args[1]);
    if (!start.has_value()) return make_null_projection();
    int64_t from = std::max<int64_t>(1, *start) - 1;
    if (static_cast<size_t>(from) >= text.size()) return make_string_projection("");
    if (args.size() == 2 || projection_is_null(args[2])) {
      return make_string_projection(text.substr(static_cast<size_t>(from)));
    }
    auto len = projection_to_int(args[2]);
    if (!len.has_value() || *len <= 0) return make_string_projection("");
    return make_string_projection(
        text.substr(static_cast<size_t>(from), static_cast<size_t>(*len)));
  }
  if (fn == "POSITION" || fn == "LOCATE") {
    if (args.size() < 2 || projection_is_null(args[0]) || projection_is_null(args[1]))
      return make_null_projection();
    std::string needle = projection_to_string(args[0]);
    std::string haystack = projection_to_string(args[1]);
    size_t start = 0;
    if (fn == "LOCATE" && args.size() == 3 && !projection_is_null(args[2])) {
      auto parsed = projection_to_int(args[2]);
      if (!parsed.has_value()) return make_null_projection();
      if (*parsed > 1) start = static_cast<size_t>(*parsed - 1);
    }
    size_t pos = haystack.find(needle, start);
    if (pos == std::string::npos) return make_number_projection(0);
    return make_number_projection(static_cast<int64_t>(pos + 1));
  }
  return make_null_projection();
}

std::optional<std::string> eval_flatten_extract_expr(
    const Query::SelectItem::FlattenExtractExpr& expr, const HtmlNode& base_node,
    const HtmlDocument& doc, const std::vector<std::vector<int64_t>>& children,
    const std::unordered_map<std::string, std::string>& bindings, ProjectRowEvalCache* row_cache) {
  using ExtractKind = Query::SelectItem::FlattenExtractExpr::Kind;

  if (expr.kind == ExtractKind::StringLiteral) {
    return expr.string_value;
  }
  if (expr.kind == ExtractKind::NumberLiteral) {
    return std::to_string(expr.number_value);
  }
  if (expr.kind == ExtractKind::NullLiteral) {
    return std::nullopt;
  }
  if (expr.kind == ExtractKind::AliasRef) {
    auto it = bindings.find(expr.alias_ref);
    if (it == bindings.end()) return std::nullopt;
    return it->second;
  }
  if (expr.kind == ExtractKind::OperandRef) {
    return projection_operand_value(expr.operand, base_node, doc, children);
  }
  if (expr.kind == ExtractKind::CaseWhen) {
    for (size_t i = 0; i < expr.case_when_conditions.size() && i < expr.case_when_values.size();
         ++i) {
      if (!executor_internal::eval_expr(expr.case_when_conditions[i], doc, children, base_node))
        continue;
      return eval_flatten_extract_expr(expr.case_when_values[i], base_node, doc, children, bindings,
                                       row_cache);
    }
    if (expr.case_else != nullptr) {
      return eval_flatten_extract_expr(*expr.case_else, base_node, doc, children, bindings,
                                       row_cache);
    }
    return std::nullopt;
  }

  if (expr.kind == ExtractKind::Coalesce) {
    for (const auto& arg : expr.args) {
      std::optional<std::string> value =
          eval_flatten_extract_expr(arg, base_node, doc, children, bindings, row_cache);
      if (!value.has_value()) continue;
      if (util::trim_ws(*value).empty()) continue;
      return value;
    }
    return std::nullopt;
  }

  if (expr.kind == ExtractKind::Text) {
    return selector_value(expr.tag, std::nullopt, expr.where, expr.selector_index,
                          expr.selector_last, false, base_node, doc, children, row_cache);
  }
  if (expr.kind == ExtractKind::Attr) {
    return selector_value(expr.tag, expr.attribute, expr.where, expr.selector_index,
                          expr.selector_last, false, base_node, doc, children, row_cache);
  }

  if (expr.kind == ExtractKind::FunctionCall) {
    const std::string fn = util::to_upper(expr.function_name);
    std::vector<std::optional<std::string>> args;
    args.reserve(expr.args.size());
    for (const auto& arg : expr.args) {
      args.push_back(eval_flatten_extract_expr(arg, base_node, doc, children, bindings, row_cache));
    }
    if (fn == "TEXT") {
      if (args.size() != 1 || !args[0].has_value()) return std::nullopt;
      return selector_value(*args[0], std::nullopt, expr.where, expr.selector_index,
                            expr.selector_last, false, base_node, doc, children, row_cache);
    }
    if (fn == "DIRECT_TEXT") {
      if (args.size() != 1 || !args[0].has_value()) return std::nullopt;
      return selector_value(*args[0], std::nullopt, expr.where, expr.selector_index,
                            expr.selector_last, true, base_node, doc, children, row_cache);
    }
    if (fn == "ATTR") {
      if (args.size() != 2 || !args[0].has_value() || !args[1].has_value()) return std::nullopt;
      return selector_value(*args[0], util::to_lower(*args[1]), expr.where, expr.selector_index,
                            expr.selector_last, false, base_node, doc, children, row_cache);
    }
    if (fn == "CONCAT") {
      std::string out;
      for (const auto& arg : args) {
        if (!arg.has_value()) return std::nullopt;
        out += *arg;
      }
      return out;
    }
    if (fn == "LOWER") {
      if (args.size() != 1 || !args[0].has_value()) return std::nullopt;
      return util::to_lower(*args[0]);
    }
    if (fn == "UPPER") {
      if (args.size() != 1 || !args[0].has_value()) return std::nullopt;
      return util::to_upper(*args[0]);
    }
    if (fn == "TRIM") {
      if (args.size() != 1 || !args[0].has_value()) return std::nullopt;
      return util::trim_ws(*args[0]);
    }
    if (fn == "LTRIM") {
      if (args.size() != 1 || !args[0].has_value()) return std::nullopt;
      size_t i = 0;
      while (i < args[0]->size() && std::isspace(static_cast<unsigned char>((*args[0])[i]))) ++i;
      return args[0]->substr(i);
    }
    if (fn == "RTRIM") {
      if (args.size() != 1 || !args[0].has_value()) return std::nullopt;
      size_t end = args[0]->size();
      while (end > 0 && std::isspace(static_cast<unsigned char>((*args[0])[end - 1]))) --end;
      return args[0]->substr(0, end);
    }
    if (fn == "REPLACE") {
      if (args.size() != 3 || !args[0].has_value() || !args[1].has_value() || !args[2].has_value())
        return std::nullopt;
      std::string out = *args[0];
      if (args[1]->empty()) return out;
      size_t pos = 0;
      while ((pos = out.find(*args[1], pos)) != std::string::npos) {
        out.replace(pos, args[1]->size(), *args[2]);
        pos += args[2]->size();
      }
      return out;
    }
    if (fn == "REGEX_REPLACE") {
      if (args.size() != 3 || !args[0].has_value() || !args[1].has_value() ||
          !args[2].has_value()) {
        return std::nullopt;
      }
      return util::regex_replace_all(*args[0], *args[1], *args[2]);
    }
    if (fn == "LENGTH" || fn == "CHAR_LENGTH") {
      if (args.size() != 1 || !args[0].has_value()) return std::nullopt;
      return std::to_string(args[0]->size());
    }
    if (fn == "SUBSTRING" || fn == "SUBSTR") {
      if (args.size() < 2 || args.size() > 3 || !args[0].has_value() || !args[1].has_value())
        return std::nullopt;
      auto start = parse_int64_value(*args[1]);
      if (!start.has_value()) return std::nullopt;
      int64_t from = std::max<int64_t>(1, *start) - 1;
      if (static_cast<size_t>(from) >= args[0]->size()) return std::string{};
      if (args.size() == 2 || !args[2].has_value())
        return args[0]->substr(static_cast<size_t>(from));
      auto len = parse_int64_value(*args[2]);
      if (!len.has_value() || *len <= 0) return std::string{};
      return args[0]->substr(static_cast<size_t>(from), static_cast<size_t>(*len));
    }
    if (fn == "POSITION") {
      if (args.size() != 2 || !args[0].has_value() || !args[1].has_value()) return std::nullopt;
      size_t pos = args[1]->find(*args[0]);
      if (pos == std::string::npos) return std::string("0");
      return std::to_string(pos + 1);
    }
    if (fn == "LOCATE") {
      if (args.size() < 2 || args.size() > 3 || !args[0].has_value() || !args[1].has_value())
        return std::nullopt;
      size_t start = 0;
      if (args.size() == 3 && args[2].has_value()) {
        auto parsed = parse_int64_value(*args[2]);
        if (!parsed.has_value()) return std::nullopt;
        if (*parsed > 1) start = static_cast<size_t>(*parsed - 1);
      }
      size_t pos = args[1]->find(*args[0], start);
      if (pos == std::string::npos) return std::string("0");
      return std::to_string(pos + 1);
    }
    if (fn == "__CMP_EQ" || fn == "__CMP_NE" || fn == "__CMP_LT" || fn == "__CMP_LE" ||
        fn == "__CMP_GT" || fn == "__CMP_GE" || fn == "__CMP_LIKE") {
      if (args.size() != 2 || !args[0].has_value() || !args[1].has_value()) return std::nullopt;
      bool result = false;
      if (fn == "__CMP_LIKE") {
        result = like_match_ci(*args[0], *args[1]);
      } else {
        auto lnum = parse_int64_value(*args[0]);
        auto rnum = parse_int64_value(*args[1]);
        if (lnum.has_value() && rnum.has_value()) {
          if (fn == "__CMP_EQ")
            result = *lnum == *rnum;
          else if (fn == "__CMP_NE")
            result = *lnum != *rnum;
          else if (fn == "__CMP_LT")
            result = *lnum < *rnum;
          else if (fn == "__CMP_LE")
            result = *lnum <= *rnum;
          else if (fn == "__CMP_GT")
            result = *lnum > *rnum;
          else
            result = *lnum >= *rnum;
        } else {
          if (fn == "__CMP_EQ")
            result = *args[0] == *args[1];
          else if (fn == "__CMP_NE")
            result = *args[0] != *args[1];
          else if (fn == "__CMP_LT")
            result = *args[0] < *args[1];
          else if (fn == "__CMP_LE")
            result = *args[0] <= *args[1];
          else if (fn == "__CMP_GT")
            result = *args[0] > *args[1];
          else
            result = *args[0] >= *args[1];
        }
      }
      return result ? std::string("true") : std::string("false");
    }
    if (fn == "COALESCE") {
      for (const auto& value : args) {
        if (!value.has_value()) continue;
        if (util::trim_ws(*value).empty()) continue;
        return value;
      }
      return std::nullopt;
    }
  }

  return std::nullopt;
}

std::optional<std::string> eval_parse_source_expr(const ScalarExpr& expr) {
  // WHY: PARSE(<expr>) is source-level and has no row context, so evaluate
  // against an empty node and only allow expressions that reduce to scalars.
  const HtmlNode empty_node;
  ScalarProjectionValue value = eval_select_scalar_expr(expr, empty_node);
  if (projection_is_null(value)) return std::nullopt;
  return projection_to_string(value);
}

}  // namespace markql
