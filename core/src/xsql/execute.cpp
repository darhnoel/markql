#include "xsql/xsql.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "../executor/executor_internal.h"
#include "../executor.h"
#include "../html_parser.h"
#include "../query_parser.h"
#include "../util/string_util.h"
#include "xsql_internal.h"

namespace xsql {

struct ParsedDocumentHandle {
  HtmlDocument doc;
  std::string html;
  std::string source_uri;
};

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

ScalarProjectionValue eval_select_scalar_expr(const ScalarExpr& expr, const HtmlNode& node) {
  switch (expr.kind) {
    case ScalarExpr::Kind::NullLiteral:
      return make_null_projection();
    case ScalarExpr::Kind::StringLiteral:
      return make_string_projection(expr.string_value);
    case ScalarExpr::Kind::NumberLiteral:
      return make_number_projection(expr.number_value);
    case ScalarExpr::Kind::Operand: {
      const Operand& op = expr.operand;
      if (op.axis != Operand::Axis::Self) return make_null_projection();
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
      ScalarProjectionValue arg_value = eval_select_scalar_expr(first_arg, node);
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
      ScalarProjectionValue attr_value = eval_select_scalar_expr(expr.args[1], node);
      if (projection_is_null(attr_value)) return make_null_projection();
      std::string attr = util::to_lower(projection_to_string(attr_value));
      auto it = target->attributes.find(attr);
      if (it == target->attributes.end()) return make_null_projection();
      return make_string_projection(it->second);
    }

    size_t depth = 1;
    if (expr.args.size() == 2) {
      ScalarProjectionValue depth_value = eval_select_scalar_expr(expr.args[1], node);
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
    args.push_back(eval_select_scalar_expr(arg, node));
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
              {"output", "TO TABLE", "TO TABLE([HEADER|NOHEADER][, EXPORT='file.csv'])",
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
              {"source", "fragments", "FROM FRAGMENTS(<raw|subquery>)",
               "Concatenate HTML fragments"},
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

FragmentSource collect_fragments(const QueryResult& result) {
  if (result.to_table || !result.tables.empty()) {
    throw std::runtime_error("FRAGMENTS does not accept TO TABLE() results");
  }
  if (result.columns.size() != 1) {
    throw std::runtime_error("FRAGMENTS expects a single HTML string column");
  }
  const std::string& field = result.columns[0];
  FragmentSource out;
  size_t total_bytes = 0;
  for (const auto& row : result.rows) {
    std::optional<std::string> value = field_value_string(row, field);
    if (!value.has_value()) {
      throw std::runtime_error("FRAGMENTS expects HTML strings (use inner_html(...) or RAW('<...>'))");
    }
    std::string trimmed = util::trim_ws(*value);
    if (trimmed.empty()) {
      continue;
    }
    if (!looks_like_html_fragment(trimmed)) {
      throw std::runtime_error("FRAGMENTS expects HTML strings (use inner_html(...) or RAW('<...>'))");
    }
    if (trimmed.size() > xsql_internal::kMaxFragmentHtmlBytes) {
      throw std::runtime_error("FRAGMENTS HTML fragment exceeds maximum size");
    }
    total_bytes += trimmed.size();
    if (out.fragments.size() >= xsql_internal::kMaxFragmentCount) {
      throw std::runtime_error("FRAGMENTS exceeds maximum fragment count");
    }
    if (total_bytes > xsql_internal::kMaxFragmentBytes) {
      throw std::runtime_error("FRAGMENTS exceeds maximum total HTML size");
    }
    out.fragments.push_back(std::move(trimmed));
  }
  if (out.fragments.empty()) {
    throw std::runtime_error("FRAGMENTS produced no HTML fragments");
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
      ScalarProjectionValue value = eval_select_scalar_expr(*item.expr, node);
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
  xsql_internal::validate_projection(query);
  xsql_internal::validate_order_by(query);
  xsql_internal::validate_to_table(query);
  xsql_internal::validate_export_sink(query);
  xsql_internal::validate_qualifiers(query);
  xsql_internal::validate_predicates(query);
  xsql_internal::validate_limits(query);
}

QueryResult execute_query_with_source(const Query& query,
                                      const std::string& default_html,
                                      const std::string& default_source_uri) {
  std::string effective_source_uri = default_source_uri;
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
      QueryResult sub_result = execute_query_with_source(subquery, default_html, default_source_uri);
      fragments = collect_fragments(sub_result);
    } else {
      throw std::runtime_error("FRAGMENTS requires a subquery or RAW('<...>') input");
    }
    HtmlDocument doc = build_fragments_document(fragments);
    effective_source_uri = "fragment";
    return execute_query_ast(query, doc, effective_source_uri);
  }
  HtmlDocument doc = parse_html(default_html);
  return execute_query_ast(query, doc, effective_source_uri);
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

std::shared_ptr<const ParsedDocumentHandle> prepare_document(const std::string& html,
                                                             const std::string& source_uri) {
  auto prepared = std::make_shared<ParsedDocumentHandle>();
  prepared->doc = parse_html(html);
  prepared->html = html;
  prepared->source_uri = source_uri.empty() ? "document" : source_uri;
  return prepared;
}

QueryResult execute_query_from_prepared_document(const std::shared_ptr<const ParsedDocumentHandle>& prepared,
                                                 const std::string& query) {
  if (prepared == nullptr) {
    throw std::runtime_error("Prepared document handle is null");
  }
  auto parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    throw std::runtime_error("Query parse error: " + parsed.error->message);
  }
  validate_query(*parsed.query);
  if (parsed.query->kind != Query::Kind::Select) {
    return execute_meta_query(*parsed.query, prepared->source_uri);
  }
  if (parsed.query->source.kind == Source::Kind::Document) {
    return execute_query_ast(*parsed.query, prepared->doc, prepared->source_uri);
  }
  return execute_query_with_source(*parsed.query, prepared->html, prepared->source_uri);
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
