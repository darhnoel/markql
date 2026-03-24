#include "filter_internal.h"

#include <algorithm>
#include <regex>

#include "../engine/markql_internal.h"

namespace markql::executor_internal {

bool string_in_list(const std::string& value, const std::vector<std::string>& list) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

/// Evaluates a boolean expression over the current node and document.
/// MUST be deterministic and MUST honor axis/field semantics.
/// Inputs are expr/doc/children/node; outputs are boolean with no side effects.
bool eval_expr_with_context(const Expr& expr, const HtmlDocument& doc,
                            const std::vector<std::vector<int64_t>>& children,
                            const EvalContext& context) {
  const HtmlNode& node = context.current_row_node;
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    std::vector<std::string> values = cmp.rhs.values;
    const bool lhs_is_operand =
        cmp.lhs_expr.has_value() && cmp.lhs_expr->kind == ScalarExpr::Kind::Operand;
    const bool legacy_op =
        cmp.op == CompareExpr::Op::Eq || cmp.op == CompareExpr::Op::In ||
        cmp.op == CompareExpr::Op::NotEq || cmp.op == CompareExpr::Op::Lt ||
        cmp.op == CompareExpr::Op::Lte || cmp.op == CompareExpr::Op::Gt ||
        cmp.op == CompareExpr::Op::Gte || cmp.op == CompareExpr::Op::Regex ||
        cmp.op == CompareExpr::Op::Like || cmp.op == CompareExpr::Op::Contains ||
        cmp.op == CompareExpr::Op::ContainsAll || cmp.op == CompareExpr::Op::ContainsAny ||
        cmp.op == CompareExpr::Op::HasDirectText || cmp.op == CompareExpr::Op::IsNull ||
        cmp.op == CompareExpr::Op::IsNotNull;
    const bool can_use_legacy =
        lhs_is_operand && legacy_op &&
        (cmp.op == CompareExpr::Op::IsNull || cmp.op == CompareExpr::Op::IsNotNull ||
         cmp.op == CompareExpr::Op::HasDirectText || !values.empty());
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
      if (cmp.op == CompareExpr::Op::Lte) {
        return values_less(lhs_value, rhs_value) || values_equal(lhs_value, rhs_value);
      }
      if (cmp.op == CompareExpr::Op::Gt) return values_less(rhs_value, lhs_value);
      if (cmp.op == CompareExpr::Op::Gte) {
        return values_less(rhs_value, lhs_value) || values_equal(lhs_value, rhs_value);
      }
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
      if (cmp.op == CompareExpr::Op::Contains || cmp.op == CompareExpr::Op::ContainsAll ||
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
        if (cmp.op == CompareExpr::Op::Contains) {
          return contains_ci(to_string_value(lhs_value), rhs_values.front());
        }
        if (cmp.op == CompareExpr::Op::ContainsAll) {
          return contains_all_ci(to_string_value(lhs_value), rhs_values);
        }
        return contains_any_ci(to_string_value(lhs_value), rhs_values);
      }
    }

    if (cmp.op == CompareExpr::Op::HasDirectText) {
      if (node.tag != cmp.lhs.attribute) return false;
      std::string direct = markql_internal::extract_direct_text(node.inner_html);
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
      return has_child_field(doc, children, node, cmp.lhs.field_kind, cmp.lhs.attribute, values,
                             cmp.op);
    }
    if (cmp.lhs.axis == Operand::Axis::Ancestor) {
      const HtmlNode* current = &node;
      while (current->parent_id.has_value()) {
        const HtmlNode& parent = doc.nodes.at(static_cast<size_t>(*current->parent_id));
        if (cmp.lhs.field_kind == Operand::FieldKind::NodeId) {
          if (match_field(parent, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op)) {
            return true;
          }
        } else if (cmp.lhs.field_kind == Operand::FieldKind::SiblingPos) {
          if (match_sibling_pos(doc, children, parent, values, cmp.op)) return true;
        } else {
          if (match_field(parent, cmp.lhs.field_kind, cmp.lhs.attribute, values, cmp.op)) {
            return true;
          }
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
      return has_descendant_field(doc, children, node, cmp.lhs.field_kind, cmp.lhs.attribute,
                                  values, cmp.op);
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

bool eval_expr(const Expr& expr, const HtmlDocument& doc,
               const std::vector<std::vector<int64_t>>& children, const HtmlNode& node) {
  return eval_expr_with_context(expr, doc, children, EvalContext{node});
}

/// Evaluates a boolean expression for FLATTEN_TEXT base node selection.
/// MUST ignore descendant.tag filters so they only affect flattening.
bool eval_expr_flatten_base(const Expr& expr, const HtmlDocument& doc,
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

}  // namespace markql::executor_internal
