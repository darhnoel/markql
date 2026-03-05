#include "xsql/xsql.h"

#include <memory>
#include <vector>

#include "../executor/executor_internal.h"
#include "../../lang/markql_parser.h"
#include "../../util/string_util.h"
#include "dom_descendants_internal.h"
#include "engine_execution_internal.h"

namespace xsql {

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

}  // namespace xsql
