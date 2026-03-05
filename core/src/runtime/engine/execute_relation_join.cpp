#include "xsql/xsql.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine_execution_internal.h"
#include "relation_runtime_internal.h"

namespace xsql {

namespace {

struct RelationJoinKeySpec {
  std::string alias;
  std::string column;
};

struct RelationHashJoinPlan {
  RelationJoinKeySpec left_key;
  RelationJoinKeySpec right_key;
};

std::optional<std::string> operand_join_column(const Operand& operand) {
  if (operand.axis != Operand::Axis::Self &&
      operand.axis != Operand::Axis::Parent) {
    return std::nullopt;
  }
  const std::string prefix = operand.axis == Operand::Axis::Parent ? "parent." : "";
  switch (operand.field_kind) {
    case Operand::FieldKind::Attribute:
      return prefix + operand.attribute;
    case Operand::FieldKind::Tag:
      return prefix + "tag";
    case Operand::FieldKind::Text:
      return prefix + "text";
    case Operand::FieldKind::NodeId:
      return prefix + "node_id";
    case Operand::FieldKind::ParentId:
      return prefix + "parent_id";
    case Operand::FieldKind::SiblingPos:
      return prefix + "sibling_pos";
    case Operand::FieldKind::MaxDepth:
      return prefix + "max_depth";
    case Operand::FieldKind::DocOrder:
      return prefix + "doc_order";
    case Operand::FieldKind::AttributesMap:
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<RelationJoinKeySpec> join_key_spec_from_scalar(const ScalarExpr& expr) {
  if (expr.kind != ScalarExpr::Kind::Operand) return std::nullopt;
  if (!expr.operand.qualifier.has_value()) return std::nullopt;
  std::optional<std::string> column = operand_join_column(expr.operand);
  if (!column.has_value()) return std::nullopt;
  return RelationJoinKeySpec{
      lower_alias_name(*expr.operand.qualifier),
      std::move(*column)};
}

bool relation_has_alias(const Relation& rel, const std::string& alias) {
  return rel.alias_columns.find(alias) != rel.alias_columns.end();
}

std::optional<RelationHashJoinPlan> plan_simple_hash_join(const Query::JoinItem& join,
                                                          const Relation& left_rel,
                                                          const Relation& right_rel) {
  if (join.type != Query::JoinItem::Type::Inner &&
      join.type != Query::JoinItem::Type::Left) {
    return std::nullopt;
  }
  if (!join.on.has_value() || !std::holds_alternative<CompareExpr>(*join.on)) {
    return std::nullopt;
  }
  const auto& cmp = std::get<CompareExpr>(*join.on);
  if (cmp.op != CompareExpr::Op::Eq ||
      !cmp.lhs_expr.has_value() ||
      !cmp.rhs_expr.has_value()) {
    return std::nullopt;
  }
  std::optional<RelationJoinKeySpec> lhs = join_key_spec_from_scalar(*cmp.lhs_expr);
  std::optional<RelationJoinKeySpec> rhs = join_key_spec_from_scalar(*cmp.rhs_expr);
  if (!lhs.has_value() || !rhs.has_value()) return std::nullopt;

  const bool lhs_in_left = relation_has_alias(left_rel, lhs->alias);
  const bool lhs_in_right = relation_has_alias(right_rel, lhs->alias);
  const bool rhs_in_left = relation_has_alias(left_rel, rhs->alias);
  const bool rhs_in_right = relation_has_alias(right_rel, rhs->alias);

  if (lhs_in_left && rhs_in_right && !lhs_in_right && !rhs_in_left) {
    return RelationHashJoinPlan{*lhs, *rhs};
  }
  if (rhs_in_left && lhs_in_right && !rhs_in_right && !lhs_in_left) {
    return RelationHashJoinPlan{*rhs, *lhs};
  }
  return std::nullopt;
}

std::optional<std::string> relation_row_key_value(const RelationRow& row,
                                                  const RelationJoinKeySpec& key) {
  auto alias_it = row.aliases.find(key.alias);
  if (alias_it == row.aliases.end()) return std::nullopt;
  auto value_it = alias_it->second.values.find(key.column);
  if (value_it == alias_it->second.values.end()) return std::nullopt;
  return value_it->second;
}

std::optional<std::string> normalize_join_key(const std::optional<std::string>& raw) {
  if (!raw.has_value()) return std::nullopt;
  if (auto parsed = parse_int64_value(*raw); parsed.has_value()) {
    return std::string("N:") + std::to_string(*parsed);
  }
  return std::string("S:") + *raw;
}

RelationRow build_left_join_padded_row(const RelationRow& left_row,
                                       const Relation& right_rel) {
  RelationRow padded = left_row;
  for (const auto& alias_cols : right_rel.alias_columns) {
    RelationRecord null_record;
    for (const auto& col : alias_cols.second) {
      null_record.values[col] = std::nullopt;
    }
    padded.aliases[alias_cols.first] = std::move(null_record);
  }
  return padded;
}

void merge_alias_columns_from_right(Relation& target, const Relation& right_rel) {
  for (const auto& alias_cols : right_rel.alias_columns) {
    target.alias_columns[alias_cols.first].insert(alias_cols.second.begin(),
                                                   alias_cols.second.end());
  }
}

}  // namespace

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

Relation execute_relation_join_non_lateral(const Query::JoinItem& join,
                                           const Relation& left_rel,
                                           const Relation& right_rel,
                                           const std::optional<std::string>& active_alias) {
  Relation next;
  next.alias_columns = left_rel.alias_columns;
  merge_alias_columns_from_right(next, right_rel);

  bool used_hash_join = false;
  if (auto plan = plan_simple_hash_join(join, left_rel, right_rel); plan.has_value()) {
    used_hash_join = true;
    std::unordered_map<std::string, std::vector<size_t>> right_index;
    right_index.reserve(right_rel.rows.size());
    for (size_t i = 0; i < right_rel.rows.size(); ++i) {
      std::optional<std::string> key =
          normalize_join_key(relation_row_key_value(right_rel.rows[i], plan->right_key));
      if (!key.has_value()) continue;
      right_index[*key].push_back(i);
    }
    for (const auto& left_row : left_rel.rows) {
      bool matched = false;
      std::optional<std::string> left_key =
          normalize_join_key(relation_row_key_value(left_row, plan->left_key));
      if (left_key.has_value()) {
        auto it = right_index.find(*left_key);
        if (it != right_index.end()) {
          for (size_t right_idx : it->second) {
            RelationRow merged = left_row;
            std::string duplicate;
            if (!merge_row_aliases(merged, right_rel.rows[right_idx], &duplicate)) {
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
        }
      }
      if (join.type == Query::JoinItem::Type::Left && !matched) {
        next.rows.push_back(build_left_join_padded_row(left_row, right_rel));
      }
    }
  }

  if (!used_hash_join) {
    for (const auto& left_row : left_rel.rows) {
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
        next.rows.push_back(build_left_join_padded_row(left_row, right_rel));
      }
    }
  }

  return next;
}

}  // namespace xsql
