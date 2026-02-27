#include "executor_internal.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <vector>

#include "../../util/string_util.h"
#include "../engine/xsql_internal.h"

namespace xsql::executor_internal {

struct EvalContext {
  const HtmlNode& current_row_node;
};

bool eval_expr_with_context(const Expr& expr,
                            const HtmlDocument& doc,
                            const std::vector<std::vector<int64_t>>& children,
                            const EvalContext& context);

namespace {

/// Splits a string on ASCII whitespace into tokens.
/// MUST ignore repeated whitespace and MUST preserve token order.
/// Inputs are strings; outputs are token vectors with no side effects.
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

/// Performs a case-insensitive substring match.
/// MUST use ASCII-only case folding for deterministic behavior.
/// Inputs are haystack/needle; outputs are boolean with no side effects.
bool contains_ci(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  std::string lower_haystack = util::to_lower(haystack);
  std::string lower_needle = util::to_lower(needle);
  return lower_haystack.find(lower_needle) != std::string::npos;
}

/// Performs a case-insensitive match for all tokens in the list.
/// MUST return true only when every token is present.
/// Inputs are haystack/tokens; outputs are boolean with no side effects.
bool contains_all_ci(const std::string& haystack, const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (!contains_ci(haystack, token)) return false;
  }
  return true;
}

/// Performs a case-insensitive match for any token in the list.
/// MUST return true when at least one token is present.
/// Inputs are haystack/tokens; outputs are boolean with no side effects.
bool contains_any_ci(const std::string& haystack, const std::vector<std::string>& tokens) {
  for (const auto& token : tokens) {
    if (contains_ci(haystack, token)) return true;
  }
  return false;
}

/// Parses a string as a strict int64 value.
/// MUST reject partial parses and MUST return nullopt on errors.
/// Inputs are strings; outputs are optional ints with no side effects.
std::optional<int64_t> parse_int64(const std::string& value) {
  try {
    size_t idx = 0;
    int64_t out = std::stoll(value, &idx);
    if (idx != value.size()) return std::nullopt;
    return out;
  } catch (...) {
    return std::nullopt;
  }
}

struct ScalarValue {
  enum class Kind { Null, String, Number } kind = Kind::Null;
  std::string string_value;
  int64_t number_value = 0;
};

ScalarValue make_null() { return ScalarValue{}; }

ScalarValue make_string(std::string value) {
  ScalarValue out;
  out.kind = ScalarValue::Kind::String;
  out.string_value = std::move(value);
  return out;
}

ScalarValue make_number(int64_t value) {
  ScalarValue out;
  out.kind = ScalarValue::Kind::Number;
  out.number_value = value;
  return out;
}

bool is_null(const ScalarValue& value) { return value.kind == ScalarValue::Kind::Null; }

std::string to_string_value(const ScalarValue& value) {
  if (value.kind == ScalarValue::Kind::Number) {
    return std::to_string(value.number_value);
  }
  return value.string_value;
}

std::optional<int64_t> to_int64_value(const ScalarValue& value) {
  if (value.kind == ScalarValue::Kind::Number) return value.number_value;
  if (value.kind == ScalarValue::Kind::String) return parse_int64(value.string_value);
  return std::nullopt;
}

bool values_equal(const ScalarValue& left, const ScalarValue& right) {
  if (is_null(left) || is_null(right)) return false;
  auto lnum = to_int64_value(left);
  auto rnum = to_int64_value(right);
  if (lnum.has_value() && rnum.has_value()) return *lnum == *rnum;
  return to_string_value(left) == to_string_value(right);
}

bool values_less(const ScalarValue& left, const ScalarValue& right) {
  if (is_null(left) || is_null(right)) return false;
  auto lnum = to_int64_value(left);
  auto rnum = to_int64_value(right);
  if (lnum.has_value() && rnum.has_value()) return *lnum < *rnum;
  return to_string_value(left) < to_string_value(right);
}

// WHY: a small iterative matcher avoids regex setup cost for frequent LIKE filters.
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

ScalarValue eval_scalar_expr_impl(const ScalarExpr& expr,
                                  const HtmlDocument& doc,
                                  const std::vector<std::vector<int64_t>>& children,
                                  const EvalContext& context);

int64_t sibling_pos_for_node(const HtmlDocument& doc,
                             const std::vector<std::vector<int64_t>>& children,
                             const HtmlNode& node) {
  if (!node.parent_id.has_value()) {
    return 1;
  }
  const auto& siblings = children.at(static_cast<size_t>(*node.parent_id));
  for (size_t i = 0; i < siblings.size(); ++i) {
    if (siblings[i] == node.id) {
      return static_cast<int64_t>(i + 1);
    }
  }
  return 1;
}

bool match_position_value(int64_t pos,
                          const std::vector<std::string>& values,
                          CompareExpr::Op op) {
  bool is_in = op == CompareExpr::Op::In;
  if (op == CompareExpr::Op::Regex) return false;
  if (is_in) {
    for (const auto& value : values) {
      auto parsed = parse_int64(value);
      if (parsed.has_value() && *parsed == pos) return true;
    }
    return false;
  }
  auto target = parse_int64(values.front());
  if (!target.has_value()) return false;
  if (op == CompareExpr::Op::NotEq) return pos != *target;
  if (op == CompareExpr::Op::Lt) return pos < *target;
  if (op == CompareExpr::Op::Lte) return pos <= *target;
  if (op == CompareExpr::Op::Gt) return pos > *target;
  if (op == CompareExpr::Op::Gte) return pos >= *target;
  return pos == *target;
}

bool match_sibling_pos(const HtmlDocument& doc,
                       const std::vector<std::vector<int64_t>>& children,
                       const HtmlNode& node,
                       const std::vector<std::string>& values,
                       CompareExpr::Op op) {
  int64_t pos = sibling_pos_for_node(doc, children, node);
  return match_position_value(pos, values, op);
}

/// Matches an attribute value against a list of candidates.
/// MUST treat class as whitespace-delimited tokens for membership checks.
/// Inputs are node/attr/values; outputs are boolean with no side effects.
bool match_attribute(const HtmlNode& node,
                     const std::string& attr,
                     const std::vector<std::string>& values,
                     bool is_in) {
  auto it = node.attributes.find(attr);
  if (it == node.attributes.end()) return false;

  std::string attr_value = it->second;
  // WHY: class is defined as a space-delimited token list in HTML.
  if (attr == "class") {
    auto tokens = split_ws(attr_value);
    for (const auto& token : tokens) {
      if (string_in_list(token, values)) return true;
    }
    return false;
  }

  if (is_in) {
    return string_in_list(attr_value, values);
  }
  return attr_value == values.front();
}

/// Matches a field (tag/text/attribute/node_id/parent_id) against values.
/// MUST honor operator semantics and MUST treat regex patterns as ECMAScript.
/// Inputs are node/field/values; outputs are boolean with no side effects.
bool match_field(const HtmlNode& node,
                 Operand::FieldKind field_kind,
                 const std::string& attr,
                 const std::vector<std::string>& values,
                 CompareExpr::Op op) {
  if ((op == CompareExpr::Op::Contains ||
       op == CompareExpr::Op::ContainsAll ||
       op == CompareExpr::Op::ContainsAny) &&
      field_kind != Operand::FieldKind::Attribute) {
    return false;
  }
  if (field_kind == Operand::FieldKind::SiblingPos) {
    return false;
  }
  bool is_in = op == CompareExpr::Op::In;
  if (field_kind == Operand::FieldKind::NodeId) {
    if (op == CompareExpr::Op::Regex) return false;
    auto target = parse_int64(values.front());
    if (!target.has_value()) return false;
    if (is_in) {
      for (const auto& value : values) {
        auto parsed = parse_int64(value);
        if (parsed.has_value() && *parsed == node.id) return true;
      }
      return false;
    }
    if (op == CompareExpr::Op::NotEq) return node.id != *target;
    if (op == CompareExpr::Op::Lt) return node.id < *target;
    if (op == CompareExpr::Op::Lte) return node.id <= *target;
    if (op == CompareExpr::Op::Gt) return node.id > *target;
    if (op == CompareExpr::Op::Gte) return node.id >= *target;
    return node.id == *target;
  }
  if (field_kind == Operand::FieldKind::ParentId) {
    if (!node.parent_id.has_value()) return false;
    if (op == CompareExpr::Op::Regex) return false;
    auto target = parse_int64(values.front());
    if (!target.has_value()) return false;
    if (is_in) {
      for (const auto& value : values) {
        auto parsed = parse_int64(value);
        if (parsed.has_value() && *parsed == *node.parent_id) return true;
      }
      return false;
    }
    if (op == CompareExpr::Op::NotEq) return *node.parent_id != *target;
    if (op == CompareExpr::Op::Lt) return *node.parent_id < *target;
    if (op == CompareExpr::Op::Lte) return *node.parent_id <= *target;
    if (op == CompareExpr::Op::Gt) return *node.parent_id > *target;
    if (op == CompareExpr::Op::Gte) return *node.parent_id >= *target;
    return *node.parent_id == *target;
  }
  if (field_kind == Operand::FieldKind::MaxDepth ||
      field_kind == Operand::FieldKind::DocOrder) {
    if (op == CompareExpr::Op::Regex) return false;
    auto target = parse_int64(values.front());
    if (!target.has_value()) return false;
    int64_t field_value = (field_kind == Operand::FieldKind::MaxDepth)
                              ? node.max_depth
                              : node.doc_order;
    if (is_in) {
      for (const auto& value : values) {
        auto parsed = parse_int64(value);
        if (parsed.has_value() && *parsed == field_value) return true;
      }
      return false;
    }
    if (op == CompareExpr::Op::NotEq) return field_value != *target;
    if (op == CompareExpr::Op::Lt) return field_value < *target;
    if (op == CompareExpr::Op::Lte) return field_value <= *target;
    if (op == CompareExpr::Op::Gt) return field_value > *target;
    if (op == CompareExpr::Op::Gte) return field_value >= *target;
    return field_value == *target;
  }
  if (field_kind == Operand::FieldKind::Attribute) {
    if (op == CompareExpr::Op::Contains) {
      auto it = node.attributes.find(attr);
      if (it == node.attributes.end()) return false;
      return contains_ci(it->second, values.front());
    }
    if (op == CompareExpr::Op::ContainsAll) {
      auto it = node.attributes.find(attr);
      if (it == node.attributes.end()) return false;
      return contains_all_ci(it->second, values);
    }
    if (op == CompareExpr::Op::ContainsAny) {
      auto it = node.attributes.find(attr);
      if (it == node.attributes.end()) return false;
      return contains_any_ci(it->second, values);
    }
    if (op == CompareExpr::Op::Regex) {
      auto it = node.attributes.find(attr);
      if (it == node.attributes.end()) return false;
      try {
        std::regex re(values.front(), std::regex::ECMAScript);
        return std::regex_search(it->second, re);
      } catch (const std::regex_error&) {
        return false;
      }
    }
    if (op == CompareExpr::Op::NotEq) {
      auto it = node.attributes.find(attr);
      if (it == node.attributes.end()) return false;
      if (attr == "class") {
        auto tokens = split_ws(it->second);
        for (const auto& token : tokens) {
          if (token == values.front()) return false;
        }
        return true;
      }
      return it->second != values.front();
    }
    if (op == CompareExpr::Op::Like) {
      auto it = node.attributes.find(attr);
      if (it == node.attributes.end()) return false;
      return like_match_ci(it->second, values.front());
    }
    return match_attribute(node, attr, values, is_in);
  }
  if (field_kind == Operand::FieldKind::Tag) {
    if (op == CompareExpr::Op::Contains ||
        op == CompareExpr::Op::ContainsAll ||
        op == CompareExpr::Op::ContainsAny) {
      return false;
    }
    if (op == CompareExpr::Op::Regex) {
      try {
        std::regex re(values.front(), std::regex::ECMAScript);
        return std::regex_search(node.tag, re);
      } catch (const std::regex_error&) {
        return false;
      }
    }
    if (is_in) {
      for (const auto& v : values) {
        if (node.tag == util::to_lower(v)) return true;
      }
      return false;
    }
    if (op == CompareExpr::Op::Like) return like_match_ci(node.tag, values.front());
    if (op == CompareExpr::Op::NotEq) return node.tag != util::to_lower(values.front());
    if (op == CompareExpr::Op::Lt) return node.tag < util::to_lower(values.front());
    if (op == CompareExpr::Op::Lte) return node.tag <= util::to_lower(values.front());
    if (op == CompareExpr::Op::Gt) return node.tag > util::to_lower(values.front());
    if (op == CompareExpr::Op::Gte) return node.tag >= util::to_lower(values.front());
    return node.tag == util::to_lower(values.front());
  }
  if (op == CompareExpr::Op::Regex) {
    try {
      std::regex re(values.front(), std::regex::ECMAScript);
      return std::regex_search(node.text, re);
    } catch (const std::regex_error&) {
      return false;
    }
  }
  if (is_in) return string_in_list(node.text, values);
  if (op == CompareExpr::Op::Like) return like_match_ci(node.text, values.front());
  if (op == CompareExpr::Op::NotEq) return node.text != values.front();
  if (op == CompareExpr::Op::Lt) return node.text < values.front();
  if (op == CompareExpr::Op::Lte) return node.text <= values.front();
  if (op == CompareExpr::Op::Gt) return node.text > values.front();
  if (op == CompareExpr::Op::Gte) return node.text >= values.front();
  return node.text == values.front();
}

/// Checks whether any direct child matches a node_id predicate.
/// MUST only inspect direct children and MUST return false on empty children.
/// Inputs are doc/children/node/values; outputs are boolean with no side effects.
bool has_child_node_id(const HtmlDocument& doc,
                       const std::vector<std::vector<int64_t>>& children,
                       const HtmlNode& node,
                       const std::vector<std::string>& values,
                       CompareExpr::Op op) {
  for (int64_t id : children.at(static_cast<size_t>(node.id))) {
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (match_field(child, Operand::FieldKind::NodeId, "", values, op)) return true;
  }
  return false;
}

/// Checks whether any descendant matches a node_id predicate.
/// MUST traverse the subtree and MUST return on first match for efficiency.
/// Inputs are doc/children/node/values; outputs are boolean with no side effects.
bool has_descendant_node_id(const HtmlDocument& doc,
                            const std::vector<std::vector<int64_t>>& children,
                            const HtmlNode& node,
                            const std::vector<std::string>& values,
                            CompareExpr::Op op) {
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (match_field(child, Operand::FieldKind::NodeId, "", values, op)) return true;
    const auto& next = children.at(static_cast<size_t>(id));
    stack.insert(stack.end(), next.begin(), next.end());
  }
  return false;
}

bool has_child_sibling_pos(const HtmlDocument& doc,
                           const std::vector<std::vector<int64_t>>& children,
                           const HtmlNode& node,
                           const std::vector<std::string>& values,
                           CompareExpr::Op op) {
  const auto& kids = children.at(static_cast<size_t>(node.id));
  for (int64_t id : kids) {
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (match_sibling_pos(doc, children, child, values, op)) return true;
  }
  return false;
}

bool has_descendant_sibling_pos(const HtmlDocument& doc,
                                const std::vector<std::vector<int64_t>>& children,
                                const HtmlNode& node,
                                const std::vector<std::string>& values,
                                CompareExpr::Op op) {
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (match_sibling_pos(doc, children, child, values, op)) return true;
    const auto& next = children.at(static_cast<size_t>(id));
    stack.insert(stack.end(), next.begin(), next.end());
  }
  return false;
}

/// Tests whether the axis target has a parent_id field present.
/// MUST follow axis semantics for self/parent/child/ancestor/descendant.
/// Inputs are doc/children/node/axis; outputs are boolean with no side effects.
bool axis_has_parent_id(const HtmlDocument& doc,
                        const std::vector<std::vector<int64_t>>& children,
                        const HtmlNode& node,
                        Operand::Axis axis) {
  if (axis == Operand::Axis::Self) {
    return node.parent_id.has_value();
  }
  if (axis == Operand::Axis::Parent) {
    if (!node.parent_id.has_value()) return false;
    const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*node.parent_id));
    return parent.parent_id.has_value();
  }
  if (axis == Operand::Axis::Child) {
    for (int64_t id : children.at(static_cast<size_t>(node.id))) {
      const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
      if (child.parent_id.has_value()) return true;
    }
    return false;
  }
  if (axis == Operand::Axis::Ancestor) {
    const HtmlNode* current = &node;
    while (current->parent_id.has_value()) {
      const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*current->parent_id));
      if (parent.parent_id.has_value()) return true;
      current = &parent;
    }
    return false;
  }
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (child.parent_id.has_value()) return true;
    const auto& next = children.at(static_cast<size_t>(id));
    stack.insert(stack.end(), next.begin(), next.end());
  }
  return false;
}

/// Checks whether any descendant matches a field predicate.
/// MUST traverse depth-first and MUST short-circuit on match.
/// Inputs are doc/children/node/field; outputs are boolean with no side effects.
bool has_descendant_field(const HtmlDocument& doc,
                          const std::vector<std::vector<int64_t>>& children,
                          const HtmlNode& node,
                          Operand::FieldKind field_kind,
                          const std::string& attr,
                          const std::vector<std::string>& values,
                          CompareExpr::Op op) {
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (match_field(child, field_kind, attr, values, op)) return true;
    const auto& next = children.at(static_cast<size_t>(id));
    stack.insert(stack.end(), next.begin(), next.end());
  }
  return false;
}

/// Checks whether any descendant defines a given attribute.
/// MUST traverse the subtree and MUST short-circuit on first find.
/// Inputs are doc/children/node/attr; outputs are boolean with no side effects.
bool has_descendant_attribute_exists(const HtmlDocument& doc,
                                     const std::vector<std::vector<int64_t>>& children,
                                     const HtmlNode& node,
                                     const std::string& attr) {
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (child.attributes.find(attr) != child.attributes.end()) return true;
    const auto& next = children.at(static_cast<size_t>(id));
    stack.insert(stack.end(), next.begin(), next.end());
  }
  return false;
}

/// Checks whether any direct child matches a field predicate.
/// MUST only inspect immediate children and MUST short-circuit on match.
/// Inputs are doc/children/node/field; outputs are boolean with no side effects.
bool has_child_field(const HtmlDocument& doc,
                     const std::vector<std::vector<int64_t>>& children,
                     const HtmlNode& node,
                     Operand::FieldKind field_kind,
                     const std::string& attr,
                     const std::vector<std::string>& values,
                     CompareExpr::Op op) {
  for (int64_t id : children.at(static_cast<size_t>(node.id))) {
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (match_field(child, field_kind, attr, values, op)) return true;
  }
  return false;
}

/// Checks whether any direct child defines a given attribute.
/// MUST only inspect immediate children and MUST short-circuit on find.
/// Inputs are doc/children/node/attr; outputs are boolean with no side effects.
bool has_child_attribute_exists(const HtmlDocument& doc,
                                const std::vector<std::vector<int64_t>>& children,
                                const HtmlNode& node,
                                const std::string& attr) {
  for (int64_t id : children.at(static_cast<size_t>(node.id))) {
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (child.attributes.find(attr) != child.attributes.end()) return true;
  }
  return false;
}

/// Reports whether a node has any direct children.
/// MUST not traverse beyond the immediate child list.
/// Inputs are children map/node; outputs are boolean with no side effects.
bool has_child_any(const std::vector<std::vector<int64_t>>& children, const HtmlNode& node) {
  return !children.at(static_cast<size_t>(node.id)).empty();
}

/// Reports whether a node has any descendants.
/// MUST short-circuit on the first descendant for performance.
/// Inputs are children map/node; outputs are boolean with no side effects.
bool has_descendant_any(const std::vector<std::vector<int64_t>>& children, const HtmlNode& node) {
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    return true;
  }
  return false;
}

/// Tests whether an axis target defines a given attribute.
/// MUST follow axis semantics and short-circuit on matches.
/// Inputs are doc/children/node/axis/attr; outputs are boolean.
bool axis_has_attribute(const HtmlDocument& doc,
                        const std::vector<std::vector<int64_t>>& children,
                        const HtmlNode& node,
                        Operand::Axis axis,
                        const std::string& attr) {
  if (axis == Operand::Axis::Self) {
    return node.attributes.find(attr) != node.attributes.end();
  }
  if (axis == Operand::Axis::Parent) {
    if (!node.parent_id.has_value()) return false;
    const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*node.parent_id));
    return parent.attributes.find(attr) != parent.attributes.end();
  }
  if (axis == Operand::Axis::Child) {
    return has_child_attribute_exists(doc, children, node, attr);
  }
  if (axis == Operand::Axis::Ancestor) {
    const HtmlNode* current = &node;
    while (current->parent_id.has_value()) {
      const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*current->parent_id));
      if (parent.attributes.find(attr) != parent.attributes.end()) return true;
      current = &parent;
    }
    return false;
  }
  return has_descendant_attribute_exists(doc, children, node, attr);
}

/// Tests whether an axis target node exists at all.
/// MUST treat self as always existing and others per axis rules.
/// Inputs are doc/children/node/axis; outputs are boolean.
bool axis_has_any_node(const HtmlDocument& doc,
                       const std::vector<std::vector<int64_t>>& children,
                       const HtmlNode& node,
                       Operand::Axis axis) {
  if (axis == Operand::Axis::Self) return true;
  if (axis == Operand::Axis::Parent) return node.parent_id.has_value();
  if (axis == Operand::Axis::Child) return has_child_any(children, node);
  if (axis == Operand::Axis::Ancestor) {
    return node.parent_id.has_value();
  }
  return has_descendant_any(children, node);
}

std::vector<const HtmlNode*> axis_nodes(const HtmlDocument& doc,
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

ScalarValue value_from_operand(const Operand& operand,
                               const HtmlDocument& doc,
                               const std::vector<std::vector<int64_t>>& children,
                               const HtmlNode& node) {
  std::vector<const HtmlNode*> candidates = axis_nodes(doc, children, node, operand.axis);
  for (const HtmlNode* candidate : candidates) {
    if (candidate == nullptr) continue;
    switch (operand.field_kind) {
      case Operand::FieldKind::Attribute: {
        auto it = candidate->attributes.find(operand.attribute);
        if (it != candidate->attributes.end()) return make_string(it->second);
        break;
      }
      case Operand::FieldKind::Tag:
        return make_string(candidate->tag);
      case Operand::FieldKind::Text:
        return make_string(candidate->text);
      case Operand::FieldKind::NodeId:
        return make_number(candidate->id);
      case Operand::FieldKind::ParentId:
        if (candidate->parent_id.has_value()) return make_number(*candidate->parent_id);
        break;
      case Operand::FieldKind::SiblingPos:
        return make_number(sibling_pos_for_node(doc, children, *candidate));
      case Operand::FieldKind::MaxDepth:
        return make_number(candidate->max_depth);
      case Operand::FieldKind::DocOrder:
        return make_number(candidate->doc_order);
      case Operand::FieldKind::AttributesMap:
        break;
    }
  }
  return make_null();
}

ScalarValue eval_scalar_expr_impl(const ScalarExpr& expr,
                                  const HtmlDocument& doc,
                                  const std::vector<std::vector<int64_t>>& children,
                                  const EvalContext& context) {
  const HtmlNode& node = context.current_row_node;
  switch (expr.kind) {
    case ScalarExpr::Kind::NullLiteral:
      return make_null();
    case ScalarExpr::Kind::StringLiteral:
      return make_string(expr.string_value);
    case ScalarExpr::Kind::NumberLiteral:
      return make_number(expr.number_value);
    case ScalarExpr::Kind::Operand:
      return value_from_operand(expr.operand, doc, children, node);
    case ScalarExpr::Kind::SelfRef:
      return make_null();
    case ScalarExpr::Kind::FunctionCall:
      break;
  }

  std::string fn = util::to_upper(expr.function_name);
  if (fn == "TEXT" || fn == "DIRECT_TEXT" || fn == "INNER_HTML" ||
      fn == "RAW_INNER_HTML" || fn == "ATTR") {
    if ((fn == "TEXT" || fn == "DIRECT_TEXT") && expr.args.size() != 1) return make_null();
    if ((fn == "INNER_HTML" || fn == "RAW_INNER_HTML") &&
        (expr.args.empty() || expr.args.size() > 2)) {
      return make_null();
    }
    if (fn == "ATTR" && expr.args.size() != 2) return make_null();
    if (expr.args.empty()) return make_null();

    const HtmlNode* target = nullptr;
    const ScalarExpr& target_arg_expr = expr.args.front();
    if (target_arg_expr.kind == ScalarExpr::Kind::SelfRef) {
      target = &node;
    } else {
      ScalarValue target_value = eval_scalar_expr_impl(target_arg_expr, doc, children, context);
      if (is_null(target_value)) return make_null();
      std::string tag = util::to_lower(to_string_value(target_value));
      if (node.tag != tag) return make_null();
      target = &node;
    }
    if (target == nullptr) return make_null();

    if (fn == "TEXT") return make_string(target->text);
    if (fn == "DIRECT_TEXT") {
      return make_string(xsql_internal::extract_direct_text_strict(target->inner_html));
    }
    if (fn == "ATTR") {
      ScalarValue attr_value = eval_scalar_expr_impl(expr.args[1], doc, children, context);
      if (is_null(attr_value)) return make_null();
      std::string attr = util::to_lower(to_string_value(attr_value));
      auto it = target->attributes.find(attr);
      if (it == target->attributes.end()) return make_null();
      return make_string(it->second);
    }

    size_t depth = 1;
    bool has_depth = false;
    if (expr.args.size() == 2) {
      ScalarValue depth_value = eval_scalar_expr_impl(expr.args[1], doc, children, context);
      if (is_null(depth_value)) return make_null();
      auto parsed = to_int64_value(depth_value);
      if (!parsed.has_value() || *parsed < 0) return make_null();
      depth = static_cast<size_t>(*parsed);
      has_depth = true;
    }
    size_t effective_depth = has_depth ? depth : 1;
    std::string html = xsql_internal::limit_inner_html(target->inner_html, effective_depth);
    if (fn == "RAW_INNER_HTML") return make_string(html);
    return make_string(util::minify_html(html));
  }

  std::vector<ScalarValue> args;
  args.reserve(expr.args.size());
  for (const auto& arg : expr.args) {
    args.push_back(eval_scalar_expr_impl(arg, doc, children, context));
  }

  if (fn == "COALESCE") {
    for (const auto& value : args) {
      if (!is_null(value)) return value;
    }
    return make_null();
  }
  if (fn == "CONCAT") {
    std::string out;
    for (const auto& value : args) {
      if (is_null(value)) return make_null();
      out += to_string_value(value);
    }
    return make_string(out);
  }
  if (fn == "LOWER" || fn == "UPPER") {
    if (args.size() != 1 || is_null(args[0])) return make_null();
    if (fn == "LOWER") return make_string(util::to_lower(to_string_value(args[0])));
    return make_string(util::to_upper(to_string_value(args[0])));
  }
  if (fn == "TRIM" || fn == "LTRIM" || fn == "RTRIM") {
    if (args.size() != 1 || is_null(args[0])) return make_null();
    std::string value = to_string_value(args[0]);
    if (fn == "TRIM") return make_string(util::trim_ws(value));
    if (fn == "LTRIM") {
      size_t i = 0;
      while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i]))) ++i;
      return make_string(value.substr(i));
    }
    size_t end = value.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return make_string(value.substr(0, end));
  }
  if (fn == "REPLACE") {
    if (args.size() != 3 || is_null(args[0]) || is_null(args[1]) || is_null(args[2])) return make_null();
    std::string text = to_string_value(args[0]);
    std::string from = to_string_value(args[1]);
    std::string to = to_string_value(args[2]);
    if (from.empty()) return make_string(text);
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
      text.replace(pos, from.size(), to);
      pos += to.size();
    }
    return make_string(text);
  }
  if (fn == "LENGTH" || fn == "CHAR_LENGTH") {
    if (args.size() != 1 || is_null(args[0])) return make_null();
    return make_number(static_cast<int64_t>(to_string_value(args[0]).size()));
  }
  if (fn == "SUBSTRING" || fn == "SUBSTR") {
    if (args.size() < 2 || args.size() > 3 || is_null(args[0]) || is_null(args[1])) return make_null();
    std::string text = to_string_value(args[0]);
    auto start = to_int64_value(args[1]);
    if (!start.has_value()) return make_null();
    int64_t from = std::max<int64_t>(1, *start) - 1;
    if (static_cast<size_t>(from) >= text.size()) return make_string("");
    if (args.size() == 2 || is_null(args[2])) return make_string(text.substr(static_cast<size_t>(from)));
    auto len = to_int64_value(args[2]);
    if (!len.has_value() || *len <= 0) return make_string("");
    return make_string(text.substr(static_cast<size_t>(from), static_cast<size_t>(*len)));
  }
  if (fn == "POSITION") {
    if (args.size() != 2 || is_null(args[0]) || is_null(args[1])) return make_null();
    std::string needle = to_string_value(args[0]);
    std::string haystack = to_string_value(args[1]);
    size_t pos = haystack.find(needle);
    if (pos == std::string::npos) return make_number(0);
    return make_number(static_cast<int64_t>(pos + 1));
  }
  if (fn == "LOCATE") {
    if (args.size() < 2 || args.size() > 3 || is_null(args[0]) || is_null(args[1])) return make_null();
    std::string needle = to_string_value(args[0]);
    std::string haystack = to_string_value(args[1]);
    size_t start = 0;
    if (args.size() == 3 && !is_null(args[2])) {
      auto parsed = to_int64_value(args[2]);
      if (!parsed.has_value()) return make_null();
      if (*parsed > 1) start = static_cast<size_t>(*parsed - 1);
    }
    size_t pos = haystack.find(needle, start);
    if (pos == std::string::npos) return make_number(0);
    return make_number(static_cast<int64_t>(pos + 1));
  }
  return make_null();
}

bool eval_exists_with_context(const ExistsExpr& exists,
                              const HtmlDocument& doc,
                              const std::vector<std::vector<int64_t>>& children,
                              const EvalContext& context) {
  const HtmlNode& node = context.current_row_node;
  if (!exists.where.has_value()) {
    return axis_has_any_node(doc, children, node, exists.axis);
  }
  const Expr& filter = *exists.where;
  if (exists.axis == Operand::Axis::Self) {
    return eval_expr_with_context(filter, doc, children, EvalContext{node});
  }
  if (exists.axis == Operand::Axis::Parent) {
    if (!node.parent_id.has_value()) return false;
    const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*node.parent_id));
    return eval_expr_with_context(filter, doc, children, EvalContext{parent});
  }
  if (exists.axis == Operand::Axis::Child) {
    for (int64_t id : children.at(static_cast<size_t>(node.id))) {
      const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
      if (eval_expr_with_context(filter, doc, children, EvalContext{child})) return true;
    }
    return false;
  }
  if (exists.axis == Operand::Axis::Ancestor) {
    const HtmlNode* current = &node;
    while (current->parent_id.has_value()) {
      const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*current->parent_id));
      if (eval_expr_with_context(filter, doc, children, EvalContext{parent})) return true;
      current = &parent;
    }
    return false;
  }
  std::vector<int64_t> stack;
  stack.insert(stack.end(), children.at(static_cast<size_t>(node.id)).begin(),
               children.at(static_cast<size_t>(node.id)).end());
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& child = doc.nodes.at(static_cast<size_t>(id));
    if (eval_expr_with_context(filter, doc, children, EvalContext{child})) return true;
    const auto& next = children.at(static_cast<size_t>(id));
    stack.insert(stack.end(), next.begin(), next.end());
  }
  return false;
}

}  // namespace

/// Checks membership of a string in a list with exact match.
/// MUST be case-sensitive to preserve attribute semantics.
/// Inputs are value/list; outputs are boolean with no side effects.
bool string_in_list(const std::string& value, const std::vector<std::string>& list) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

/// Evaluates a boolean expression over the current node and document.
/// MUST be deterministic and MUST honor axis/field semantics.
/// Inputs are expr/doc/children/node; outputs are boolean with no side effects.
bool eval_expr_with_context(const Expr& expr,
                            const HtmlDocument& doc,
                            const std::vector<std::vector<int64_t>>& children,
                            const EvalContext& context) {
  const HtmlNode& node = context.current_row_node;
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    std::vector<std::string> values = cmp.rhs.values;
    bool is_in = cmp.op == CompareExpr::Op::In;
    bool lhs_is_operand = cmp.lhs_expr.has_value() && cmp.lhs_expr->kind == ScalarExpr::Kind::Operand;
    bool legacy_op = cmp.op == CompareExpr::Op::Eq ||
                     cmp.op == CompareExpr::Op::In ||
                     cmp.op == CompareExpr::Op::NotEq ||
                     cmp.op == CompareExpr::Op::Lt ||
                     cmp.op == CompareExpr::Op::Lte ||
                     cmp.op == CompareExpr::Op::Gt ||
                     cmp.op == CompareExpr::Op::Gte ||
                     cmp.op == CompareExpr::Op::Regex ||
                     cmp.op == CompareExpr::Op::Like ||
                     cmp.op == CompareExpr::Op::Contains ||
                     cmp.op == CompareExpr::Op::ContainsAll ||
                     cmp.op == CompareExpr::Op::ContainsAny ||
                     cmp.op == CompareExpr::Op::HasDirectText ||
                     cmp.op == CompareExpr::Op::IsNull ||
                     cmp.op == CompareExpr::Op::IsNotNull;
    bool can_use_legacy = lhs_is_operand && legacy_op &&
                          (cmp.op == CompareExpr::Op::IsNull ||
                           cmp.op == CompareExpr::Op::IsNotNull ||
                           cmp.op == CompareExpr::Op::HasDirectText ||
                           !values.empty());
    if (!can_use_legacy && cmp.lhs_expr.has_value()) {
      ScalarValue lhs_value = eval_scalar_expr_impl(*cmp.lhs_expr, doc, children, context);
      if (cmp.op == CompareExpr::Op::IsNull) return is_null(lhs_value);
      if (cmp.op == CompareExpr::Op::IsNotNull) return !is_null(lhs_value);
      if (cmp.op == CompareExpr::Op::In) {
        if (is_null(lhs_value)) return false;
        for (const auto& rhs_expr : cmp.rhs_expr_list) {
          ScalarValue rhs_value = eval_scalar_expr_impl(rhs_expr, doc, children, context);
          if (values_equal(lhs_value, rhs_value)) return true;
        }
        return false;
      }
      ScalarValue rhs_value = cmp.rhs_expr.has_value()
                                  ? eval_scalar_expr_impl(*cmp.rhs_expr, doc, children, context)
                                  : make_null();
      if (cmp.op == CompareExpr::Op::Eq) return values_equal(lhs_value, rhs_value);
      if (cmp.op == CompareExpr::Op::NotEq) return !values_equal(lhs_value, rhs_value);
      if (cmp.op == CompareExpr::Op::Lt) return values_less(lhs_value, rhs_value);
      if (cmp.op == CompareExpr::Op::Lte) return values_less(lhs_value, rhs_value) || values_equal(lhs_value, rhs_value);
      if (cmp.op == CompareExpr::Op::Gt) return values_less(rhs_value, lhs_value);
      if (cmp.op == CompareExpr::Op::Gte) return values_less(rhs_value, lhs_value) || values_equal(lhs_value, rhs_value);
      if (cmp.op == CompareExpr::Op::Like) {
        if (is_null(lhs_value) || is_null(rhs_value)) return false;
        return like_match_ci(to_string_value(lhs_value), to_string_value(rhs_value));
      }
      if (cmp.op == CompareExpr::Op::Regex) {
        if (is_null(lhs_value) || is_null(rhs_value)) return false;
        try {
          std::regex re(to_string_value(rhs_value), std::regex::ECMAScript);
          return std::regex_search(to_string_value(lhs_value), re);
        } catch (const std::regex_error&) {
          return false;
        }
      }
      if (cmp.op == CompareExpr::Op::Contains ||
          cmp.op == CompareExpr::Op::ContainsAll ||
          cmp.op == CompareExpr::Op::ContainsAny) {
        if (is_null(lhs_value)) return false;
        std::vector<std::string> rhs_values;
        for (const auto& rhs_expr : cmp.rhs_expr_list) {
          ScalarValue value = eval_scalar_expr_impl(rhs_expr, doc, children, context);
          if (is_null(value)) return false;
          rhs_values.push_back(to_string_value(value));
        }
        if (rhs_values.empty()) {
          if (!cmp.rhs_expr.has_value()) return false;
          ScalarValue value = eval_scalar_expr_impl(*cmp.rhs_expr, doc, children, context);
          if (is_null(value)) return false;
          rhs_values.push_back(to_string_value(value));
        }
        if (cmp.op == CompareExpr::Op::Contains) return contains_ci(to_string_value(lhs_value), rhs_values.front());
        if (cmp.op == CompareExpr::Op::ContainsAll) return contains_all_ci(to_string_value(lhs_value), rhs_values);
        return contains_any_ci(to_string_value(lhs_value), rhs_values);
      }
    }

    if (cmp.op == CompareExpr::Op::HasDirectText) {
      if (node.tag != cmp.lhs.attribute) return false;
      std::string direct = xsql_internal::extract_direct_text(node.inner_html);
      return contains_ci(direct, values.front());
    }
    if (cmp.op == CompareExpr::Op::IsNull || cmp.op == CompareExpr::Op::IsNotNull) {
      bool exists = false;
      if (cmp.lhs.field_kind == Operand::FieldKind::AttributesMap) {
        exists = !node.attributes.empty();
      } else if (cmp.lhs.field_kind == Operand::FieldKind::Attribute) {
        exists = axis_has_attribute(doc, children, node, cmp.lhs.axis, cmp.lhs.attribute);
      } else if (cmp.lhs.field_kind == Operand::FieldKind::NodeId) {
        exists = axis_has_any_node(doc, children, node, cmp.lhs.axis);
      } else if (cmp.lhs.field_kind == Operand::FieldKind::ParentId) {
        exists = axis_has_parent_id(doc, children, node, cmp.lhs.axis);
      } else {
        exists = axis_has_any_node(doc, children, node, cmp.lhs.axis);
      }
      return (cmp.op == CompareExpr::Op::IsNull) ? !exists : exists;
    }
    if (cmp.lhs.axis == Operand::Axis::Parent) {
      if (!node.parent_id.has_value()) return false;
      const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*node.parent_id));
      if (cmp.lhs.field_kind == Operand::FieldKind::NodeId) {
        return match_field(parent, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op);
      }
      if (cmp.lhs.field_kind == Operand::FieldKind::SiblingPos) {
        return match_sibling_pos(doc, children, parent, values, cmp.op);
      }
      return match_field(parent, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op);
    }
    if (cmp.lhs.axis == Operand::Axis::Child) {
      if (cmp.lhs.field_kind == Operand::FieldKind::NodeId) {
        return has_child_node_id(doc, children, node, values, cmp.op);
      }
      if (cmp.lhs.field_kind == Operand::FieldKind::SiblingPos) {
        return has_child_sibling_pos(doc, children, node, values, cmp.op);
      }
      return has_child_field(doc, children, node, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op);
    }
    if (cmp.lhs.axis == Operand::Axis::Ancestor) {
      const HtmlNode* current = &node;
      while (current->parent_id.has_value()) {
        const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*current->parent_id));
        if (cmp.lhs.field_kind == Operand::FieldKind::NodeId) {
          if (match_field(parent, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op)) return true;
        } else if (cmp.lhs.field_kind == Operand::FieldKind::SiblingPos) {
          if (match_sibling_pos(doc, children, parent, values, cmp.op)) return true;
        } else {
          if (match_field(parent, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op)) return true;
        }
        current = &parent;
      }
      return false;
    }
    if (cmp.lhs.axis == Operand::Axis::Descendant) {
      if (cmp.lhs.field_kind == Operand::FieldKind::NodeId) {
        return has_descendant_node_id(doc, children, node, values, cmp.op);
      }
      if (cmp.lhs.field_kind == Operand::FieldKind::SiblingPos) {
        return has_descendant_sibling_pos(doc, children, node, values, cmp.op);
      }
      return has_descendant_field(doc, children, node, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op);
    }
    if (cmp.lhs.field_kind == Operand::FieldKind::SiblingPos) {
      return match_sibling_pos(doc, children, node, values, cmp.op);
    }
    return match_field(node, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op);
  }

  if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
    const auto& exists = *std::get<std::shared_ptr<ExistsExpr>>(expr);
    return eval_exists_with_context(exists, doc, children, context);
  }
  const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  bool left = eval_expr_with_context(bin.left, doc, children, context);
  bool right = eval_expr_with_context(bin.right, doc, children, context);
  if (bin.op == BinaryExpr::Op::And) return left && right;
  return left || right;
}

bool eval_expr(const Expr& expr,
               const HtmlDocument& doc,
               const std::vector<std::vector<int64_t>>& children,
               const HtmlNode& node) {
  return eval_expr_with_context(expr, doc, children, EvalContext{node});
}

/// Evaluates a boolean expression for FLATTEN_TEXT base node selection.
/// MUST ignore descendant.tag filters so they only affect flattening.
bool eval_expr_flatten_base(const Expr& expr,
                            const HtmlDocument& doc,
                            const std::vector<std::vector<int64_t>>& children,
                            const HtmlNode& node) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    if (cmp.lhs.axis == Operand::Axis::Descendant) {
      return true;
    }
    return eval_expr_with_context(expr, doc, children, EvalContext{node});
  }

  if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
    const auto& exists = *std::get<std::shared_ptr<ExistsExpr>>(expr);
    return eval_exists_with_context(exists, doc, children, EvalContext{node});
  }
  const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  bool left = eval_expr_flatten_base(bin.left, doc, children, node);
  bool right = eval_expr_flatten_base(bin.right, doc, children, node);
  if (bin.op == BinaryExpr::Op::And) return left && right;
  return left || right;
}

}  // namespace xsql::executor_internal
