#include "xsql/xsql.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "../executor/executor_internal.h"
#include "../executor.h"
#include "../../dom/html_parser.h"
#include "../../lang/markql_parser.h"
#include "../../util/string_util.h"
#include "xsql_internal.h"

namespace xsql {

namespace {

struct FragmentSource {
  std::vector<std::string> fragments;
};

struct DescendantTagFilter {
  struct Predicate {
    Operand::FieldKind field_kind = Operand::FieldKind::Tag;
    std::string attribute;
    CompareExpr::Op op = CompareExpr::Op::Eq;
    std::vector<std::string> values;
  };
  std::vector<Predicate> predicates;
};

bool like_match_ci(const std::string& text, const std::string& pattern);

void collect_descendants_any_depth(const std::vector<std::vector<int64_t>>& children,
                                   int64_t node_id,
                                   std::vector<int64_t>& out);

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

void collect_row_scope_nodes(const std::vector<std::vector<int64_t>>& children,
                             int64_t node_id,
                             std::vector<int64_t>& out) {
  out.push_back(node_id);
  collect_descendants_any_depth(children, node_id, out);
}

std::string normalized_extract_text(const HtmlNode& node) {
  std::string direct = xsql_internal::extract_direct_text_strict(node.inner_html);
  std::string normalized = normalize_flatten_text(direct);
  if (!normalized.empty()) return normalized;
  direct = xsql_internal::extract_direct_text(node.inner_html);
  normalized = normalize_flatten_text(direct);
  if (!normalized.empty()) return normalized;
  return normalize_flatten_text(node.text);
}

std::optional<int64_t> parse_int64_value(const std::string& value) {
  try {
    size_t idx = 0;
    int64_t out = std::stoll(value, &idx);
    if (idx != value.size()) return std::nullopt;
    return out;
  } catch (...) {
    return std::nullopt;
  }
}

struct ScalarProjectionValue {
  enum class Kind { Null, String, Number } kind = Kind::Null;
  std::string string_value;
  int64_t number_value = 0;
};

ScalarProjectionValue make_null_projection() { return ScalarProjectionValue{}; }

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

std::string projection_to_string(const ScalarProjectionValue& value) {
  if (value.kind == ScalarProjectionValue::Kind::Number) {
    return std::to_string(value.number_value);
  }
  return value.string_value;
}

std::optional<int64_t> projection_to_int(const ScalarProjectionValue& value) {
  if (value.kind == ScalarProjectionValue::Kind::Number) return value.number_value;
  if (value.kind == ScalarProjectionValue::Kind::String) return parse_int64_value(value.string_value);
  return std::nullopt;
}

bool projection_is_null(const ScalarProjectionValue& value) {
  return value.kind == ScalarProjectionValue::Kind::Null;
}

std::optional<std::string> projection_operand_value(
    const Operand& operand,
    const HtmlNode& base_node,
    const HtmlDocument& doc,
    const std::vector<std::vector<int64_t>>& children);

ScalarProjectionValue eval_select_scalar_expr(
    const ScalarExpr& expr,
    const HtmlNode& node,
    const HtmlDocument* doc = nullptr,
    const std::vector<std::vector<int64_t>>* children = nullptr) {
  switch (expr.kind) {
    case ScalarExpr::Kind::NullLiteral:
      return make_null_projection();
    case ScalarExpr::Kind::StringLiteral:
      return make_string_projection(expr.string_value);
    case ScalarExpr::Kind::NumberLiteral:
      return make_number_projection(expr.number_value);
    case ScalarExpr::Kind::Operand: {
      const Operand& op = expr.operand;
      if (op.axis != Operand::Axis::Self ||
          op.field_kind == Operand::FieldKind::SiblingPos) {
        if (doc == nullptr || children == nullptr) return make_null_projection();
        std::optional<std::string> value =
            projection_operand_value(op, node, *doc, *children);
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
      if (op.field_kind == Operand::FieldKind::MaxDepth) return make_number_projection(node.max_depth);
      if (op.field_kind == Operand::FieldKind::DocOrder) return make_number_projection(node.doc_order);
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
  if (fn == "TEXT" || fn == "DIRECT_TEXT" || fn == "INNER_HTML" ||
      fn == "RAW_INNER_HTML" || fn == "ATTR") {
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
      return make_string_projection(xsql_internal::extract_direct_text_strict(target->inner_html));
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
      ScalarProjectionValue depth_value = eval_select_scalar_expr(expr.args[1], node, doc, children);
      auto parsed = projection_to_int(depth_value);
      if (!parsed.has_value() || *parsed < 0) return make_null_projection();
      depth = static_cast<size_t>(*parsed);
    }
    std::string html = xsql_internal::limit_inner_html(target->inner_html, depth);
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
    if (args.size() != 3 || projection_is_null(args[0]) || projection_is_null(args[1]) || projection_is_null(args[2])) {
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
  if (fn == "LENGTH" || fn == "CHAR_LENGTH") {
    if (args.size() != 1 || projection_is_null(args[0])) return make_null_projection();
    return make_number_projection(static_cast<int64_t>(projection_to_string(args[0]).size()));
  }
  if (fn == "SUBSTRING" || fn == "SUBSTR") {
    if (args.size() < 2 || args.size() > 3 || projection_is_null(args[0]) || projection_is_null(args[1])) {
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
    return make_string_projection(text.substr(static_cast<size_t>(from), static_cast<size_t>(*len)));
  }
  if (fn == "POSITION" || fn == "LOCATE") {
    if (args.size() < 2 || projection_is_null(args[0]) || projection_is_null(args[1])) return make_null_projection();
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

std::optional<std::string> eval_parse_source_expr(const ScalarExpr& expr) {
  // WHY: PARSE(<expr>) is source-level and has no row context, so evaluate
  // against an empty node and only allow expressions that reduce to scalars.
  const HtmlNode empty_node;
  ScalarProjectionValue value = eval_select_scalar_expr(expr, empty_node);
  if (projection_is_null(value)) return std::nullopt;
  return projection_to_string(value);
}

std::optional<std::string> selector_value(
    const std::string& tag,
    const std::optional<std::string>& attr,
    const std::optional<Expr>& where,
    const std::optional<int64_t>& selector_index,
    bool selector_last,
    bool direct_text,
    const HtmlNode& base_node,
    const HtmlDocument& doc,
    const std::vector<std::vector<int64_t>>& children) {
  std::vector<int64_t> scope_nodes;
  scope_nodes.reserve(32);
  collect_row_scope_nodes(children, base_node.id, scope_nodes);
  const std::string extract_tag = util::to_lower(tag);
  int64_t seen = 0;
  std::optional<std::string> last_value;
  int64_t target = selector_index.value_or(1);
  for (int64_t id : scope_nodes) {
    const HtmlNode& node = doc.nodes.at(static_cast<size_t>(id));
    if (node.tag != extract_tag) continue;
    if (where.has_value() && !executor_internal::eval_expr(*where, doc, children, node)) {
      continue;
    }
    std::optional<std::string> value;
    if (attr.has_value()) {
      auto it = node.attributes.find(*attr);
      if (it == node.attributes.end() || it->second.empty()) continue;
      value = it->second;
    } else if (direct_text) {
      std::string direct = util::trim_ws(xsql_internal::extract_direct_text_strict(node.inner_html));
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

std::vector<const HtmlNode*> projection_axis_nodes(const HtmlDocument& doc,
                                                   const std::vector<std::vector<int64_t>>& children,
                                                   const HtmlNode& node,
                                                   Operand::Axis axis) {
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
  stack.insert(stack.end(),
               children.at(static_cast<size_t>(node.id)).begin(),
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

std::optional<std::string> projection_operand_value(const Operand& operand,
                                                    const HtmlNode& base_node,
                                                    const HtmlDocument& doc,
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

std::optional<std::string> eval_flatten_extract_expr(
    const Query::SelectItem::FlattenExtractExpr& expr,
    const HtmlNode& base_node,
    const HtmlDocument& doc,
    const std::vector<std::vector<int64_t>>& children,
    const std::unordered_map<std::string, std::string>& bindings) {
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
    for (size_t i = 0; i < expr.case_when_conditions.size() && i < expr.case_when_values.size(); ++i) {
      if (!executor_internal::eval_expr(expr.case_when_conditions[i], doc, children, base_node)) continue;
      return eval_flatten_extract_expr(expr.case_when_values[i], base_node, doc, children, bindings);
    }
    if (expr.case_else != nullptr) {
      return eval_flatten_extract_expr(*expr.case_else, base_node, doc, children, bindings);
    }
    return std::nullopt;
  }

  if (expr.kind == ExtractKind::Coalesce) {
    for (const auto& arg : expr.args) {
      std::optional<std::string> value =
          eval_flatten_extract_expr(arg, base_node, doc, children, bindings);
      if (!value.has_value()) continue;
      if (util::trim_ws(*value).empty()) continue;
      return value;
    }
    return std::nullopt;
  }

  if (expr.kind == ExtractKind::Text) {
    return selector_value(expr.tag, std::nullopt, expr.where, expr.selector_index, expr.selector_last,
                          false, base_node, doc, children);
  }
  if (expr.kind == ExtractKind::Attr) {
    return selector_value(expr.tag, expr.attribute, expr.where, expr.selector_index, expr.selector_last,
                          false, base_node, doc, children);
  }

  if (expr.kind == ExtractKind::FunctionCall) {
    const std::string fn = util::to_upper(expr.function_name);
    std::vector<std::optional<std::string>> args;
    args.reserve(expr.args.size());
    for (const auto& arg : expr.args) {
      args.push_back(eval_flatten_extract_expr(arg, base_node, doc, children, bindings));
    }
    if (fn == "TEXT") {
      if (args.size() != 1 || !args[0].has_value()) return std::nullopt;
      return selector_value(*args[0], std::nullopt, expr.where, expr.selector_index, expr.selector_last,
                            false, base_node, doc, children);
    }
    if (fn == "DIRECT_TEXT") {
      if (args.size() != 1 || !args[0].has_value()) return std::nullopt;
      return selector_value(*args[0], std::nullopt, expr.where, expr.selector_index, expr.selector_last,
                            true, base_node, doc, children);
    }
    if (fn == "ATTR") {
      if (args.size() != 2 || !args[0].has_value() || !args[1].has_value()) return std::nullopt;
      return selector_value(*args[0], util::to_lower(*args[1]), expr.where, expr.selector_index, expr.selector_last,
                            false, base_node, doc, children);
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
      if (args.size() != 3 || !args[0].has_value() || !args[1].has_value() || !args[2].has_value()) return std::nullopt;
      std::string out = *args[0];
      if (args[1]->empty()) return out;
      size_t pos = 0;
      while ((pos = out.find(*args[1], pos)) != std::string::npos) {
        out.replace(pos, args[1]->size(), *args[2]);
        pos += args[2]->size();
      }
      return out;
    }
    if (fn == "LENGTH" || fn == "CHAR_LENGTH") {
      if (args.size() != 1 || !args[0].has_value()) return std::nullopt;
      return std::to_string(args[0]->size());
    }
    if (fn == "SUBSTRING" || fn == "SUBSTR") {
      if (args.size() < 2 || args.size() > 3 || !args[0].has_value() || !args[1].has_value()) return std::nullopt;
      auto start = parse_int64_value(*args[1]);
      if (!start.has_value()) return std::nullopt;
      int64_t from = std::max<int64_t>(1, *start) - 1;
      if (static_cast<size_t>(from) >= args[0]->size()) return std::string{};
      if (args.size() == 2 || !args[2].has_value()) return args[0]->substr(static_cast<size_t>(from));
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
      if (args.size() < 2 || args.size() > 3 || !args[0].has_value() || !args[1].has_value()) return std::nullopt;
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
          if (fn == "__CMP_EQ") result = *lnum == *rnum;
          else if (fn == "__CMP_NE") result = *lnum != *rnum;
          else if (fn == "__CMP_LT") result = *lnum < *rnum;
          else if (fn == "__CMP_LE") result = *lnum <= *rnum;
          else if (fn == "__CMP_GT") result = *lnum > *rnum;
          else result = *lnum >= *rnum;
        } else {
          if (fn == "__CMP_EQ") result = *args[0] == *args[1];
          else if (fn == "__CMP_NE") result = *args[0] != *args[1];
          else if (fn == "__CMP_LT") result = *args[0] < *args[1];
          else if (fn == "__CMP_LE") result = *args[0] <= *args[1];
          else if (fn == "__CMP_GT") result = *args[0] > *args[1];
          else result = *args[0] >= *args[1];
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

void collect_descendants_at_depth(const std::vector<std::vector<int64_t>>& children,
                                  int64_t node_id,
                                  size_t depth,
                                  std::vector<int64_t>& out) {
  if (depth == 0) {
    out.push_back(node_id);
    return;
  }
  for (int64_t child : children.at(static_cast<size_t>(node_id))) {
    collect_descendants_at_depth(children, child, depth - 1, out);
  }
}

void collect_descendants_any_depth(const std::vector<std::vector<int64_t>>& children,
                                   int64_t node_id,
                                   std::vector<int64_t>& out) {
  for (int64_t child : children.at(static_cast<size_t>(node_id))) {
    out.push_back(child);
    collect_descendants_any_depth(children, child, out);
  }
}

bool collect_descendant_tag_filter(const Expr& expr, DescendantTagFilter& filter) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    if (cmp.lhs.axis == Operand::Axis::Descendant &&
        (cmp.lhs.field_kind == Operand::FieldKind::Tag ||
         cmp.lhs.field_kind == Operand::FieldKind::Attribute)) {
      DescendantTagFilter::Predicate pred;
      pred.field_kind = cmp.lhs.field_kind;
      pred.attribute = cmp.lhs.attribute;
      pred.op = cmp.op;
      pred.values.reserve(cmp.rhs.values.size());
      if (cmp.lhs.field_kind == Operand::FieldKind::Tag) {
        for (const auto& value : cmp.rhs.values) {
          pred.values.push_back(util::to_lower(value));
        }
      } else {
        pred.values = cmp.rhs.values;
      }
      filter.predicates.push_back(std::move(pred));
      return true;
    }
    return false;
  }
  if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
    return false;
  }
  const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  bool left = collect_descendant_tag_filter(bin.left, filter);
  bool right = collect_descendant_tag_filter(bin.right, filter);
  return left || right;
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  std::string lower_haystack = util::to_lower(haystack);
  std::string lower_needle = util::to_lower(needle);
  return lower_haystack.find(lower_needle) != std::string::npos;
}

bool like_match_ci(const std::string& text, const std::string& pattern) {
  std::string s = util::to_lower(text);
  std::string p = util::to_lower(pattern);
  size_t si = 0;
  size_t pi = 0;
  size_t star = std::string::npos;
  size_t match = 0;
  while (si < s.size()) {
    if (pi < p.size() && (p[pi] == '_' || p[pi] == s[si])) {
      ++si;
      ++pi;
      continue;
    }
    if (pi < p.size() && p[pi] == '%') {
      star = pi++;
      match = si;
      continue;
    }
    if (star != std::string::npos) {
      pi = star + 1;
      si = ++match;
      continue;
    }
    return false;
  }
  while (pi < p.size() && p[pi] == '%') ++pi;
  return pi == p.size();
}

bool contains_all_ci(const std::string& haystack, const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (!contains_ci(haystack, token)) return false;
  }
  return true;
}

bool contains_any_ci(const std::string& haystack, const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (contains_ci(haystack, token)) return true;
  }
  return false;
}

std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
    }
    size_t start = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
    }
    if (start < i) out.push_back(s.substr(start, i - start));
  }
  return out;
}

QueryResult::TableOptions to_result_table_options(const Query::TableOptions& options) {
  QueryResult::TableOptions out;
  out.trim_empty_rows = options.trim_empty_rows;
  out.stop_after_empty_rows = options.stop_after_empty_rows;
  out.header_normalize = options.header_normalize;
  out.header_normalize_explicit = options.header_normalize_explicit;
  if (options.trim_empty_cols == Query::TableOptions::TrimEmptyCols::Off) {
    out.trim_empty_cols = QueryResult::TableOptions::TrimEmptyCols::Off;
  } else if (options.trim_empty_cols == Query::TableOptions::TrimEmptyCols::Trailing) {
    out.trim_empty_cols = QueryResult::TableOptions::TrimEmptyCols::Trailing;
  } else {
    out.trim_empty_cols = QueryResult::TableOptions::TrimEmptyCols::All;
  }
  if (options.empty_is == Query::TableOptions::EmptyIs::BlankOrNull) {
    out.empty_is = QueryResult::TableOptions::EmptyIs::BlankOrNull;
  } else if (options.empty_is == Query::TableOptions::EmptyIs::NullOnly) {
    out.empty_is = QueryResult::TableOptions::EmptyIs::NullOnly;
  } else {
    out.empty_is = QueryResult::TableOptions::EmptyIs::BlankOnly;
  }
  if (options.format == Query::TableOptions::Format::Rect) {
    out.format = QueryResult::TableOptions::Format::Rect;
  } else {
    out.format = QueryResult::TableOptions::Format::Sparse;
  }
  if (options.sparse_shape == Query::TableOptions::SparseShape::Long) {
    out.sparse_shape = QueryResult::TableOptions::SparseShape::Long;
  } else {
    out.sparse_shape = QueryResult::TableOptions::SparseShape::Wide;
  }
  return out;
}

bool is_space_or_nbsp(const std::string& text, size_t index, size_t& consumed) {
  const unsigned char c = static_cast<unsigned char>(text[index]);
  if (std::isspace(c)) {
    consumed = 1;
    return true;
  }
  if (c == 0xC2 && index + 1 < text.size() &&
      static_cast<unsigned char>(text[index + 1]) == 0xA0) {
    consumed = 2;
    return true;
  }
  return false;
}

std::string normalize_table_whitespace(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  bool have_non_space = false;
  bool pending_space = false;
  for (size_t i = 0; i < value.size();) {
    size_t consumed = 0;
    if (is_space_or_nbsp(value, i, consumed)) {
      if (have_non_space) {
        pending_space = true;
      }
      i += consumed;
      continue;
    }
    if (pending_space) {
      out.push_back(' ');
      pending_space = false;
    }
    out.push_back(value[i]);
    have_non_space = true;
    ++i;
  }
  return out;
}

std::string normalize_header_text(const std::string& value) {
  const std::string normalized = normalize_table_whitespace(value);
  if (normalized.empty()) return "";
  std::vector<std::string> deduped_tokens;
  size_t start = 0;
  while (start < normalized.size()) {
    size_t end = normalized.find(' ', start);
    if (end == std::string::npos) end = normalized.size();
    std::string token = normalized.substr(start, end - start);
    if (!token.empty() &&
        (deduped_tokens.empty() || deduped_tokens.back() != token)) {
      deduped_tokens.push_back(std::move(token));
    }
    start = end + 1;
  }
  std::string out;
  for (size_t i = 0; i < deduped_tokens.size(); ++i) {
    if (i > 0) out.push_back(' ');
    out += deduped_tokens[i];
  }
  return out;
}

bool table_cell_empty(const std::vector<std::string>& row,
                      size_t col_index,
                      Query::TableOptions::EmptyIs empty_is) {
  if (col_index >= row.size()) {
    return empty_is == Query::TableOptions::EmptyIs::BlankOrNull ||
           empty_is == Query::TableOptions::EmptyIs::NullOnly;
  }
  if (empty_is == Query::TableOptions::EmptyIs::NullOnly) {
    return false;
  }
  return normalize_table_whitespace(row[col_index]).empty();
}

bool table_row_all_empty(const std::vector<std::string>& row,
                         size_t max_cols,
                         Query::TableOptions::EmptyIs empty_is) {
  if (max_cols == 0) return true;
  for (size_t col = 0; col < max_cols; ++col) {
    if (!table_cell_empty(row, col, empty_is)) return false;
  }
  return true;
}

std::vector<size_t> select_table_columns(const std::vector<std::vector<std::string>>& rows,
                                         size_t max_cols,
                                         const Query::TableOptions& options) {
  std::vector<size_t> selected;
  if (max_cols == 0) return selected;
  if (options.trim_empty_cols == Query::TableOptions::TrimEmptyCols::Off) {
    selected.reserve(max_cols);
    for (size_t col = 0; col < max_cols; ++col) selected.push_back(col);
    return selected;
  }
  std::vector<bool> empty_cols(max_cols, true);
  for (size_t col = 0; col < max_cols; ++col) {
    for (const auto& row : rows) {
      if (!table_cell_empty(row, col, options.empty_is)) {
        empty_cols[col] = false;
        break;
      }
    }
  }
  if (options.trim_empty_cols == Query::TableOptions::TrimEmptyCols::Trailing) {
    size_t keep_until = max_cols;
    while (keep_until > 0 && empty_cols[keep_until - 1]) {
      --keep_until;
    }
    selected.reserve(keep_until);
    for (size_t col = 0; col < keep_until; ++col) selected.push_back(col);
    return selected;
  }
  selected.reserve(max_cols);
  for (size_t col = 0; col < max_cols; ++col) {
    if (!empty_cols[col]) selected.push_back(col);
  }
  return selected;
}

std::vector<std::string> unique_header_keys(const std::vector<std::string>& headers) {
  std::vector<std::string> keys;
  keys.reserve(headers.size());
  std::unordered_map<std::string, size_t> seen;
  for (const auto& header : headers) {
    auto count_it = seen.find(header);
    if (count_it == seen.end()) {
      seen[header] = 1;
      keys.push_back(header);
      continue;
    }
    const size_t next = ++count_it->second;
    keys.push_back(header + "_" + std::to_string(next));
  }
  return keys;
}

struct MaterializedTable {
  std::vector<std::string> headers;
  std::vector<std::string> header_keys;
  std::vector<std::vector<std::string>> rect_rows;
  std::vector<std::vector<std::string>> sparse_long_rows;
  std::vector<std::vector<std::pair<std::string, std::string>>> sparse_wide_rows;
};

bool table_uses_default_output(const Query& query) {
  return query.table_options.format == Query::TableOptions::Format::Rect &&
         !query.table_options.trim_empty_rows &&
         query.table_options.trim_empty_cols == Query::TableOptions::TrimEmptyCols::Off &&
         query.table_options.empty_is == Query::TableOptions::EmptyIs::BlankOrNull &&
         query.table_options.stop_after_empty_rows == 0 &&
         !query.table_options.header_normalize_explicit;
}

MaterializedTable materialize_table(const std::vector<std::vector<std::string>>& raw_rows,
                                    bool has_header,
                                    const Query::TableOptions& options) {
  MaterializedTable out;
  if (raw_rows.empty()) return out;

  size_t max_cols = 0;
  for (const auto& row : raw_rows) {
    if (row.size() > max_cols) max_cols = row.size();
  }

  std::vector<std::vector<std::string>> kept_rows;
  kept_rows.reserve(raw_rows.size());
  size_t consecutive_empty_rows = 0;
  for (const auto& row : raw_rows) {
    const bool all_empty = table_row_all_empty(row, max_cols, options.empty_is);
    if (all_empty) {
      ++consecutive_empty_rows;
    } else {
      consecutive_empty_rows = 0;
    }
    if (!(options.trim_empty_rows && all_empty)) {
      kept_rows.push_back(row);
    }
    if (options.stop_after_empty_rows > 0 &&
        consecutive_empty_rows >= options.stop_after_empty_rows) {
      break;
    }
  }

  max_cols = 0;
  for (const auto& row : kept_rows) {
    if (row.size() > max_cols) max_cols = row.size();
  }
  std::vector<size_t> keep_cols = select_table_columns(kept_rows, max_cols, options);
  const size_t out_cols = keep_cols.size();

  out.rect_rows.reserve(kept_rows.size());
  for (const auto& row : kept_rows) {
    std::vector<std::string> projected;
    projected.reserve(out_cols);
    for (size_t keep_index : keep_cols) {
      if (keep_index < row.size()) {
        projected.push_back(row[keep_index]);
      } else {
        projected.emplace_back();
      }
    }
    out.rect_rows.push_back(std::move(projected));
  }

  if (out_cols == 0) {
    return out;
  }

  const bool apply_header_normalize =
      has_header && options.header_normalize && options.header_normalize_explicit;

  out.headers.resize(out_cols);
  for (size_t col = 0; col < out_cols; ++col) {
    std::string header;
    if (has_header && !out.rect_rows.empty() && col < out.rect_rows[0].size()) {
      header = out.rect_rows[0][col];
    }
    if (apply_header_normalize) {
      header = normalize_header_text(header);
    }
    if (header.empty()) {
      header = "col_" + std::to_string(col + 1);
    }
    out.headers[col] = std::move(header);
  }
  out.header_keys = unique_header_keys(out.headers);

  if (has_header && !out.rect_rows.empty() && apply_header_normalize) {
    out.rect_rows[0] = out.headers;
  }

  if (options.format != Query::TableOptions::Format::Sparse) {
    return out;
  }

  const size_t data_start = (has_header && !out.rect_rows.empty()) ? 1 : 0;
  for (size_t row_idx = data_start; row_idx < kept_rows.size(); ++row_idx) {
    const auto& original = kept_rows[row_idx];
    if (options.sparse_shape == Query::TableOptions::SparseShape::Long) {
      for (size_t col_pos = 0; col_pos < keep_cols.size(); ++col_pos) {
        const size_t source_col = keep_cols[col_pos];
        if (table_cell_empty(original, source_col, options.empty_is)) continue;
        std::vector<std::string> out_row;
        out_row.reserve(has_header ? 4 : 3);
        out_row.push_back(std::to_string((row_idx - data_start) + 1));
        out_row.push_back(std::to_string(col_pos + 1));
        if (has_header) {
          out_row.push_back(out.headers[col_pos]);
        }
        if (source_col < original.size()) {
          out_row.push_back(original[source_col]);
        } else {
          out_row.emplace_back();
        }
        out.sparse_long_rows.push_back(std::move(out_row));
      }
      continue;
    }
    std::vector<std::pair<std::string, std::string>> sparse_row;
    sparse_row.reserve(keep_cols.size());
    for (size_t col_pos = 0; col_pos < keep_cols.size(); ++col_pos) {
      const size_t source_col = keep_cols[col_pos];
      if (table_cell_empty(original, source_col, options.empty_is)) continue;
      std::string key =
          has_header ? out.header_keys[col_pos] : ("col_" + std::to_string(col_pos + 1));
      std::string value = (source_col < original.size()) ? original[source_col] : std::string{};
      sparse_row.emplace_back(std::move(key), std::move(value));
    }
    out.sparse_wide_rows.push_back(std::move(sparse_row));
  }
  return out;
}

bool match_descendant_predicate(const HtmlNode& node, const DescendantTagFilter::Predicate& pred) {
  if (pred.field_kind == Operand::FieldKind::Tag) {
    if (pred.op == CompareExpr::Op::In) {
      return executor_internal::string_in_list(node.tag, pred.values);
    }
    if (pred.op == CompareExpr::Op::Eq) {
      return node.tag == pred.values.front();
    }
    return false;
  }
  const auto it = node.attributes.find(pred.attribute);
  if (it == node.attributes.end()) return false;
  const std::string& attr_value = it->second;
  if (pred.op == CompareExpr::Op::Contains) {
    return contains_ci(attr_value, pred.values.front());
  }
  if (pred.op == CompareExpr::Op::ContainsAll) {
    return contains_all_ci(attr_value, pred.values);
  }
  if (pred.op == CompareExpr::Op::ContainsAny) {
    return contains_any_ci(attr_value, pred.values);
  }
  if (pred.op == CompareExpr::Op::In || pred.op == CompareExpr::Op::Eq) {
    if (pred.attribute == "class") {
      auto tokens = split_ws(attr_value);
      if (pred.op == CompareExpr::Op::Eq) {
        return executor_internal::string_in_list(pred.values.front(), tokens);
      }
      for (const auto& token : tokens) {
        if (executor_internal::string_in_list(token, pred.values)) return true;
      }
      return false;
    }
    if (pred.op == CompareExpr::Op::Eq) {
      return attr_value == pred.values.front();
    }
    return executor_internal::string_in_list(attr_value, pred.values);
  }
  return false;
}

bool looks_like_html_fragment(const std::string& value) {
  return value.find('<') != std::string::npos && value.find('>') != std::string::npos;
}

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

struct RelationRecord {
  std::unordered_map<std::string, std::optional<std::string>> values;
  std::unordered_map<std::string, std::string> attributes;
};

struct RelationRow {
  std::unordered_map<std::string, RelationRecord> aliases;
};

struct Relation {
  std::vector<RelationRow> rows;
  std::unordered_map<std::string, std::unordered_set<std::string>> alias_columns;
  std::vector<std::string> warnings;
};

struct SourceRowPrefilter {
  std::optional<int64_t> parent_id_eq;
  std::optional<std::string> tag_eq;
  bool impossible = false;
};

bool query_uses_relation_runtime(const Query& query,
                                 const std::unordered_map<std::string, Relation>* ctes,
                                 const RelationRow* outer_row) {
  if (outer_row != nullptr) return true;
  if (ctes != nullptr && !ctes->empty()) return true;
  if (query.with.has_value() && !query.with->ctes.empty()) return true;
  if (!query.joins.empty()) return true;
  if (query.source.kind == Source::Kind::CteRef ||
      query.source.kind == Source::Kind::DerivedSubquery) {
    return true;
  }
  for (const auto& order : query.order_by) {
    if (order.field.find('.') != std::string::npos) return true;
  }
  return false;
}

bool is_plain_count_star_document_query(const Query& query) {
  if (query.kind != Query::Kind::Select) return false;
  if (query.source.kind != Source::Kind::Document) return false;
  if (query.with.has_value()) return false;
  if (!query.joins.empty()) return false;
  if (query.where.has_value()) return false;
  if (!query.order_by.empty()) return false;
  if (!query.exclude_fields.empty()) return false;
  if (query.limit.has_value()) return false;
  if (query.to_list || query.to_table) return false;
  if (query.export_sink.has_value()) return false;
  if (query.select_items.size() != 1) return false;
  const auto& item = query.select_items.front();
  return item.aggregate == Query::SelectItem::Aggregate::Count && item.tag == "*";
}

QueryResult build_count_star_result(const Query& query,
                                    int64_t count,
                                    const std::string& source_uri) {
  QueryResult out;
  out.columns = xsql_internal::build_columns(query);
  out.columns_implicit = !xsql_internal::is_projection_query(query);
  out.to_list = query.to_list;
  out.to_table = query.to_table;
  out.table_has_header = query.table_has_header;
  out.table_options = to_result_table_options(query.table_options);
  QueryResultRow row;
  row.node_id = count;
  row.source_uri = source_uri;
  out.rows.push_back(std::move(row));
  return out;
}

std::string lower_alias_name(const std::string& alias) {
  return util::to_lower(alias);
}

void merge_alias_columns(Relation& rel,
                         const std::string& alias,
                         const RelationRecord& record) {
  auto& cols = rel.alias_columns[alias];
  for (const auto& kv : record.values) {
    cols.insert(kv.first);
  }
}

std::optional<int64_t> parse_optional_i64(const std::optional<std::string>& value) {
  if (!value.has_value()) return std::nullopt;
  return parse_int64_value(*value);
}

void fill_result_core_from_record(QueryResultRow& out, const RelationRecord& record) {
  auto get = [&](const std::string& key) -> std::optional<std::string> {
    auto it = record.values.find(key);
    if (it == record.values.end()) return std::nullopt;
    return it->second;
  };
  if (auto v = get("node_id"); v.has_value()) {
    if (auto parsed = parse_int64_value(*v); parsed.has_value()) out.node_id = *parsed;
  }
  if (auto v = get("tag"); v.has_value()) out.tag = *v;
  if (auto v = get("text"); v.has_value()) out.text = *v;
  if (auto v = get("inner_html"); v.has_value()) out.inner_html = *v;
  if (auto v = get("parent_id"); v.has_value()) {
    if (auto parsed = parse_int64_value(*v); parsed.has_value()) out.parent_id = *parsed;
  }
  if (auto v = get("sibling_pos"); v.has_value()) {
    if (auto parsed = parse_int64_value(*v); parsed.has_value()) out.sibling_pos = *parsed;
  }
  if (auto v = get("max_depth"); v.has_value()) {
    if (auto parsed = parse_int64_value(*v); parsed.has_value()) out.max_depth = *parsed;
  }
  if (auto v = get("doc_order"); v.has_value()) {
    if (auto parsed = parse_int64_value(*v); parsed.has_value()) out.doc_order = *parsed;
  }
  if (auto v = get("source_uri"); v.has_value()) out.source_uri = *v;
  out.attributes = record.attributes;
}

const RelationRecord* resolve_record(const RelationRow& row,
                                     const std::optional<std::string>& qualifier,
                                     const std::optional<std::string>& active_alias) {
  if (qualifier.has_value()) {
    const std::string lowered = lower_alias_name(*qualifier);
    auto it = row.aliases.find(lowered);
    if (it != row.aliases.end()) return &it->second;
    if (lowered == "doc" && row.aliases.size() == 1) {
      const std::string suggestion = row.aliases.begin()->first;
      if (suggestion != "doc") {
        throw std::runtime_error(
            "Identifier 'doc' is not bound; did you mean '" + suggestion + "'?");
      }
    }
    throw std::runtime_error("Unknown identifier '" + *qualifier +
                             "' (expected a FROM alias or legacy tag binding)");
  }
  if (active_alias.has_value()) {
    auto it = row.aliases.find(*active_alias);
    if (it != row.aliases.end()) return &it->second;
  }
  if (row.aliases.size() == 1) {
    return &row.aliases.begin()->second;
  }
  return nullptr;
}

std::optional<std::string> relation_operand_value(const Operand& operand,
                                                  const RelationRow& row,
                                                  const std::optional<std::string>& active_alias) {
  const RelationRecord* record = resolve_record(row, operand.qualifier, active_alias);
  if (record == nullptr) return std::nullopt;
  auto get_field = [&](const std::string& key) -> std::optional<std::string> {
    auto it = record->values.find(key);
    if (it == record->values.end()) return std::nullopt;
    return it->second;
  };
  auto prefixed_key = [&](const std::string& key) {
    if (operand.axis == Operand::Axis::Parent) {
      return std::string("parent.") + key;
    }
    return key;
  };
  if (operand.axis != Operand::Axis::Self &&
      operand.axis != Operand::Axis::Parent) {
    return std::nullopt;
  }
  switch (operand.field_kind) {
    case Operand::FieldKind::Attribute: {
      auto it = record->values.find(prefixed_key(operand.attribute));
      if (it != record->values.end()) return it->second;
      if (operand.axis == Operand::Axis::Self) {
        auto attr = record->attributes.find(operand.attribute);
        if (attr != record->attributes.end()) return attr->second;
      }
      return std::nullopt;
    }
    case Operand::FieldKind::Tag:
      return get_field(prefixed_key("tag"));
    case Operand::FieldKind::Text:
      return get_field(prefixed_key("text"));
    case Operand::FieldKind::NodeId:
      return get_field(prefixed_key("node_id"));
    case Operand::FieldKind::ParentId:
      return get_field(prefixed_key("parent_id"));
    case Operand::FieldKind::SiblingPos:
      return get_field(prefixed_key("sibling_pos"));
    case Operand::FieldKind::MaxDepth:
      return get_field(prefixed_key("max_depth"));
    case Operand::FieldKind::DocOrder:
      return get_field(prefixed_key("doc_order"));
    case Operand::FieldKind::AttributesMap:
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<std::string> eval_relation_scalar_expr(const ScalarExpr& expr,
                                                     const RelationRow& row,
                                                     const std::optional<std::string>& active_alias) {
  if (expr.kind == ScalarExpr::Kind::NullLiteral) return std::nullopt;
  if (expr.kind == ScalarExpr::Kind::StringLiteral) return expr.string_value;
  if (expr.kind == ScalarExpr::Kind::NumberLiteral) return std::to_string(expr.number_value);
  if (expr.kind == ScalarExpr::Kind::Operand) {
    return relation_operand_value(expr.operand, row, active_alias);
  }
  if (expr.kind == ScalarExpr::Kind::SelfRef) {
    return std::nullopt;
  }
  const std::string fn = util::to_upper(expr.function_name);
  if ((fn == "TEXT" || fn == "DIRECT_TEXT" || fn == "INNER_HTML" || fn == "RAW_INNER_HTML") &&
      !expr.args.empty()) {
    std::optional<std::string> target = eval_relation_scalar_expr(expr.args[0], row, active_alias);
    if (!target.has_value()) return std::nullopt;
    const std::string lowered_target = util::to_lower(*target);
    auto alias_it = row.aliases.find(lowered_target);
    if (alias_it != row.aliases.end()) {
      const std::string key = (fn == "INNER_HTML" || fn == "RAW_INNER_HTML") ? "inner_html" : "text";
      auto it = alias_it->second.values.find(key);
      if (it == alias_it->second.values.end()) return std::nullopt;
      return it->second;
    }
    const RelationRecord* active = resolve_record(row, std::nullopt, active_alias);
    if (active == nullptr) return std::nullopt;
    auto tag_it = active->values.find("tag");
    if (tag_it == active->values.end() || !tag_it->second.has_value()) return std::nullopt;
    if (util::to_lower(*tag_it->second) != lowered_target) return std::nullopt;
    const std::string key = (fn == "INNER_HTML" || fn == "RAW_INNER_HTML") ? "inner_html" : "text";
    auto value_it = active->values.find(key);
    if (value_it == active->values.end()) return std::nullopt;
    return value_it->second;
  }
  if (fn == "ATTR" && expr.args.size() == 2) {
    std::optional<std::string> target = eval_relation_scalar_expr(expr.args[0], row, active_alias);
    std::optional<std::string> attr = eval_relation_scalar_expr(expr.args[1], row, active_alias);
    if (!target.has_value() || !attr.has_value()) return std::nullopt;
    const std::string lowered_target = util::to_lower(*target);
    auto alias_it = row.aliases.find(lowered_target);
    if (alias_it != row.aliases.end()) {
      auto value_it = alias_it->second.values.find(util::to_lower(*attr));
      if (value_it != alias_it->second.values.end()) return value_it->second;
      auto attr_it = alias_it->second.attributes.find(util::to_lower(*attr));
      if (attr_it != alias_it->second.attributes.end()) return attr_it->second;
      return std::nullopt;
    }
  }
  if (fn == "COALESCE") {
    for (const auto& arg : expr.args) {
      std::optional<std::string> value = eval_relation_scalar_expr(arg, row, active_alias);
      if (!value.has_value()) continue;
      if (util::trim_ws(*value).empty()) continue;
      return value;
    }
    return std::nullopt;
  }
  if (fn == "LOWER" || fn == "UPPER" || fn == "TRIM" || fn == "LTRIM" || fn == "RTRIM") {
    if (expr.args.size() != 1) return std::nullopt;
    std::optional<std::string> value = eval_relation_scalar_expr(expr.args[0], row, active_alias);
    if (!value.has_value()) return std::nullopt;
    if (fn == "LOWER") return util::to_lower(*value);
    if (fn == "UPPER") return util::to_upper(*value);
    if (fn == "TRIM") return util::trim_ws(*value);
    if (fn == "LTRIM") {
      size_t i = 0;
      while (i < value->size() && std::isspace(static_cast<unsigned char>((*value)[i]))) ++i;
      return value->substr(i);
    }
    size_t end = value->size();
    while (end > 0 && std::isspace(static_cast<unsigned char>((*value)[end - 1]))) --end;
    return value->substr(0, end);
  }
  if (fn == "REPLACE") {
    if (expr.args.size() != 3) return std::nullopt;
    std::optional<std::string> text = eval_relation_scalar_expr(expr.args[0], row, active_alias);
    std::optional<std::string> from = eval_relation_scalar_expr(expr.args[1], row, active_alias);
    std::optional<std::string> to = eval_relation_scalar_expr(expr.args[2], row, active_alias);
    if (!text.has_value() || !from.has_value() || !to.has_value()) return std::nullopt;
    std::string out = *text;
    if (from->empty()) return out;
    size_t pos = 0;
    while ((pos = out.find(*from, pos)) != std::string::npos) {
      out.replace(pos, from->size(), *to);
      pos += to->size();
    }
    return out;
  }
  return std::nullopt;
}

bool eval_relation_expr(const Expr& expr,
                        const RelationRow& row,
                        const std::optional<std::string>& active_alias);

std::optional<std::string> eval_relation_project_expr(
    const Query::SelectItem::FlattenExtractExpr& expr,
    const RelationRow& row,
    const std::optional<std::string>& active_alias,
    const std::unordered_map<std::string, std::string>& bindings) {
  using Kind = Query::SelectItem::FlattenExtractExpr::Kind;
  if (expr.kind == Kind::StringLiteral) return expr.string_value;
  if (expr.kind == Kind::NumberLiteral) return std::to_string(expr.number_value);
  if (expr.kind == Kind::NullLiteral) return std::nullopt;
  if (expr.kind == Kind::AliasRef) {
    auto it = bindings.find(expr.alias_ref);
    if (it == bindings.end()) return std::nullopt;
    return it->second;
  }
  if (expr.kind == Kind::OperandRef) {
    return relation_operand_value(expr.operand, row, active_alias);
  }
  if (expr.kind == Kind::Coalesce) {
    for (const auto& arg : expr.args) {
      std::optional<std::string> value =
          eval_relation_project_expr(arg, row, active_alias, bindings);
      if (!value.has_value()) continue;
      if (util::trim_ws(*value).empty()) continue;
      return value;
    }
    return std::nullopt;
  }
  if (expr.kind == Kind::FunctionCall) {
    ScalarExpr scalar_expr;
    scalar_expr.kind = ScalarExpr::Kind::FunctionCall;
    scalar_expr.function_name = expr.function_name;
    for (const auto& arg : expr.args) {
      if (arg.kind == Kind::StringLiteral) {
        ScalarExpr scalar_arg;
        scalar_arg.kind = ScalarExpr::Kind::StringLiteral;
        scalar_arg.string_value = arg.string_value;
        scalar_expr.args.push_back(std::move(scalar_arg));
        continue;
      }
      if (arg.kind == Kind::NumberLiteral) {
        ScalarExpr scalar_arg;
        scalar_arg.kind = ScalarExpr::Kind::NumberLiteral;
        scalar_arg.number_value = arg.number_value;
        scalar_expr.args.push_back(std::move(scalar_arg));
        continue;
      }
      if (arg.kind == Kind::NullLiteral) {
        ScalarExpr scalar_arg;
        scalar_arg.kind = ScalarExpr::Kind::NullLiteral;
        scalar_expr.args.push_back(std::move(scalar_arg));
        continue;
      }
      if (arg.kind == Kind::OperandRef) {
        ScalarExpr scalar_arg;
        scalar_arg.kind = ScalarExpr::Kind::Operand;
        scalar_arg.operand = arg.operand;
        scalar_expr.args.push_back(std::move(scalar_arg));
        continue;
      }
      if (arg.kind == Kind::AliasRef) {
        auto it = bindings.find(arg.alias_ref);
        ScalarExpr scalar_arg;
        if (it == bindings.end()) {
          scalar_arg.kind = ScalarExpr::Kind::NullLiteral;
        } else {
          scalar_arg.kind = ScalarExpr::Kind::StringLiteral;
          scalar_arg.string_value = it->second;
        }
        scalar_expr.args.push_back(std::move(scalar_arg));
        continue;
      }
      std::optional<std::string> nested =
          eval_relation_project_expr(arg, row, active_alias, bindings);
      ScalarExpr scalar_arg;
      if (!nested.has_value()) {
        scalar_arg.kind = ScalarExpr::Kind::NullLiteral;
      } else {
        scalar_arg.kind = ScalarExpr::Kind::StringLiteral;
        scalar_arg.string_value = *nested;
      }
      scalar_expr.args.push_back(std::move(scalar_arg));
    }
    return eval_relation_scalar_expr(scalar_expr, row, active_alias);
  }
  if (expr.kind == Kind::CaseWhen) {
    for (size_t i = 0; i < expr.case_when_conditions.size() &&
                       i < expr.case_when_values.size(); ++i) {
      if (!eval_relation_expr(expr.case_when_conditions[i], row, active_alias)) continue;
      return eval_relation_project_expr(expr.case_when_values[i], row, active_alias, bindings);
    }
    if (expr.case_else != nullptr) {
      return eval_relation_project_expr(*expr.case_else, row, active_alias, bindings);
    }
    return std::nullopt;
  }
  return std::nullopt;
}

bool eval_relation_expr(const Expr& expr,
                        const RelationRow& row,
                        const std::optional<std::string>& active_alias) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    std::optional<std::string> lhs;
    if (cmp.lhs_expr.has_value()) {
      lhs = eval_relation_scalar_expr(*cmp.lhs_expr, row, active_alias);
    } else {
      lhs = relation_operand_value(cmp.lhs, row, active_alias);
    }
    if (cmp.op == CompareExpr::Op::IsNull) {
      return !lhs.has_value();
    }
    if (cmp.op == CompareExpr::Op::IsNotNull) {
      return lhs.has_value();
    }
    if (cmp.op == CompareExpr::Op::In) {
      if (!lhs.has_value()) return false;
      std::vector<std::string> candidates;
      if (!cmp.rhs_expr_list.empty()) {
        for (const auto& rhs_expr : cmp.rhs_expr_list) {
          std::optional<std::string> rhs = eval_relation_scalar_expr(rhs_expr, row, active_alias);
          if (rhs.has_value()) candidates.push_back(*rhs);
        }
      } else {
        candidates = cmp.rhs.values;
      }
      return executor_internal::string_in_list(*lhs, candidates);
    }
    if (cmp.op == CompareExpr::Op::Contains ||
        cmp.op == CompareExpr::Op::ContainsAll ||
        cmp.op == CompareExpr::Op::ContainsAny) {
      if (!lhs.has_value()) return false;
      if (cmp.op == CompareExpr::Op::Contains) {
        if (cmp.rhs.values.empty()) return false;
        return contains_ci(*lhs, cmp.rhs.values.front());
      }
      if (cmp.op == CompareExpr::Op::ContainsAll) {
        return contains_all_ci(*lhs, cmp.rhs.values);
      }
      return contains_any_ci(*lhs, cmp.rhs.values);
    }
    std::optional<std::string> rhs;
    if (cmp.rhs_expr.has_value()) {
      rhs = eval_relation_scalar_expr(*cmp.rhs_expr, row, active_alias);
    } else if (!cmp.rhs.values.empty()) {
      rhs = cmp.rhs.values.front();
    }
    if (!lhs.has_value() || !rhs.has_value()) return false;
    if (cmp.op == CompareExpr::Op::Like) {
      return like_match_ci(*lhs, *rhs);
    }
    auto lhs_num = parse_int64_value(*lhs);
    auto rhs_num = parse_int64_value(*rhs);
    if (lhs_num.has_value() && rhs_num.has_value()) {
      if (cmp.op == CompareExpr::Op::Eq) return *lhs_num == *rhs_num;
      if (cmp.op == CompareExpr::Op::NotEq) return *lhs_num != *rhs_num;
      if (cmp.op == CompareExpr::Op::Lt) return *lhs_num < *rhs_num;
      if (cmp.op == CompareExpr::Op::Lte) return *lhs_num <= *rhs_num;
      if (cmp.op == CompareExpr::Op::Gt) return *lhs_num > *rhs_num;
      if (cmp.op == CompareExpr::Op::Gte) return *lhs_num >= *rhs_num;
    }
    if (cmp.op == CompareExpr::Op::Eq) return *lhs == *rhs;
    if (cmp.op == CompareExpr::Op::NotEq) return *lhs != *rhs;
    if (cmp.op == CompareExpr::Op::Lt) return *lhs < *rhs;
    if (cmp.op == CompareExpr::Op::Lte) return *lhs <= *rhs;
    if (cmp.op == CompareExpr::Op::Gt) return *lhs > *rhs;
    if (cmp.op == CompareExpr::Op::Gte) return *lhs >= *rhs;
    return false;
  }
  if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
    return false;
  }
  const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  bool left = eval_relation_expr(bin.left, row, active_alias);
  bool right = eval_relation_expr(bin.right, row, active_alias);
  return (bin.op == BinaryExpr::Op::And) ? (left && right) : (left || right);
}

int compare_optional_relation_values(const std::optional<std::string>& left,
                                     const std::optional<std::string>& right) {
  if (!left.has_value() && !right.has_value()) return 0;
  if (!left.has_value()) return -1;
  if (!right.has_value()) return 1;
  auto left_num = parse_int64_value(*left);
  auto right_num = parse_int64_value(*right);
  if (left_num.has_value() && right_num.has_value()) {
    if (*left_num < *right_num) return -1;
    if (*left_num > *right_num) return 1;
    return 0;
  }
  if (*left < *right) return -1;
  if (*left > *right) return 1;
  return 0;
}

std::optional<std::string> relation_field_by_name(const RelationRow& row,
                                                  const std::string& field,
                                                  const std::optional<std::string>& active_alias) {
  size_t dot = field.find('.');
  if (dot != std::string::npos) {
    const std::string alias = lower_alias_name(field.substr(0, dot));
    const std::string col = field.substr(dot + 1);
    auto it = row.aliases.find(alias);
    if (it == row.aliases.end()) return std::nullopt;
    auto value_it = it->second.values.find(col);
    if (value_it == it->second.values.end()) return std::nullopt;
    return value_it->second;
  }
  if (active_alias.has_value()) {
    auto it = row.aliases.find(*active_alias);
    if (it != row.aliases.end()) {
      auto value_it = it->second.values.find(field);
      if (value_it != it->second.values.end()) return value_it->second;
    }
  }
  std::optional<std::string> found;
  for (const auto& alias_entry : row.aliases) {
    auto value_it = alias_entry.second.values.find(field);
    if (value_it == alias_entry.second.values.end()) continue;
    if (found.has_value()) return std::nullopt;
    found = value_it->second;
  }
  return found;
}

bool operand_targets_source_row(const Operand& operand,
                                const std::optional<std::string>& active_alias) {
  if (operand.axis != Operand::Axis::Self) return false;
  if (!operand.qualifier.has_value()) return true;
  if (!active_alias.has_value()) return false;
  return lower_alias_name(*operand.qualifier) == *active_alias;
}

std::optional<std::string> compare_rhs_single_value(const CompareExpr& cmp,
                                                    const RelationRow* outer_row,
                                                    const std::optional<std::string>& active_alias) {
  if (cmp.rhs_expr.has_value()) {
    static const RelationRow kEmptyRow;
    const RelationRow& row = outer_row != nullptr ? *outer_row : kEmptyRow;
    return eval_relation_scalar_expr(*cmp.rhs_expr, row, active_alias);
  }
  if (cmp.rhs.values.size() == 1) {
    return cmp.rhs.values.front();
  }
  return std::nullopt;
}

void collect_source_prefilter_constraints(const Expr& expr,
                                          const std::optional<std::string>& active_alias,
                                          const RelationRow* outer_row,
                                          SourceRowPrefilter& out) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    if (cmp.op != CompareExpr::Op::Eq) return;
    const Operand* lhs = nullptr;
    if (cmp.lhs_expr.has_value() && cmp.lhs_expr->kind == ScalarExpr::Kind::Operand) {
      lhs = &cmp.lhs_expr->operand;
    } else {
      lhs = &cmp.lhs;
    }
    if (!operand_targets_source_row(*lhs, active_alias)) return;
    std::optional<std::string> rhs = compare_rhs_single_value(cmp, outer_row, active_alias);
    if (!rhs.has_value()) return;
    if (lhs->field_kind == Operand::FieldKind::Tag) {
      std::string lowered = util::to_lower(*rhs);
      if (out.tag_eq.has_value() && *out.tag_eq != lowered) {
        out.impossible = true;
      } else {
        out.tag_eq = lowered;
      }
      return;
    }
    if (lhs->field_kind == Operand::FieldKind::ParentId) {
      auto parsed = parse_int64_value(*rhs);
      if (!parsed.has_value()) return;
      if (out.parent_id_eq.has_value() && *out.parent_id_eq != *parsed) {
        out.impossible = true;
      } else {
        out.parent_id_eq = *parsed;
      }
      return;
    }
    return;
  }
  if (std::holds_alternative<std::shared_ptr<BinaryExpr>>(expr)) {
    const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
    if (bin.op != BinaryExpr::Op::And) return;
    collect_source_prefilter_constraints(bin.left, active_alias, outer_row, out);
    collect_source_prefilter_constraints(bin.right, active_alias, outer_row, out);
    return;
  }
}

QueryResult execute_query_with_source_legacy(const Query& query,
                                             const std::string& default_html,
                                             const std::string& default_source_uri);

QueryResult execute_query_with_source_context(
    const Query& query,
    const std::string& default_html,
    const std::string& default_source_uri,
    const std::unordered_map<std::string, Relation>* ctes,
    const RelationRow* outer_row,
    struct RelationRuntimeCache* cache);

struct RelationRuntimeCache {
  std::optional<HtmlDocument> default_document;
  std::optional<std::vector<int64_t>> default_sibling_pos;
};

FragmentSource collect_html_fragments(const QueryResult& result, const std::string& source_name);
HtmlDocument build_fragments_document(const FragmentSource& fragments);

Relation relation_from_query_result(const QueryResult& result, const std::string& alias_name) {
  Relation out;
  const std::string alias = lower_alias_name(alias_name);
  out.warnings = result.warnings;
  for (const auto& row : result.rows) {
    RelationRow rel_row;
    RelationRecord record;
    record.values["node_id"] = std::to_string(row.node_id);
    record.values["tag"] = row.tag;
    record.values["text"] = row.text;
    record.values["inner_html"] = row.inner_html;
    if (row.parent_id.has_value()) {
      record.values["parent_id"] = std::to_string(*row.parent_id);
    } else {
      record.values["parent_id"] = std::nullopt;
    }
    record.values["sibling_pos"] = std::to_string(row.sibling_pos);
    record.values["max_depth"] = std::to_string(row.max_depth);
    record.values["doc_order"] = std::to_string(row.doc_order);
    record.values["source_uri"] = row.source_uri;
    for (const auto& col : result.columns) {
      record.values[col] = field_value_string(row, col);
    }
    for (const auto& attr : row.attributes) {
      record.attributes[attr.first] = attr.second;
      record.values[attr.first] = attr.second;
    }
    rel_row.aliases[alias] = std::move(record);
    merge_alias_columns(out, alias, rel_row.aliases[alias]);
    out.rows.push_back(std::move(rel_row));
  }
  return out;
}

std::vector<int64_t> build_sibling_positions(const HtmlDocument& doc) {
  std::vector<int64_t> sibling_pos(doc.nodes.size(), 1);
  std::vector<std::vector<int64_t>> children(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (node.parent_id.has_value()) {
      children.at(static_cast<size_t>(*node.parent_id)).push_back(node.id);
    }
  }
  for (const auto& kids : children) {
    for (size_t i = 0; i < kids.size(); ++i) {
      sibling_pos.at(static_cast<size_t>(kids[i])) = static_cast<int64_t>(i + 1);
    }
  }
  return sibling_pos;
}

Relation relation_from_document(const HtmlDocument& doc,
                                const std::string& alias_name,
                                const std::string& source_uri,
                                const std::vector<int64_t>* sibling_pos_override = nullptr,
                                const SourceRowPrefilter* prefilter = nullptr) {
  Relation out;
  const std::string alias = lower_alias_name(alias_name);
  std::vector<int64_t> local_sibling_pos;
  if (sibling_pos_override == nullptr) {
    local_sibling_pos = build_sibling_positions(doc);
    sibling_pos_override = &local_sibling_pos;
  }
  std::unordered_map<int64_t, const HtmlNode*> node_by_id;
  node_by_id.reserve(doc.nodes.size());
  for (const auto& n : doc.nodes) {
    node_by_id[n.id] = &n;
  }
  for (const auto& node : doc.nodes) {
    if (prefilter != nullptr) {
      if (prefilter->impossible) continue;
      if (prefilter->parent_id_eq.has_value()) {
        if (!node.parent_id.has_value() || *node.parent_id != *prefilter->parent_id_eq) continue;
      }
      if (prefilter->tag_eq.has_value() && node.tag != *prefilter->tag_eq) continue;
    }
    RelationRow rel_row;
    RelationRecord record;
    record.values["node_id"] = std::to_string(node.id);
    record.values["tag"] = node.tag;
    record.values["text"] = node.text;
    record.values["inner_html"] = node.inner_html;
    if (node.parent_id.has_value()) {
      record.values["parent_id"] = std::to_string(*node.parent_id);
    } else {
      record.values["parent_id"] = std::nullopt;
    }
    record.values["sibling_pos"] =
        std::to_string(sibling_pos_override->at(static_cast<size_t>(node.id)));
    record.values["max_depth"] = std::to_string(node.max_depth);
    record.values["doc_order"] = std::to_string(node.doc_order);
    record.values["source_uri"] = source_uri;
    for (const auto& attr : node.attributes) {
      record.attributes[attr.first] = attr.second;
      record.values[attr.first] = attr.second;
    }
    if (node.parent_id.has_value()) {
      auto parent_it = node_by_id.find(*node.parent_id);
      if (parent_it != node_by_id.end() && parent_it->second != nullptr) {
        const HtmlNode& parent = *parent_it->second;
        record.values["parent.node_id"] = std::to_string(parent.id);
        record.values["parent.tag"] = parent.tag;
        record.values["parent.text"] = parent.text;
        record.values["parent.inner_html"] = parent.inner_html;
        if (parent.parent_id.has_value()) {
          record.values["parent.parent_id"] = std::to_string(*parent.parent_id);
        } else {
          record.values["parent.parent_id"] = std::nullopt;
        }
        record.values["parent.sibling_pos"] =
            std::to_string(sibling_pos_override->at(static_cast<size_t>(parent.id)));
        record.values["parent.max_depth"] = std::to_string(parent.max_depth);
        record.values["parent.doc_order"] = std::to_string(parent.doc_order);
        for (const auto& attr : parent.attributes) {
          record.values["parent." + attr.first] = attr.second;
        }
      }
    }
    rel_row.aliases[alias] = std::move(record);
    merge_alias_columns(out, alias, rel_row.aliases[alias]);
    out.rows.push_back(std::move(rel_row));
  }
  return out;
}

bool merge_row_aliases(RelationRow& target, const RelationRow& add, std::string* duplicate_alias) {
  for (const auto& alias_entry : add.aliases) {
    if (target.aliases.find(alias_entry.first) != target.aliases.end()) {
      if (duplicate_alias != nullptr) *duplicate_alias = alias_entry.first;
      return false;
    }
    target.aliases.insert(alias_entry);
  }
  return true;
}

Relation evaluate_source_relation(const Source& source,
                                  const std::string& default_html,
                                  const std::string& default_source_uri,
                                  const std::unordered_map<std::string, Relation>* ctes,
                                  const RelationRow* outer_row,
                                  RelationRuntimeCache* cache,
                                  const SourceRowPrefilter* prefilter = nullptr) {
  if (source.kind == Source::Kind::CteRef) {
    const std::string lookup = lower_alias_name(source.value);
    if (ctes == nullptr || ctes->find(lookup) == ctes->end()) {
      throw std::runtime_error("Unknown CTE source '" + source.value + "'");
    }
    Relation rel = ctes->at(lookup);
    const std::string target_alias =
        source.alias.has_value() ? lower_alias_name(*source.alias) : lookup;
    if (target_alias != lookup) {
      for (auto& row : rel.rows) {
        auto it = row.aliases.find(lookup);
        if (it == row.aliases.end()) continue;
        RelationRecord record = std::move(it->second);
        row.aliases.erase(it);
        row.aliases[target_alias] = std::move(record);
      }
      auto schema_it = rel.alias_columns.find(lookup);
      if (schema_it != rel.alias_columns.end()) {
        rel.alias_columns[target_alias] = std::move(schema_it->second);
        rel.alias_columns.erase(schema_it);
      }
    }
    return rel;
  }
  if (source.kind == Source::Kind::DerivedSubquery) {
    if (source.derived_query == nullptr) {
      throw std::runtime_error("Derived table source is missing a subquery");
    }
    if (!source.alias.has_value()) {
      throw std::runtime_error("Derived table requires an alias");
    }
    QueryResult sub = execute_query_with_source_context(
        *source.derived_query, default_html, default_source_uri, ctes, outer_row, cache);
    return relation_from_query_result(sub, *source.alias);
  }

  HtmlDocument doc;
  const std::vector<int64_t>* sibling_pos = nullptr;
  std::string source_uri = default_source_uri;
  std::vector<std::string> warnings;
  if (source.kind == Source::Kind::Document) {
    // WHY: WITH/JOIN/LATERAL can revisit FROM doc many times; parse once per statement.
    if (cache != nullptr && cache->default_document.has_value()) {
      doc = *cache->default_document;
    } else {
      doc = parse_html(default_html);
      if (cache != nullptr) {
        cache->default_document = doc;
      }
    }
    if (cache != nullptr) {
      if (!cache->default_sibling_pos.has_value()) {
        cache->default_sibling_pos = build_sibling_positions(doc);
      }
      sibling_pos = &*cache->default_sibling_pos;
    }
  } else if (source.kind == Source::Kind::Path) {
    doc = parse_html(xsql_internal::read_file(source.value));
    source_uri = source.value;
  } else if (source.kind == Source::Kind::Url) {
    doc = parse_html(xsql_internal::fetch_url(source.value, 5000));
    source_uri = source.value;
  } else if (source.kind == Source::Kind::RawHtml) {
    if (source.value.size() > xsql_internal::kMaxRawHtmlBytes) {
      throw std::runtime_error("RAW() HTML exceeds maximum size");
    }
    doc = parse_html(source.value);
    source_uri = "raw";
  } else if (source.kind == Source::Kind::Fragments) {
    FragmentSource fragments;
    if (source.fragments_raw.has_value()) {
      fragments.fragments.push_back(*source.fragments_raw);
    } else if (source.fragments_query != nullptr) {
      const Query& subquery = *source.fragments_query;
      QueryResult sub = execute_query_with_source_context(
          subquery, default_html, default_source_uri, ctes, nullptr, cache);
      fragments = collect_html_fragments(sub, "FRAGMENTS");
    } else {
      throw std::runtime_error("FRAGMENTS requires a subquery or RAW('<...>') input");
    }
    doc = build_fragments_document(fragments);
    source_uri = "fragment";
    warnings.push_back("FRAGMENTS is deprecated; use PARSE(...) instead.");
  } else if (source.kind == Source::Kind::Parse) {
    FragmentSource fragments;
    if (source.parse_expr != nullptr) {
      std::optional<std::string> value = eval_parse_source_expr(*source.parse_expr);
      if (!value.has_value()) {
        throw std::runtime_error("PARSE() requires a non-null HTML string expression");
      }
      std::string trimmed = util::trim_ws(*value);
      if (trimmed.empty() || !looks_like_html_fragment(trimmed)) {
        throw std::runtime_error("PARSE() expects an HTML string expression");
      }
      fragments.fragments.push_back(std::move(trimmed));
    } else if (source.parse_query != nullptr) {
      QueryResult sub = execute_query_with_source_context(
          *source.parse_query, default_html, default_source_uri, ctes, nullptr, cache);
      fragments = collect_html_fragments(sub, "PARSE");
    } else {
      throw std::runtime_error("PARSE() requires an expression or subquery input");
    }
    doc = build_fragments_document(fragments);
    source_uri = "parse";
  } else {
    throw std::runtime_error("Unsupported source kind in relation runtime");
  }
  const std::string alias =
      source.alias.has_value() ? *source.alias : std::string("__self");
  Relation rel = relation_from_document(doc, alias, source_uri, sibling_pos, prefilter);
  for (const auto& warning : warnings) {
    rel.warnings.push_back(warning);
  }
  return rel;
}

Relation evaluate_query_relation(
    const Query& query,
    const std::string& default_html,
    const std::string& default_source_uri,
    const std::unordered_map<std::string, Relation>* parent_ctes,
    const RelationRow* outer_row,
    RelationRuntimeCache* cache) {
  const std::optional<std::string> active_alias =
      query.source.alias.has_value()
          ? std::optional<std::string>(lower_alias_name(*query.source.alias))
          : std::nullopt;

  std::unordered_map<std::string, Relation> local_ctes;
  if (parent_ctes != nullptr) {
    local_ctes = *parent_ctes;
  }
  std::vector<std::string> warnings;
  if (query.with.has_value()) {
    for (const auto& cte : query.with->ctes) {
      if (cte.query == nullptr) {
        throw std::runtime_error("CTE '" + cte.name + "' is missing a subquery");
      }
      QueryResult cte_result = execute_query_with_source_context(
          *cte.query, default_html, default_source_uri, &local_ctes, nullptr, cache);
      Relation cte_relation = relation_from_query_result(cte_result, cte.name);
      warnings.insert(warnings.end(),
                      cte_relation.warnings.begin(),
                      cte_relation.warnings.end());
      local_ctes[lower_alias_name(cte.name)] = std::move(cte_relation);
    }
  }

  std::optional<SourceRowPrefilter> source_prefilter;
  if (query.source.kind == Source::Kind::Document &&
      query.where.has_value()) {
    SourceRowPrefilter candidate;
    collect_source_prefilter_constraints(*query.where, active_alias, outer_row, candidate);
    if (candidate.impossible ||
        candidate.parent_id_eq.has_value() ||
        candidate.tag_eq.has_value()) {
      source_prefilter = std::move(candidate);
    }
  }

  Relation from_rel = evaluate_source_relation(
      query.source, default_html, default_source_uri, &local_ctes, outer_row, cache,
      source_prefilter.has_value() ? &*source_prefilter : nullptr);
  warnings.insert(warnings.end(), from_rel.warnings.begin(), from_rel.warnings.end());

  Relation current;
  current.alias_columns = from_rel.alias_columns;
  current.rows.reserve(from_rel.rows.size());
  for (const auto& base_row : from_rel.rows) {
    RelationRow merged;
    if (outer_row != nullptr) {
      std::string duplicate;
      if (!merge_row_aliases(merged, *outer_row, &duplicate)) {
        throw std::runtime_error("Duplicate source alias '" + duplicate + "' in FROM");
      }
    }
    std::string duplicate;
    if (!merge_row_aliases(merged, base_row, &duplicate)) {
      throw std::runtime_error("Duplicate source alias '" + duplicate + "' in FROM");
    }
    current.rows.push_back(std::move(merged));
  }
  if (outer_row != nullptr) {
    for (const auto& alias_entry : outer_row->aliases) {
      merge_alias_columns(current, alias_entry.first, alias_entry.second);
    }
  }

  for (const auto& join : query.joins) {
    Relation next;
    next.alias_columns = current.alias_columns;
    if (join.lateral) {
      for (const auto& left_row : current.rows) {
        Relation right_rel = evaluate_source_relation(
            join.right_source, default_html, default_source_uri, &local_ctes, &left_row, cache,
            nullptr);
        warnings.insert(warnings.end(), right_rel.warnings.begin(), right_rel.warnings.end());
        for (const auto& alias_cols : right_rel.alias_columns) {
          next.alias_columns[alias_cols.first].insert(
              alias_cols.second.begin(), alias_cols.second.end());
        }
        for (const auto& right_row : right_rel.rows) {
          RelationRow merged = left_row;
          std::string duplicate;
          if (!merge_row_aliases(merged, right_row, &duplicate)) {
            throw std::runtime_error("Duplicate source alias '" + duplicate + "' in FROM");
          }
          next.rows.push_back(std::move(merged));
        }
      }
      current = std::move(next);
      continue;
    }

    Relation right_rel = evaluate_source_relation(
        join.right_source, default_html, default_source_uri, &local_ctes, nullptr, cache,
        nullptr);
    warnings.insert(warnings.end(), right_rel.warnings.begin(), right_rel.warnings.end());
    for (const auto& alias_cols : right_rel.alias_columns) {
      next.alias_columns[alias_cols.first].insert(
          alias_cols.second.begin(), alias_cols.second.end());
    }
    for (const auto& left_row : current.rows) {
      bool matched = false;
      for (const auto& right_row : right_rel.rows) {
        RelationRow merged = left_row;
        std::string duplicate;
        if (!merge_row_aliases(merged, right_row, &duplicate)) {
          throw std::runtime_error("Duplicate source alias '" + duplicate + "' in FROM");
        }
        bool keep = true;
        if (join.type != Query::JoinItem::Type::Cross && join.on.has_value()) {
          keep = eval_relation_expr(*join.on, merged, active_alias);
        }
        if (!keep) continue;
        matched = true;
        next.rows.push_back(std::move(merged));
      }
      if (join.type == Query::JoinItem::Type::Left && !matched) {
        RelationRow padded = left_row;
        for (const auto& alias_cols : right_rel.alias_columns) {
          RelationRecord null_record;
          for (const auto& col : alias_cols.second) {
            null_record.values[col] = std::nullopt;
          }
          padded.aliases[alias_cols.first] = std::move(null_record);
        }
        next.rows.push_back(std::move(padded));
      }
    }
    current = std::move(next);
  }

  if (query.where.has_value()) {
    Relation filtered;
    filtered.alias_columns = current.alias_columns;
    for (const auto& row : current.rows) {
      if (eval_relation_expr(*query.where, row, active_alias)) {
        filtered.rows.push_back(row);
      }
    }
    current = std::move(filtered);
  }

  if (!query.order_by.empty()) {
    std::stable_sort(current.rows.begin(), current.rows.end(),
                     [&](const RelationRow& left, const RelationRow& right) {
                       for (const auto& order : query.order_by) {
                         int cmp = compare_optional_relation_values(
                             relation_field_by_name(left, order.field, active_alias),
                             relation_field_by_name(right, order.field, active_alias));
                         if (cmp == 0) continue;
                         return order.descending ? (cmp > 0) : (cmp < 0);
                       }
                       return false;
                     });
  }
  if (query.limit.has_value() && current.rows.size() > *query.limit) {
    current.rows.resize(*query.limit);
  }
  current.warnings = std::move(warnings);
  return current;
}

void assign_result_column_value(QueryResultRow& row,
                                const std::string& column,
                                const std::optional<std::string>& value) {
  if (!value.has_value()) return;
  if (column == "node_id") {
    if (auto parsed = parse_int64_value(*value); parsed.has_value()) row.node_id = *parsed;
    return;
  }
  if (column == "tag") {
    row.tag = *value;
    return;
  }
  if (column == "text") {
    row.text = *value;
    return;
  }
  if (column == "inner_html") {
    row.inner_html = *value;
    return;
  }
  if (column == "parent_id") {
    if (auto parsed = parse_int64_value(*value); parsed.has_value()) row.parent_id = *parsed;
    return;
  }
  if (column == "sibling_pos") {
    if (auto parsed = parse_int64_value(*value); parsed.has_value()) row.sibling_pos = *parsed;
    return;
  }
  if (column == "max_depth") {
    if (auto parsed = parse_int64_value(*value); parsed.has_value()) row.max_depth = *parsed;
    return;
  }
  if (column == "doc_order") {
    if (auto parsed = parse_int64_value(*value); parsed.has_value()) row.doc_order = *parsed;
    return;
  }
  if (column == "source_uri") {
    row.source_uri = *value;
    return;
  }
  row.computed_fields[column] = *value;
}

QueryResult query_result_from_relation(const Query& query, const Relation& relation) {
  QueryResult out;
  out.columns = xsql_internal::build_columns(query);
  out.columns_implicit = !xsql_internal::is_projection_query(query);
  out.to_list = query.to_list;
  out.to_table = query.to_table;
  out.table_has_header = query.table_has_header;
  out.table_options = to_result_table_options(query.table_options);
  if (query.export_sink.has_value()) {
    const auto& sink = *query.export_sink;
    if (sink.kind == Query::ExportSink::Kind::Csv) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Csv;
    } else if (sink.kind == Query::ExportSink::Kind::Parquet) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Parquet;
    } else if (sink.kind == Query::ExportSink::Kind::Json) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Json;
    } else if (sink.kind == Query::ExportSink::Kind::Ndjson) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Ndjson;
    }
    out.export_sink.path = sink.path;
  }
  out.warnings = relation.warnings;

  for (const auto& item : query.select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      QueryResultRow row;
      row.node_id = static_cast<int64_t>(relation.rows.size());
      out.rows.push_back(std::move(row));
      return out;
    }
  }

  const std::optional<std::string> active_alias =
      query.source.alias.has_value()
          ? std::optional<std::string>(lower_alias_name(*query.source.alias))
          : std::nullopt;

  if (!xsql_internal::is_projection_query(query)) {
    for (const auto& rel_row : relation.rows) {
      const RelationRecord* selected = nullptr;
      for (const auto& item : query.select_items) {
        const std::string tag_or_alias = lower_alias_name(item.tag);
        if (item.tag == "*") {
          if (active_alias.has_value()) {
            auto it = rel_row.aliases.find(*active_alias);
            if (it != rel_row.aliases.end()) selected = &it->second;
          }
          if (selected == nullptr && !rel_row.aliases.empty()) {
            selected = &rel_row.aliases.begin()->second;
          }
          break;
        }
        auto alias_it = rel_row.aliases.find(tag_or_alias);
        if (alias_it != rel_row.aliases.end()) {
          selected = &alias_it->second;
          break;
        }
        for (const auto& alias_entry : rel_row.aliases) {
          auto tag_it = alias_entry.second.values.find("tag");
          if (tag_it == alias_entry.second.values.end() || !tag_it->second.has_value()) continue;
          if (util::to_lower(*tag_it->second) == tag_or_alias) {
            selected = &alias_entry.second;
            break;
          }
        }
        if (selected != nullptr) break;
      }
      if (selected == nullptr) continue;
      QueryResultRow row;
      fill_result_core_from_record(row, *selected);
      out.rows.push_back(std::move(row));
    }
    return out;
  }

  for (const auto& rel_row : relation.rows) {
    QueryResultRow row;
    const RelationRecord* seed = resolve_record(rel_row, std::nullopt, active_alias);
    if (seed != nullptr) {
      fill_result_core_from_record(row, *seed);
    }
    for (const auto& item : query.select_items) {
      if (!item.field.has_value()) continue;
      std::optional<std::string> value;
      if (item.expr_projection && item.expr.has_value()) {
        value = eval_relation_scalar_expr(*item.expr, rel_row, active_alias);
      } else if (item.expr_projection && item.project_expr.has_value()) {
        value = eval_relation_project_expr(
            *item.project_expr, rel_row, active_alias, row.computed_fields);
      } else {
        const std::string lowered_tag = lower_alias_name(item.tag);
        auto it = rel_row.aliases.find(lowered_tag);
        if (it != rel_row.aliases.end()) {
          auto value_it = it->second.values.find(*item.field);
          if (value_it != it->second.values.end()) {
            value = value_it->second;
          }
        } else {
          value = relation_field_by_name(rel_row, *item.field, active_alias);
        }
      }
      assign_result_column_value(row, *item.field, value);
    }
    out.rows.push_back(std::move(row));
  }
  return out;
}

QueryResult execute_query_with_source_context(
    const Query& query,
    const std::string& default_html,
    const std::string& default_source_uri,
    const std::unordered_map<std::string, Relation>* ctes,
    const RelationRow* outer_row,
    RelationRuntimeCache* cache) {
  if (!query_uses_relation_runtime(query, ctes, outer_row)) {
    return execute_query_with_source_legacy(query, default_html, default_source_uri);
  }
  RelationRuntimeCache local_cache;
  RelationRuntimeCache* active_cache = cache != nullptr ? cache : &local_cache;
  Relation relation = evaluate_query_relation(
      query, default_html, default_source_uri, ctes, outer_row, active_cache);
  return query_result_from_relation(query, relation);
}

QueryResult build_meta_result(const std::vector<std::string>& columns,
                              const std::vector<std::vector<std::string>>& rows) {
  QueryResult out;
  out.columns = columns;
  for (const auto& values : rows) {
    QueryResultRow row;
    for (size_t i = 0; i < columns.size() && i < values.size(); ++i) {
      const auto& col = columns[i];
      const auto& value = values[i];
      if (col == "source_uri") {
        row.source_uri = value;
      } else {
        row.attributes[col] = value;
      }
    }
    out.rows.push_back(std::move(row));
  }
  return out;
}

QueryResult execute_meta_query(const Query& query, const std::string& source_uri) {
  switch (query.kind) {
    case Query::Kind::ShowInput: {
      return build_meta_result({"key", "value"},
                               {{"source_uri", source_uri}});
    }
    case Query::Kind::ShowInputs: {
      return build_meta_result({"source_uri"},
                               {{source_uri}});
    }
    case Query::Kind::ShowFunctions: {
      return build_meta_result(
          {"function", "returns", "description"},
          {
              {"text(tag|self)", "string", "Text content of a tag or current row node"},
              {"direct_text(tag|self)", "string", "Immediate text content of a tag or current row node"},
              {"first_text(tag WHERE ...)", "string", "First scoped TEXT match (alias of TEXT(..., 1))"},
              {"last_text(tag WHERE ...)", "string", "Last scoped TEXT match"},
              {"first_attr(tag, attr WHERE ...)", "string", "First scoped ATTR match"},
              {"last_attr(tag, attr WHERE ...)", "string", "Last scoped ATTR match"},
              {"concat(a, b, ...)", "string", "Concatenate strings; NULL if any arg is NULL"},
              {"substring(str, start, len)", "string", "1-based substring"},
              {"substr(str, start, len)", "string", "Alias of substring"},
              {"length(str)", "int64", "String length in UTF-8 bytes"},
              {"char_length(str)", "int64", "Alias of length"},
              {"position(substr IN str)", "int64", "1-based position, 0 if not found"},
              {"locate(substr, str[, start])", "int64", "1-based position, 0 if not found"},
              {"replace(str, from, to)", "string", "Replace substring"},
              {"lower(str)", "string", "Lowercase"},
              {"upper(str)", "string", "Uppercase"},
              {"ltrim(str)", "string", "Trim left whitespace"},
              {"rtrim(str)", "string", "Trim right whitespace"},
              {"coalesce(a, b, ...)", "scalar", "First non-NULL value"},
              {"case when ... then ... else ... end", "scalar", "Conditional expression"},
              {"inner_html(tag|self[, depth|MAX_DEPTH])", "string", "Minified HTML inside a tag/current row node"},
              {"raw_inner_html(tag|self[, depth|MAX_DEPTH])", "string", "Raw inner HTML without minification"},
              {"flatten_text(tag[, depth])", "string[]", "Flatten descendant text at depth into columns"},
              {"flatten(tag[, depth])", "string[]", "Alias of flatten_text"},
              {"project(tag)", "mixed[]", "Evaluate named extraction expressions per row"},
              {"flatten_extract(tag)", "mixed[]", "Compatibility alias of project(tag)"},
              {"trim(inner_html(...))", "string", "Trim whitespace in inner_html"},
              {"count(tag|*)", "int64", "Aggregate node count"},
              {"summarize(*)", "table<tag,count>", "Tag counts summary"},
              {"tfidf(tag|*)", "map<string,double>", "TF-IDF term scores"},
          });
    }
    case Query::Kind::ShowAxes: {
      return build_meta_result(
          {"axis", "description"},
          {
              {"parent", "Parent node"},
              {"child", "Direct child nodes"},
              {"ancestor", "Any ancestor node"},
              {"descendant", "Any descendant node"},
          });
    }
    case Query::Kind::ShowOperators: {
      return build_meta_result(
          {"operator", "description"},
          {
              {"=", "Equality"},
              {"<>", "Not equal"},
              {"<, <=, >, >=", "Ordered comparison"},
              {"IN (...)", "Membership"},
              {"LIKE", "SQL-style wildcard match (% and _)"},
              {"CONTAINS", "Substring or list contains"},
              {"CONTAINS ALL", "Contains all values"},
              {"CONTAINS ANY", "Contains any value"},
              {"IS NULL", "Null check"},
              {"IS NOT NULL", "Not-null check"},
              {"HAS_DIRECT_TEXT", "Direct text predicate"},
              {"~", "Regex match"},
              {"AND", "Logical AND"},
              {"OR", "Logical OR"},
          });
    }
    case Query::Kind::DescribeDoc: {
      return build_meta_result(
          {"column_name", "type", "nullable", "notes"},
          {
              {"node_id", "int64", "false", "Stable node identifier"},
              {"tag", "string", "false", "Lowercase tag name"},
              {"attributes", "map<string,string>", "false", "HTML attributes"},
              {"parent_id", "int64", "true", "Null for root"},
              {"max_depth", "int64", "false", "Max element depth under node"},
              {"doc_order", "int64", "false", "Preorder document index"},
              {"sibling_pos", "int64", "false", "1-based among siblings"},
              {"source_uri", "string", "true", "Empty for RAW/STDIN"},
          });
    }
    case Query::Kind::DescribeLanguage: {
      return build_meta_result(
          {"category", "name", "syntax", "notes"},
          {
              {"clause", "SELECT", "SELECT <tag|*>[, ...]", "Tag list or *"},
              {"clause", "FROM", "FROM <source>", "Defaults to document in REPL"},
              {"clause", "WHERE", "WHERE <expr>", "Predicate expression"},
              {"clause", "ORDER BY", "ORDER BY <field> [ASC|DESC]",
               "node_id, tag, text, parent_id, sibling_pos, max_depth, doc_order; SUMMARIZE uses tag/count"},
              {"clause", "LIMIT", "LIMIT <n>", "n >= 0, max enforced"},
              {"clause", "EXCLUDE", "EXCLUDE <field>[, ...]", "Only with SELECT *"},
              {"output", "TO LIST", "TO LIST()", "Requires one projected column"},
              {"output", "TO TABLE",
               "TO TABLE([HEADER|NOHEADER][, TRIM_EMPTY_ROWS=ON][, TRIM_EMPTY_COLS=TRAILING|ALL]"
               "[, EMPTY_IS=...][, STOP_AFTER_EMPTY_ROWS=n][, FORMAT=SPARSE][, SPARSE_SHAPE=LONG|WIDE]"
               "[, HEADER_NORMALIZE=ON][, EXPORT='file.csv'])",
               "Select table tags only"},
              {"output", "TO CSV", "TO CSV('file.csv')", "Export result"},
              {"output", "TO PARQUET", "TO PARQUET('file.parquet')", "Export result"},
              {"output", "TO JSON", "TO JSON(['file.json'])", "Export rows as a JSON array"},
              {"output", "TO NDJSON", "TO NDJSON(['file.ndjson'])", "Export rows as newline-delimited JSON"},
              {"source", "document", "FROM document", "Active input in REPL"},
              {"source", "alias", "FROM doc", "Alias for document"},
              {"source", "path", "FROM 'file.html'", "Local file"},
              {"source", "url", "FROM 'https://example.com'", "Requires libcurl"},
              {"source", "raw", "FROM RAW('<html>')", "Inline HTML"},
              {"source", "parse", "FROM PARSE('<ul><li>...</li></ul>') AS frag",
               "Parse HTML strings into a node source"},
              {"source", "fragments", "FROM FRAGMENTS(<raw|subquery>)",
               "Concatenate HTML fragments (deprecated; use PARSE)"},
              {"source", "fragments_raw", "FRAGMENTS(RAW('<ul>...</ul>'))", "Raw fragment input"},
              {"source", "fragments_query",
               "FRAGMENTS(SELECT inner_html(...) FROM doc)", "Subquery returns HTML strings"},
              {"field", "node_id", "node_id", "int64"},
              {"field", "tag", "tag", "lowercase"},
              {"field", "attributes", "attributes", "map<string,string>"},
              {"field", "parent_id", "parent_id", "int64 or null"},
              {"field", "sibling_pos", "sibling_pos", "1-based among siblings"},
              {"field", "source_uri", "source_uri", "Hidden unless multi-source"},
              {"function", "text", "text(tag|self)", "Direct text content; requires WHERE"},
              {"function", "inner_html", "inner_html(tag|self[, depth|MAX_DEPTH])",
               "Minified inner HTML; depth defaults to 1; requires WHERE"},
              {"function", "raw_inner_html", "raw_inner_html(tag|self[, depth|MAX_DEPTH])",
               "Raw inner HTML (no minify); depth defaults to 1; requires WHERE"},
              {"function", "trim", "trim(text(...)) | trim(inner_html(...))",
               "Trim whitespace"},
              {"function", "direct_text", "direct_text(tag|self)", "Immediate text children only"},
              {"function", "concat", "concat(a, b, ...)", "NULL if any arg is NULL"},
              {"function", "substring", "substring(str, start, len)", "1-based slicing"},
              {"function", "length", "length(str)", "UTF-8 byte length"},
              {"function", "position", "position(substr IN str)", "1-based; 0 if not found"},
              {"function", "replace", "replace(str, from, to)", "Substring replacement"},
              {"function", "case expression",
               "CASE WHEN <expr> THEN <value> [ELSE <value>] END",
               "Evaluates WHEN clauses top-to-bottom"},
              {"function", "trim family", "ltrim/rtrim/trim(str)", "Whitespace trimming"},
              {"function", "first_text", "first_text(tag WHERE ...)", "First scoped text match"},
              {"function", "last_text", "last_text(tag WHERE ...)", "Last scoped text match"},
              {"function", "first_attr", "first_attr(tag, attr WHERE ...)", "First scoped attr match"},
              {"function", "last_attr", "last_attr(tag, attr WHERE ...)", "Last scoped attr match"},
              {"function", "project",
               "project(tag) AS (alias: expr, ...)",
               "Expressions: TEXT/ATTR/DIRECT_TEXT/COALESCE plus SQL string functions"},
              {"function", "flatten_extract",
               "flatten_extract(tag) AS (alias: expr, ...)",
               "Expressions: TEXT/ATTR/DIRECT_TEXT/COALESCE plus SQL string functions"},
              {"aggregate", "count", "count(tag|*)", "int64"},
              {"aggregate", "summarize", "summarize(*)", "tag counts table"},
              {"aggregate", "tfidf", "tfidf(tag|*)", "map<string,double>"},
              {"axis", "parent", "parent.<field>", "Direct parent"},
              {"axis", "child", "child.<field>", "Direct child"},
              {"axis", "ancestor", "ancestor.<field>", "Any ancestor"},
              {"axis", "descendant", "descendant.<field>", "Any descendant"},
              {"predicate", "exists", "EXISTS(axis [WHERE expr])", "Existential axis predicate"},
              {"operator", "=", "lhs = rhs", "Equality"},
              {"operator", "<>", "lhs <> rhs", "Not equal"},
              {"operator", "<, <=, >, >=", "lhs > rhs", "Ordered comparison"},
              {"operator", "IN", "lhs IN ('a','b')", "Membership"},
              {"operator", "LIKE", "lhs LIKE '%x%'", "SQL-style wildcard match"},
              {"operator", "CONTAINS", "lhs CONTAINS 'x'", "Substring or list contains"},
              {"operator", "CONTAINS ALL", "lhs CONTAINS ALL ('a','b')", "All values"},
              {"operator", "CONTAINS ANY", "lhs CONTAINS ANY ('a','b')", "Any value"},
              {"operator", "IS NULL", "lhs IS NULL", "Null check"},
              {"operator", "IS NOT NULL", "lhs IS NOT NULL", "Not-null check"},
              {"operator", "HAS_DIRECT_TEXT", "HAS_DIRECT_TEXT", "Predicate on direct text"},
              {"operator", "~", "lhs ~ 're'", "Regex match"},
              {"operator", "AND", "expr AND expr", "Logical AND"},
              {"operator", "OR", "expr OR expr", "Logical OR"},
              {"meta", "SHOW INPUT", "SHOW INPUT", "Active source"},
              {"meta", "SHOW INPUTS", "SHOW INPUTS", "Last result sources or active"},
              {"meta", "SHOW FUNCTIONS", "SHOW FUNCTIONS", "Function list"},
              {"meta", "SHOW AXES", "SHOW AXES", "Axis list"},
              {"meta", "SHOW OPERATORS", "SHOW OPERATORS", "Operator list"},
              {"meta", "DESCRIBE doc", "DESCRIBE doc", "Document schema"},
              {"meta", "DESCRIBE language", "DESCRIBE language", "Language spec"},
          });
    }
    case Query::Kind::Select:
    default:
      return QueryResult{};
  }
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
    if (trimmed.size() > xsql_internal::kMaxFragmentHtmlBytes) {
      throw std::runtime_error(source_name + " HTML fragment exceeds maximum size");
    }
    total_bytes += trimmed.size();
    if (out.fragments.size() >= xsql_internal::kMaxFragmentCount) {
      throw std::runtime_error(source_name + " exceeds maximum fragment count");
    }
    if (total_bytes > xsql_internal::kMaxFragmentBytes) {
      throw std::runtime_error(source_name + " exceeds maximum total HTML size");
    }
    out.fragments.push_back(std::move(trimmed));
  }
  if (out.fragments.empty()) {
    throw std::runtime_error(source_name + " produced no HTML fragments");
  }
  return out;
}

QueryResult execute_query_ast(const Query& query, const HtmlDocument& doc, const std::string& source_uri) {
  ExecuteResult exec = execute_query(query, doc, source_uri);
  QueryResult out;
  out.columns = xsql_internal::build_columns(query);
  out.columns_implicit = !xsql_internal::is_projection_query(query);
  out.source_uri_excluded =
      std::find(query.exclude_fields.begin(), query.exclude_fields.end(), "source_uri") !=
      query.exclude_fields.end();
  out.to_list = query.to_list;
  out.to_table = query.to_table;
  out.table_has_header = query.table_has_header;
  out.table_options = to_result_table_options(query.table_options);
  if (query.export_sink.has_value()) {
    const auto& sink = *query.export_sink;
    if (sink.kind == Query::ExportSink::Kind::Csv) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Csv;
    } else if (sink.kind == Query::ExportSink::Kind::Parquet) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Parquet;
    } else if (sink.kind == Query::ExportSink::Kind::Json) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Json;
    } else if (sink.kind == Query::ExportSink::Kind::Ndjson) {
      out.export_sink.kind = QueryResult::ExportSink::Kind::Ndjson;
    }
    out.export_sink.path = sink.path;
  }
  if (query.export_sink.has_value() &&
      (query.to_table || xsql_internal::is_table_select(query)) &&
      exec.nodes.size() != 1) {
    throw std::runtime_error(
        "Export requires a single table result; add a filter to select one table");
  }
  if (!query.select_items.empty() &&
      query.select_items[0].aggregate == Query::SelectItem::Aggregate::Tfidf) {
    out.rows = xsql_internal::build_tfidf_rows(query, exec.nodes);
    return out;
  }
  if (!query.select_items.empty() &&
      query.select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize) {
    std::unordered_map<std::string, size_t> counts;
    for (const auto& node : exec.nodes) {
      ++counts[node.tag];
    }
    std::vector<std::pair<std::string, size_t>> summary;
    summary.reserve(counts.size());
    for (const auto& kv : counts) {
      summary.emplace_back(kv.first, kv.second);
    }
    if (!query.order_by.empty()) {
      std::sort(summary.begin(), summary.end(),
                [&](const auto& a, const auto& b) {
                  for (const auto& order_by : query.order_by) {
                    int cmp = 0;
                    if (order_by.field == "count") {
                      if (a.second < b.second) cmp = -1;
                      else if (a.second > b.second) cmp = 1;
                    } else {
                      if (a.first < b.first) cmp = -1;
                      else if (a.first > b.first) cmp = 1;
                    }
                    if (cmp == 0) continue;
                    if (order_by.descending) {
                      return cmp > 0;
                    }
                    return cmp < 0;
                  }
                  return false;
                });
    } else {
      std::sort(summary.begin(), summary.end(),
                [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;
                });
    }
    if (query.limit.has_value() && summary.size() > *query.limit) {
      summary.resize(*query.limit);
    }
    for (const auto& item : summary) {
      QueryResultRow row;
      row.tag = item.first;
      row.node_id = static_cast<int64_t>(item.second);
      row.source_uri = source_uri;
      out.rows.push_back(std::move(row));
    }
    return out;
  }
  // WHY: table extraction bypasses row projections to preserve table layout.
  if (query.to_table ||
      (query.export_sink.has_value() && xsql_internal::is_table_select(query))) {
    auto children = xsql_internal::build_children(doc);
    for (const auto& node : exec.nodes) {
      QueryResult::TableResult table;
      table.node_id = node.id;
      xsql_internal::collect_rows(doc, children, node.id, table.rows);
      if (!table_uses_default_output(query)) {
        MaterializedTable materialized =
            materialize_table(table.rows, query.table_has_header, query.table_options);
        table.headers = std::move(materialized.headers);
        table.header_keys = std::move(materialized.header_keys);
        if (query.table_options.format == Query::TableOptions::Format::Sparse) {
          if (query.table_options.sparse_shape == Query::TableOptions::SparseShape::Long) {
            table.rows = std::move(materialized.sparse_long_rows);
          } else {
            table.rows.clear();
            table.sparse_wide_rows = std::move(materialized.sparse_wide_rows);
          }
        } else {
          table.rows = std::move(materialized.rect_rows);
        }
      }
      out.tables.push_back(std::move(table));
    }
    return out;
  }
  const Query::SelectItem* flatten_item = nullptr;
  const Query::SelectItem* flatten_extract_item = nullptr;
  for (const auto& item : query.select_items) {
    if (item.flatten_text) {
      flatten_item = &item;
    }
    if (item.flatten_extract) {
      flatten_extract_item = &item;
    }
  }
  if (flatten_extract_item != nullptr) {
    auto children = xsql_internal::build_children(doc);
    std::vector<int64_t> sibling_positions(doc.nodes.size(), 1);
    for (size_t parent = 0; parent < children.size(); ++parent) {
      const auto& kids = children[parent];
      for (size_t idx = 0; idx < kids.size(); ++idx) {
        sibling_positions.at(static_cast<size_t>(kids[idx])) = static_cast<int64_t>(idx + 1);
      }
    }
    std::string base_tag = util::to_lower(flatten_extract_item->tag);
    bool tag_is_alias = query.source.alias.has_value() &&
                        util::to_lower(*query.source.alias) == base_tag;
    bool match_all_tags = tag_is_alias || base_tag == "document";
    struct FlattenExtractRow {
      const HtmlNode* node = nullptr;
      QueryResultRow row;
    };
    std::vector<FlattenExtractRow> rows;
    rows.reserve(doc.nodes.size());
    for (const auto& node : doc.nodes) {
      if (!match_all_tags && node.tag != base_tag) {
        continue;
      }
      if (query.where.has_value()) {
        if (!executor_internal::eval_expr(*query.where, doc, children, node)) {
          continue;
        }
      }
      QueryResultRow row;
      row.node_id = node.id;
      row.tag = node.tag;
      row.text = node.text;
      row.inner_html = node.inner_html;
      row.attributes = node.attributes;
      row.source_uri = source_uri;
      row.sibling_pos = sibling_positions.at(static_cast<size_t>(node.id));
      row.max_depth = node.max_depth;
      row.doc_order = node.doc_order;
      row.parent_id = node.parent_id;

      for (size_t i = 0; i < flatten_extract_item->flatten_extract_aliases.size(); ++i) {
        const auto& alias = flatten_extract_item->flatten_extract_aliases[i];
        const auto& expr = flatten_extract_item->flatten_extract_exprs[i];
        std::optional<std::string> value =
            eval_flatten_extract_expr(expr, node, doc, children, row.computed_fields);
        if (!value.has_value()) continue;
        row.computed_fields[alias] = *value;
      }
      rows.push_back(FlattenExtractRow{&node, std::move(row)});
    }
    if (!query.order_by.empty()) {
      std::stable_sort(rows.begin(), rows.end(),
                       [&](const auto& left, const auto& right) {
                         for (const auto& order_by : query.order_by) {
                           int cmp = executor_internal::compare_nodes(*left.node, *right.node, order_by.field);
                           if (cmp == 0) continue;
                           if (order_by.descending) {
                             return cmp > 0;
                           }
                           return cmp < 0;
                         }
                         return false;
                       });
    }
    if (query.limit.has_value() && rows.size() > *query.limit) {
      rows.resize(*query.limit);
    }
    out.rows.reserve(rows.size());
    for (auto& entry : rows) {
      out.rows.push_back(std::move(entry.row));
    }
    return out;
  }
  if (flatten_item != nullptr) {
    auto children = xsql_internal::build_children(doc);
    std::vector<int64_t> sibling_positions(doc.nodes.size(), 1);
    for (size_t parent = 0; parent < children.size(); ++parent) {
      const auto& kids = children[parent];
      for (size_t idx = 0; idx < kids.size(); ++idx) {
        sibling_positions.at(static_cast<size_t>(kids[idx])) = static_cast<int64_t>(idx + 1);
      }
    }
    DescendantTagFilter descendant_filter;
    if (query.where.has_value()) {
      collect_descendant_tag_filter(*query.where, descendant_filter);
    }
    std::string base_tag = util::to_lower(flatten_item->tag);
    bool tag_is_alias = query.source.alias.has_value() &&
                        util::to_lower(*query.source.alias) == base_tag;
    bool match_all_tags = tag_is_alias || base_tag == "document";
    struct FlattenRow {
      const HtmlNode* node = nullptr;
      QueryResultRow row;
    };
    std::vector<FlattenRow> rows;
    rows.reserve(doc.nodes.size());
    for (const auto& node : doc.nodes) {
      if (!match_all_tags && node.tag != base_tag) {
        continue;
      }
      if (query.where.has_value()) {
        if (!executor_internal::eval_expr_flatten_base(*query.where, doc, children, node)) {
          continue;
        }
      }
      QueryResultRow row;
      row.node_id = node.id;
      row.tag = node.tag;
      row.text = node.text;
      row.inner_html = node.inner_html;
      row.attributes = node.attributes;
      row.source_uri = source_uri;
      row.sibling_pos = sibling_positions.at(static_cast<size_t>(node.id));
      row.max_depth = node.max_depth;
      row.doc_order = node.doc_order;
      row.parent_id = node.parent_id;

      std::vector<int64_t> descendants;
      bool depth_is_default = !flatten_item->flatten_depth.has_value();
      if (depth_is_default) {
        collect_descendants_any_depth(children, node.id, descendants);
      } else {
        collect_descendants_at_depth(children, node.id, *flatten_item->flatten_depth, descendants);
      }
      std::vector<std::string> values;
      for (int64_t id : descendants) {
        const auto& child = doc.nodes.at(static_cast<size_t>(id));
        bool matched = true;
        for (const auto& pred : descendant_filter.predicates) {
          if (!match_descendant_predicate(child, pred)) {
            matched = false;
            break;
          }
        }
        if (!matched) continue;
        std::string direct = xsql_internal::extract_direct_text_strict(child.inner_html);
        std::string normalized = normalize_flatten_text(direct);
        if (normalized.empty()) {
          direct = xsql_internal::extract_direct_text(child.inner_html);
          normalized = normalize_flatten_text(direct);
        }
        if (depth_is_default && normalized.empty()) {
          continue;
        }
        values.push_back(std::move(normalized));
      }
      for (size_t i = 0; i < flatten_item->flatten_aliases.size(); ++i) {
        if (i < values.size()) {
          row.computed_fields[flatten_item->flatten_aliases[i]] = values[i];
        }
      }
      rows.push_back(FlattenRow{&node, std::move(row)});
    }
    if (!query.order_by.empty()) {
      std::stable_sort(rows.begin(), rows.end(),
                       [&](const auto& left, const auto& right) {
                         for (const auto& order_by : query.order_by) {
                           int cmp = executor_internal::compare_nodes(*left.node, *right.node, order_by.field);
                           if (cmp == 0) continue;
                           if (order_by.descending) {
                             return cmp > 0;
                           }
                           return cmp < 0;
                         }
                         return false;
                       });
    }
    if (query.limit.has_value() && rows.size() > *query.limit) {
      rows.resize(*query.limit);
    }
    out.rows.reserve(rows.size());
    for (auto& entry : rows) {
      out.rows.push_back(std::move(entry.row));
    }
    return out;
  }
  for (const auto& item : query.select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      QueryResultRow row;
      row.node_id = static_cast<int64_t>(exec.nodes.size());
      row.source_uri = source_uri;
      out.rows.push_back(row);
      return out;
    }
  }
  auto inner_html_depth = xsql_internal::find_inner_html_depth(query);
  bool inner_html_auto_depth = xsql_internal::has_inner_html_auto_depth(query);
  std::unordered_set<std::string> trim_fields;
  trim_fields.reserve(query.select_items.size());
  for (const auto& item : query.select_items) {
    if (!item.trim || !item.field.has_value()) {
      continue;
    }
    trim_fields.insert(*item.field);
  }
  bool use_text_function = false;
  bool use_inner_html_function = false;
  bool use_raw_inner_html_function = false;
  for (const auto& item : query.select_items) {
    if (item.field.has_value() && *item.field == "text" && item.text_function) {
      use_text_function = true;
    }
    if (item.field.has_value() && *item.field == "inner_html" && item.inner_html_function) {
      use_inner_html_function = true;
      if (item.raw_inner_html_function) {
        use_raw_inner_html_function = true;
      }
    }
  }
  auto children = xsql_internal::build_children(doc);
  std::vector<int64_t> sibling_positions(doc.nodes.size(), 1);
  for (size_t parent = 0; parent < children.size(); ++parent) {
    const auto& kids = children[parent];
    for (size_t idx = 0; idx < kids.size(); ++idx) {
      sibling_positions.at(static_cast<size_t>(kids[idx])) = static_cast<int64_t>(idx + 1);
    }
  }
  for (const auto& node : exec.nodes) {
    QueryResultRow row;
    row.node_id = node.id;
    row.tag = node.tag;
    std::optional<size_t> effective_inner_html_depth = inner_html_depth;
    if (!effective_inner_html_depth.has_value() && use_inner_html_function) {
      // WHY: MAX_DEPTH means "full subtree for this row" without guessing a literal depth.
      effective_inner_html_depth = inner_html_auto_depth
                                       ? static_cast<size_t>(std::max<int64_t>(0, node.max_depth))
                                       : 1;
    }
    row.text = use_text_function ? xsql_internal::extract_direct_text(node.inner_html) : node.text;
    row.inner_html = effective_inner_html_depth.has_value()
                         ? xsql_internal::limit_inner_html(node.inner_html, *effective_inner_html_depth)
                         : node.inner_html;
    if (use_inner_html_function && !use_raw_inner_html_function) {
      row.inner_html = util::minify_html(row.inner_html);
    }
    row.attributes = node.attributes;
    row.source_uri = source_uri;
    row.sibling_pos = sibling_positions.at(static_cast<size_t>(node.id));
    row.max_depth = node.max_depth;
    row.doc_order = node.doc_order;
    for (const auto& item : query.select_items) {
      if (!item.expr_projection || !item.field.has_value()) continue;
      if (item.project_expr.has_value()) {
        std::optional<std::string> value =
            eval_flatten_extract_expr(*item.project_expr, node, doc, children, row.computed_fields);
        if (!value.has_value()) continue;
        row.computed_fields[*item.field] = *value;
        continue;
      }
      if (!item.expr.has_value()) continue;
      ScalarProjectionValue value = eval_select_scalar_expr(*item.expr, node, &doc, &children);
      if (projection_is_null(value)) continue;
      row.computed_fields[*item.field] = projection_to_string(value);
    }
    for (const auto& field : trim_fields) {
      if (field == "text") {
        row.text = util::trim_ws(row.text);
      } else if (field == "inner_html") {
        row.inner_html = util::trim_ws(row.inner_html);
      } else if (field == "tag") {
        row.tag = util::trim_ws(row.tag);
      } else if (field == "source_uri") {
        row.source_uri = util::trim_ws(row.source_uri);
      } else {
        auto it = row.attributes.find(field);
        if (it != row.attributes.end()) {
          it->second = util::trim_ws(it->second);
        }
      }
    }
    row.parent_id = node.parent_id;
    out.rows.push_back(row);
  }
  return out;
}

void validate_query(const Query& query) {
  if (query.kind != Query::Kind::Select) {
    return;
  }
  const bool relation_runtime =
      query.with.has_value() ||
      !query.joins.empty() ||
      query.source.kind == Source::Kind::CteRef ||
      query.source.kind == Source::Kind::DerivedSubquery;
  if (relation_runtime) {
    if (query.to_table) {
      throw std::runtime_error("TO TABLE() is not supported with WITH/JOIN queries");
    }
    xsql_internal::validate_limits(query);
    xsql_internal::validate_predicates(query);
    return;
  }
  xsql_internal::validate_projection(query);
  xsql_internal::validate_order_by(query);
  xsql_internal::validate_to_table(query);
  xsql_internal::validate_export_sink(query);
  xsql_internal::validate_qualifiers(query);
  xsql_internal::validate_predicates(query);
  xsql_internal::validate_limits(query);
}

QueryResult execute_query_with_source_legacy(const Query& query,
                                             const std::string& default_html,
                                             const std::string& default_source_uri) {
  std::string effective_source_uri = default_source_uri;
  if (is_plain_count_star_document_query(query)) {
    // WHY: COUNT(*) FROM doc does not need per-node inner_html/text materialization.
    int64_t count = count_html_nodes_fast(default_html);
    return build_count_star_result(query, count, effective_source_uri);
  }
  if (query.source.kind == Source::Kind::RawHtml) {
    if (query.source.value.size() > xsql_internal::kMaxRawHtmlBytes) {
      throw std::runtime_error("RAW() HTML exceeds maximum size");
    }
    HtmlDocument doc = parse_html(query.source.value);
    effective_source_uri = "raw";
    return execute_query_ast(query, doc, effective_source_uri);
  }
  if (query.source.kind == Source::Kind::Fragments) {
    FragmentSource fragments;
    if (query.source.fragments_raw.has_value()) {
      if (query.source.fragments_raw->size() > xsql_internal::kMaxRawHtmlBytes) {
        throw std::runtime_error("FRAGMENTS RAW() input exceeds maximum size");
      }
      fragments.fragments.push_back(*query.source.fragments_raw);
    } else if (query.source.fragments_query != nullptr) {
      const Query& subquery = *query.source.fragments_query;
      validate_query(subquery);
      if (subquery.source.kind == Source::Kind::Path || subquery.source.kind == Source::Kind::Url) {
        throw std::runtime_error("FRAGMENTS subquery cannot use URL or file sources");
      }
      QueryResult sub_result = execute_query_with_source_context(
          subquery, default_html, default_source_uri, nullptr, nullptr, nullptr);
      fragments = collect_html_fragments(sub_result, "FRAGMENTS");
    } else {
      throw std::runtime_error("FRAGMENTS requires a subquery or RAW('<...>') input");
    }
    HtmlDocument doc = build_fragments_document(fragments);
    effective_source_uri = "fragment";
    QueryResult out = execute_query_ast(query, doc, effective_source_uri);
    out.warnings.push_back("FRAGMENTS is deprecated; use PARSE(...) instead.");
    return out;
  }
  if (query.source.kind == Source::Kind::Parse) {
    FragmentSource fragments;
    if (query.source.parse_expr != nullptr) {
      std::optional<std::string> value = eval_parse_source_expr(*query.source.parse_expr);
      if (!value.has_value()) {
        throw std::runtime_error("PARSE() requires a non-null HTML string expression");
      }
      std::string trimmed = util::trim_ws(*value);
      if (trimmed.empty()) {
        throw std::runtime_error("PARSE() produced no HTML fragments");
      }
      if (!looks_like_html_fragment(trimmed)) {
        throw std::runtime_error("PARSE() expects an HTML string expression");
      }
      if (trimmed.size() > xsql_internal::kMaxFragmentHtmlBytes) {
        throw std::runtime_error("PARSE() HTML fragment exceeds maximum size");
      }
      fragments.fragments.push_back(std::move(trimmed));
    } else if (query.source.parse_query != nullptr) {
      const Query& subquery = *query.source.parse_query;
      validate_query(subquery);
      if (subquery.source.kind == Source::Kind::Path || subquery.source.kind == Source::Kind::Url) {
        throw std::runtime_error("PARSE() subquery cannot use URL or file sources");
      }
      QueryResult sub_result = execute_query_with_source_context(
          subquery, default_html, default_source_uri, nullptr, nullptr, nullptr);
      fragments = collect_html_fragments(sub_result, "PARSE()");
    } else {
      throw std::runtime_error("PARSE() requires a scalar expression or subquery input");
    }
    HtmlDocument doc = build_fragments_document(fragments);
    effective_source_uri = "parse";
    return execute_query_ast(query, doc, effective_source_uri);
  }
  HtmlDocument doc = parse_html(default_html);
  return execute_query_ast(query, doc, effective_source_uri);
}

QueryResult execute_query_with_source(const Query& query,
                                      const std::string& default_html,
                                      const std::string& default_source_uri) {
  return execute_query_with_source_context(
      query, default_html, default_source_uri, nullptr, nullptr, nullptr);
}

/// Executes a parsed query over provided HTML and assembles QueryResult.
/// MUST apply validation before execution and MUST propagate errors as exceptions.
/// Inputs are HTML/source/query; outputs are QueryResult with no side effects.
QueryResult execute_query_from_html(const std::string& html,
                                    const std::string& source_uri,
                                    const std::string& query) {
  auto parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    throw std::runtime_error("Query parse error: " + parsed.error->message);
  }
  validate_query(*parsed.query);
  if (parsed.query->kind != Query::Kind::Select) {
    return execute_meta_query(*parsed.query, source_uri);
  }
  return execute_query_with_source(*parsed.query, html, source_uri);
}

}  // namespace

/// Executes a query over in-memory HTML with document as the source label.
/// MUST not perform IO and MUST propagate parse/validation failures.
/// Inputs are HTML/query; outputs are QueryResult with no side effects.
QueryResult execute_query_from_document(const std::string& html, const std::string& query) {
  return execute_query_from_html(html, "document", query);
}

/// Executes a query over a file and uses the path as source label.
/// MUST read from disk and MUST propagate IO failures as exceptions.
/// Inputs are path/query; outputs are QueryResult with file IO side effects.
QueryResult execute_query_from_file(const std::string& path, const std::string& query) {
  std::string html = xsql_internal::read_file(path);
  return execute_query_from_html(html, path, query);
}

/// Executes a query over a URL and uses the URL as source label.
/// MUST honor timeout_ms and MUST propagate network failures as exceptions.
/// Inputs are url/query/timeout; outputs are QueryResult with network side effects.
QueryResult execute_query_from_url(const std::string& url, const std::string& query, int timeout_ms) {
  std::string html = xsql_internal::fetch_url(url, timeout_ms);
  return execute_query_from_html(html, url, query);
}

}  // namespace xsql
