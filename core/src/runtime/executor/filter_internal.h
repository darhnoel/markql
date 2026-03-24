#pragma once

#include "executor_internal.h"

#include <optional>

namespace markql::executor_internal {

struct EvalContext {
  const HtmlNode& current_row_node;
};

struct ScalarValue {
  enum class Kind { Null, String, Number } kind = Kind::Null;
  std::string string_value;
  int64_t number_value = 0;
};

bool contains_ci(const std::string& haystack, const std::string& needle);
bool contains_all_ci(const std::string& haystack, const std::vector<std::string>& tokens);
bool contains_any_ci(const std::string& haystack, const std::vector<std::string>& tokens);
ScalarValue make_null();
bool is_null(const ScalarValue& value);
std::string to_string_value(const ScalarValue& value);
bool values_equal(const ScalarValue& left, const ScalarValue& right);
bool values_less(const ScalarValue& left, const ScalarValue& right);
bool like_match_ci(const std::string& text, const std::string& pattern);
bool match_sibling_pos(const HtmlDocument& doc, const std::vector<std::vector<int64_t>>& children,
                       const HtmlNode& node, const std::vector<std::string>& values,
                       CompareExpr::Op op);
bool match_field(const HtmlNode& node, Operand::FieldKind field_kind, const std::string& attr,
                 const std::vector<std::string>& values, CompareExpr::Op op);
bool has_child_node_id(const HtmlDocument& doc, const std::vector<std::vector<int64_t>>& children,
                       const HtmlNode& node, const std::vector<std::string>& values,
                       CompareExpr::Op op);
bool has_descendant_node_id(const HtmlDocument& doc,
                            const std::vector<std::vector<int64_t>>& children, const HtmlNode& node,
                            const std::vector<std::string>& values, CompareExpr::Op op);
bool has_child_sibling_pos(const HtmlDocument& doc,
                           const std::vector<std::vector<int64_t>>& children, const HtmlNode& node,
                           const std::vector<std::string>& values, CompareExpr::Op op);
bool has_descendant_sibling_pos(const HtmlDocument& doc,
                                const std::vector<std::vector<int64_t>>& children,
                                const HtmlNode& node, const std::vector<std::string>& values,
                                CompareExpr::Op op);
bool axis_has_parent_id(const HtmlDocument& doc, const std::vector<std::vector<int64_t>>& children,
                        const HtmlNode& node, Operand::Axis axis);
bool has_descendant_field(const HtmlDocument& doc,
                          const std::vector<std::vector<int64_t>>& children, const HtmlNode& node,
                          Operand::FieldKind field_kind, const std::string& attr,
                          const std::vector<std::string>& values, CompareExpr::Op op);
bool has_child_field(const HtmlDocument& doc, const std::vector<std::vector<int64_t>>& children,
                     const HtmlNode& node, Operand::FieldKind field_kind, const std::string& attr,
                     const std::vector<std::string>& values, CompareExpr::Op op);
bool axis_has_attribute(const HtmlDocument& doc, const std::vector<std::vector<int64_t>>& children,
                        const HtmlNode& node, Operand::Axis axis, const std::string& attr);
bool axis_has_any_node(const HtmlDocument& doc, const std::vector<std::vector<int64_t>>& children,
                       const HtmlNode& node, Operand::Axis axis);
ScalarValue eval_scalar_expr_impl(const ScalarExpr& expr, const HtmlDocument& doc,
                                  const std::vector<std::vector<int64_t>>& children,
                                  const EvalContext& context);
bool eval_expr_with_context(const Expr& expr, const HtmlDocument& doc,
                            const std::vector<std::vector<int64_t>>& children,
                            const EvalContext& context);
bool eval_exists_with_context(const ExistsExpr& exists, const HtmlDocument& doc,
                              const std::vector<std::vector<int64_t>>& children,
                              const EvalContext& context);

}  // namespace markql::executor_internal
