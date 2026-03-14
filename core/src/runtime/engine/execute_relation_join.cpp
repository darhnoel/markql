#include "markql/markql.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "engine_execution_internal.h"
#include "relation_runtime_internal.h"

namespace markql {

namespace {

struct RelationJoinKeySpec {
  std::string alias;
  std::string column;
};

struct RelationHashJoinPlan {
  RelationJoinKeySpec left_key;
  RelationJoinKeySpec right_key;
};

enum class RelationJoinStrategy {
  NestedLoop,
  HashEqui,
  IndexedLookupNested,
};

struct RelationJoinExecutionPlan {
  RelationJoinStrategy strategy = RelationJoinStrategy::NestedLoop;
  std::optional<RelationHashJoinPlan> hash_plan;
};

struct RelationIndexLookupTerm {
  enum class Kind {
    LeftColumn,
    Literal
  } kind = Kind::LeftColumn;
  std::string right_column;
  RelationJoinKeySpec left_key;
  std::optional<std::string> literal_value;
  std::optional<std::string> normalized_literal_key;
};

struct RelationIndexLookupPlan {
  std::vector<RelationIndexLookupTerm> terms;
  bool full_on_covered = false;
};

std::optional<std::string> normalize_join_key(const std::optional<std::string>& raw);

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

RelationJoinExecutionPlan select_join_strategy(const Query::JoinItem& join,
                                               const Relation& left_rel,
                                               const Relation& right_rel) {
  RelationJoinExecutionPlan plan;
  if (auto hash = plan_simple_hash_join(join, left_rel, right_rel); hash.has_value()) {
    plan.strategy = RelationJoinStrategy::HashEqui;
    plan.hash_plan = std::move(hash);
  }
  return plan;
}

bool append_compare_conjuncts(const Expr& expr, std::vector<const CompareExpr*>& out) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    out.push_back(&std::get<CompareExpr>(expr));
    return true;
  }
  if (!std::holds_alternative<std::shared_ptr<BinaryExpr>>(expr)) return false;
  const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  if (bin.op != BinaryExpr::Op::And) return false;
  return append_compare_conjuncts(bin.left, out) &&
         append_compare_conjuncts(bin.right, out);
}

std::optional<ScalarExpr> compare_lhs_scalar(const CompareExpr& cmp) {
  if (cmp.lhs_expr.has_value()) return cmp.lhs_expr;
  ScalarExpr expr;
  expr.kind = ScalarExpr::Kind::Operand;
  expr.operand = cmp.lhs;
  return expr;
}

std::optional<ScalarExpr> compare_rhs_scalar(const CompareExpr& cmp) {
  if (cmp.rhs_expr.has_value()) return cmp.rhs_expr;
  if (cmp.rhs.values.size() != 1) return std::nullopt;
  ScalarExpr expr;
  expr.kind = ScalarExpr::Kind::StringLiteral;
  expr.string_value = cmp.rhs.values.front();
  return expr;
}

bool scalar_literal_value(const ScalarExpr& expr, std::optional<std::string>* out) {
  if (expr.kind == ScalarExpr::Kind::StringLiteral) {
    *out = expr.string_value;
    return true;
  }
  if (expr.kind == ScalarExpr::Kind::NumberLiteral) {
    *out = std::to_string(expr.number_value);
    return true;
  }
  if (expr.kind == ScalarExpr::Kind::NullLiteral) {
    *out = std::nullopt;
    return true;
  }
  return false;
}

bool alias_is_only_in_relation(const std::string& alias,
                               const Relation& yes_rel,
                               const Relation& no_rel) {
  return relation_has_alias(yes_rel, alias) && !relation_has_alias(no_rel, alias);
}

std::optional<RelationIndexLookupTerm> lookup_term_from_compare(
    const CompareExpr& cmp,
    const Relation& left_rel,
    const Relation& right_rel) {
  if (cmp.op != CompareExpr::Op::Eq) return std::nullopt;
  std::optional<ScalarExpr> lhs = compare_lhs_scalar(cmp);
  std::optional<ScalarExpr> rhs = compare_rhs_scalar(cmp);
  if (!lhs.has_value() || !rhs.has_value()) return std::nullopt;

  std::optional<RelationJoinKeySpec> lhs_key = join_key_spec_from_scalar(*lhs);
  std::optional<RelationJoinKeySpec> rhs_key = join_key_spec_from_scalar(*rhs);

  if (lhs_key.has_value() && rhs_key.has_value()) {
    if (alias_is_only_in_relation(lhs_key->alias, right_rel, left_rel) &&
        alias_is_only_in_relation(rhs_key->alias, left_rel, right_rel)) {
      RelationIndexLookupTerm term;
      term.kind = RelationIndexLookupTerm::Kind::LeftColumn;
      term.right_column = lhs_key->column;
      term.left_key = *rhs_key;
      return term;
    }
    if (alias_is_only_in_relation(rhs_key->alias, right_rel, left_rel) &&
        alias_is_only_in_relation(lhs_key->alias, left_rel, right_rel)) {
      RelationIndexLookupTerm term;
      term.kind = RelationIndexLookupTerm::Kind::LeftColumn;
      term.right_column = rhs_key->column;
      term.left_key = *lhs_key;
      return term;
    }
    return std::nullopt;
  }

  if (lhs_key.has_value() &&
      alias_is_only_in_relation(lhs_key->alias, right_rel, left_rel)) {
    std::optional<std::string> literal;
    if (!scalar_literal_value(*rhs, &literal)) return std::nullopt;
    RelationIndexLookupTerm term;
    term.kind = RelationIndexLookupTerm::Kind::Literal;
    term.right_column = lhs_key->column;
    term.literal_value = std::move(literal);
    return term;
  }
  if (rhs_key.has_value() &&
      alias_is_only_in_relation(rhs_key->alias, right_rel, left_rel)) {
    std::optional<std::string> literal;
    if (!scalar_literal_value(*lhs, &literal)) return std::nullopt;
    RelationIndexLookupTerm term;
    term.kind = RelationIndexLookupTerm::Kind::Literal;
    term.right_column = rhs_key->column;
    term.literal_value = std::move(literal);
    return term;
  }
  return std::nullopt;
}

std::optional<RelationIndexLookupPlan> plan_index_lookup_join(const Query::JoinItem& join,
                                                              const Relation& left_rel,
                                                              const Relation& right_rel) {
  if (!join.on.has_value()) return std::nullopt;
  if (join.type != Query::JoinItem::Type::Inner &&
      join.type != Query::JoinItem::Type::Left) {
    return std::nullopt;
  }
  if (right_rel.alias_columns.size() != 1) return std::nullopt;

  std::vector<const CompareExpr*> conjuncts;
  if (!append_compare_conjuncts(*join.on, conjuncts)) return std::nullopt;

  RelationIndexLookupPlan plan;
  size_t recognized_terms = 0;
  std::unordered_set<std::string> seen_columns;
  for (const CompareExpr* cmp : conjuncts) {
    std::optional<RelationIndexLookupTerm> term =
        lookup_term_from_compare(*cmp, left_rel, right_rel);
    if (!term.has_value()) continue;
    ++recognized_terms;
    if (seen_columns.find(term->right_column) != seen_columns.end()) {
      return std::nullopt;
    }
    seen_columns.insert(term->right_column);
    plan.terms.push_back(std::move(*term));
    if (plan.terms.size() == 2) break;
  }
  if (plan.terms.empty()) return std::nullopt;
  std::sort(plan.terms.begin(), plan.terms.end(),
            [](const RelationIndexLookupTerm& a, const RelationIndexLookupTerm& b) {
              return a.right_column < b.right_column;
            });
  for (auto& term : plan.terms) {
    if (term.kind == RelationIndexLookupTerm::Kind::Literal) {
      term.normalized_literal_key = normalize_join_key(term.literal_value);
    }
  }
  plan.full_on_covered =
      (recognized_terms == conjuncts.size() && plan.terms.size() == conjuncts.size());
  return plan;
}

std::optional<std::string> relation_row_key_value(const RelationRow& row,
                                                  const RelationJoinKeySpec& key) {
  auto alias_it = row.aliases.find(key.alias);
  if (alias_it == row.aliases.end()) return std::nullopt;
  auto value_it = alias_it->second.values.find(key.column);
  if (value_it == alias_it->second.values.end()) return std::nullopt;
  return value_it->second;
}

std::optional<std::string> single_alias_relation_row_value(const RelationRow& row,
                                                           const std::string& column) {
  if (row.aliases.size() != 1) return std::nullopt;
  const auto& record = row.aliases.begin()->second;
  auto value_it = record.values.find(column);
  if (value_it == record.values.end()) return std::nullopt;
  return value_it->second;
}

std::optional<std::string> normalize_join_key(const std::optional<std::string>& raw) {
  if (!raw.has_value()) return std::nullopt;
  if (auto parsed = parse_int64_value(*raw); parsed.has_value()) {
    return std::string("N:") + std::to_string(*parsed);
  }
  return std::string("S:") + *raw;
}

std::string composite_lookup_key(const std::vector<std::string>& parts) {
  std::string out;
  out.reserve(parts.size() * 16);
  for (const auto& part : parts) {
    out += std::to_string(part.size());
    out.push_back(':');
    out += part;
    out.push_back('|');
  }
  return out;
}

std::optional<std::string> relation_index_signature(const Relation& right_rel,
                                                    const RelationIndexLookupPlan& plan) {
  if (right_rel.cache_key.empty()) return std::nullopt;
  std::string signature = right_rel.cache_key;
  signature.push_back('|');
  for (const auto& term : plan.terms) {
    signature += term.right_column;
    signature.push_back('|');
  }
  return signature;
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

const char* join_strategy_name(RelationJoinStrategy strategy) {
  if (strategy == RelationJoinStrategy::HashEqui) return "hash_equi";
  if (strategy == RelationJoinStrategy::IndexedLookupNested) return "indexed_lookup_nested";
  return "nested_loop";
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
                                           const std::optional<std::string>& active_alias,
                                           const std::string& join_label,
                                           RelationRuntimeCache* cache) {
  RelationRuntimeCache::Profile* profile = cache != nullptr ? &cache->profile : nullptr;
  const bool profiling_enabled = profile != nullptr && profile->enabled;
  const auto started_at = profiling_enabled ? std::chrono::steady_clock::now()
                                            : std::chrono::steady_clock::time_point{};

  Relation next;
  next.alias_columns = left_rel.alias_columns;
  merge_alias_columns_from_right(next, right_rel);
  next.cache_key = left_rel.cache_key;

  uint64_t pairs_evaluated = 0;
  RelationJoinExecutionPlan plan = select_join_strategy(join, left_rel, right_rel);
  if (plan.strategy == RelationJoinStrategy::HashEqui && plan.hash_plan.has_value()) {
    std::unordered_map<std::string, std::vector<size_t>> right_index;
    right_index.reserve(right_rel.rows.size());
    for (size_t i = 0; i < right_rel.rows.size(); ++i) {
      std::optional<std::string> key =
          normalize_join_key(relation_row_key_value(right_rel.rows[i], plan.hash_plan->right_key));
      if (!key.has_value()) continue;
      right_index[*key].push_back(i);
    }
    for (const auto& left_row : left_rel.rows) {
      bool matched = false;
      std::optional<std::string> left_key =
          normalize_join_key(relation_row_key_value(left_row, plan.hash_plan->left_key));
      if (left_key.has_value()) {
        auto it = right_index.find(*left_key);
        if (it != right_index.end()) {
          for (size_t right_idx : it->second) {
            ++pairs_evaluated;
            RelationRow merged = left_row;
            std::string duplicate;
            if (!merge_row_aliases(merged, right_rel.rows[right_idx], &duplicate)) {
              throw std::runtime_error("Duplicate source alias '" + duplicate + "' in FROM");
            }
            matched = true;
            next.rows.push_back(std::move(merged));
          }
        }
      }
      if (join.type == Query::JoinItem::Type::Left && !matched) {
        next.rows.push_back(build_left_join_padded_row(left_row, right_rel));
      }
    }
  } else {
    std::optional<RelationIndexLookupPlan> index_lookup =
        plan_index_lookup_join(join, left_rel, right_rel);
    if (index_lookup.has_value()) {
      plan.strategy = RelationJoinStrategy::IndexedLookupNested;
      const bool needs_full_on_eval = !index_lookup->full_on_covered;

      std::unordered_map<std::string, std::vector<size_t>> local_index;
      std::unordered_map<std::string, std::vector<size_t>>* right_index = nullptr;

      bool cache_hit = false;
      if (cache != nullptr) {
        std::optional<std::string> signature = relation_index_signature(right_rel, *index_lookup);
        if (signature.has_value()) {
          auto found = cache->relation_index_cache.find(*signature);
          if (found != cache->relation_index_cache.end()) {
            cache_hit = true;
            right_index = &found->second;
          } else {
            auto [it, _] = cache->relation_index_cache.emplace(
                *signature, std::unordered_map<std::string, std::vector<size_t>>{});
            right_index = &it->second;
          }
        }
      }
      if (right_index == nullptr) {
        right_index = &local_index;
      }
      if (!cache_hit) {
        right_index->reserve(right_rel.rows.size());
        for (size_t right_idx = 0; right_idx < right_rel.rows.size(); ++right_idx) {
          const RelationRow& right_row = right_rel.rows[right_idx];
          std::vector<std::string> parts;
          parts.reserve(index_lookup->terms.size());
          bool has_full_key = true;
          for (const auto& term : index_lookup->terms) {
            std::optional<std::string> normalized =
                normalize_join_key(single_alias_relation_row_value(right_row, term.right_column));
            if (!normalized.has_value()) {
              has_full_key = false;
              break;
            }
            parts.push_back(std::move(*normalized));
          }
          if (!has_full_key) continue;
          (*right_index)[composite_lookup_key(parts)].push_back(right_idx);
        }
      }
      if (profiling_enabled) {
        if (cache_hit) {
          ++profile->relation_index_hits;
        } else {
          ++profile->relation_index_builds;
        }
      }

      for (const auto& left_row : left_rel.rows) {
        bool matched = false;
        std::vector<std::string> lookup_parts;
        lookup_parts.reserve(index_lookup->terms.size());
        bool has_lookup_key = true;
        for (const auto& term : index_lookup->terms) {
          std::optional<std::string> normalized;
          if (term.kind == RelationIndexLookupTerm::Kind::LeftColumn) {
            normalized = normalize_join_key(relation_row_key_value(left_row, term.left_key));
          } else {
            normalized = term.normalized_literal_key;
          }
          if (!normalized.has_value()) {
            has_lookup_key = false;
            break;
          }
          lookup_parts.push_back(std::move(*normalized));
        }
        if (has_lookup_key) {
          auto it = right_index->find(composite_lookup_key(lookup_parts));
          if (it != right_index->end()) {
            for (size_t right_idx : it->second) {
              ++pairs_evaluated;
              RelationRow merged = left_row;
              std::string duplicate;
              if (!merge_row_aliases(merged, right_rel.rows[right_idx], &duplicate)) {
                throw std::runtime_error("Duplicate source alias '" + duplicate + "' in FROM");
              }
              bool keep = true;
              if (needs_full_on_eval && join.type != Query::JoinItem::Type::Cross &&
                  join.on.has_value()) {
                keep = eval_relation_expr(*join.on, merged, active_alias, profile);
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
    } else {
      for (const auto& left_row : left_rel.rows) {
        bool matched = false;
        for (const auto& right_row : right_rel.rows) {
          ++pairs_evaluated;
          RelationRow merged = left_row;
          std::string duplicate;
          if (!merge_row_aliases(merged, right_row, &duplicate)) {
            throw std::runtime_error("Duplicate source alias '" + duplicate + "' in FROM");
          }
          bool keep = true;
          if (join.type != Query::JoinItem::Type::Cross && join.on.has_value()) {
            keep = eval_relation_expr(*join.on, merged, active_alias, profile);
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
  }

  if (profiling_enabled) {
    const auto finished_at = std::chrono::steady_clock::now();
    const uint64_t elapsed_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            finished_at - started_at).count());
    profile->join_time_ns += elapsed_ns;
    profile->joins.push_back(RelationRuntimeCache::JoinSample{
        join_label,
        join_strategy_name(plan.strategy),
        left_rel.rows.size(),
        right_rel.rows.size(),
        next.rows.size(),
        pairs_evaluated});
  }

  return next;
}

}  // namespace markql
