#include "xsql/xsql.h"

#include <chrono>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../executor/executor_internal.h"
#include "../../lang/markql_parser.h"
#include "../../util/string_util.h"
#include "relation_runtime_internal.h"
#include "engine_execution_internal.h"

namespace xsql {

namespace {

class ScopedScalarEvalTimer {
 public:
  explicit ScopedScalarEvalTimer(RelationRuntimeCache::Profile* profile)
      : profile_(profile) {
    if (profile_ == nullptr || !profile_->enabled) {
      profile_ = nullptr;
      return;
    }
    ++profile_->scalar_eval_active_depth;
    started_at_ = std::chrono::steady_clock::now();
  }

  ~ScopedScalarEvalTimer() {
    if (profile_ == nullptr) return;
    if (profile_->scalar_eval_active_depth > 0) {
      --profile_->scalar_eval_active_depth;
    }
    if (profile_->scalar_eval_active_depth != 0) return;
    const auto finished_at = std::chrono::steady_clock::now();
    const uint64_t elapsed_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            finished_at - started_at_).count());
    profile_->scalar_eval_time_ns += elapsed_ns;
  }

 private:
  RelationRuntimeCache::Profile* profile_ = nullptr;
  std::chrono::steady_clock::time_point started_at_{};
};

}  // namespace

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
                                                     const std::optional<std::string>& active_alias,
                                                     RelationRuntimeCache::Profile* profile) {
  ScopedScalarEvalTimer timer(profile);
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
    std::optional<std::string> target =
        eval_relation_scalar_expr(expr.args[0], row, active_alias, profile);
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
    std::optional<std::string> target =
        eval_relation_scalar_expr(expr.args[0], row, active_alias, profile);
    std::optional<std::string> attr =
        eval_relation_scalar_expr(expr.args[1], row, active_alias, profile);
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
      std::optional<std::string> value =
          eval_relation_scalar_expr(arg, row, active_alias, profile);
      if (!value.has_value()) continue;
      if (util::trim_ws(*value).empty()) continue;
      return value;
    }
    return std::nullopt;
  }
  if (fn == "LOWER" || fn == "UPPER" || fn == "TRIM" || fn == "LTRIM" || fn == "RTRIM") {
    if (expr.args.size() != 1) return std::nullopt;
    std::optional<std::string> value =
        eval_relation_scalar_expr(expr.args[0], row, active_alias, profile);
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
    std::optional<std::string> text =
        eval_relation_scalar_expr(expr.args[0], row, active_alias, profile);
    std::optional<std::string> from =
        eval_relation_scalar_expr(expr.args[1], row, active_alias, profile);
    std::optional<std::string> to =
        eval_relation_scalar_expr(expr.args[2], row, active_alias, profile);
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
                        const std::optional<std::string>& active_alias,
                        RelationRuntimeCache::Profile* profile);

std::optional<std::string> eval_relation_project_expr(
    const Query::SelectItem::FlattenExtractExpr& expr,
    const RelationRow& row,
    const std::optional<std::string>& active_alias,
    const std::unordered_map<std::string, std::string>& bindings,
    RelationRuntimeCache::Profile* profile) {
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
          eval_relation_project_expr(arg, row, active_alias, bindings, profile);
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
          eval_relation_project_expr(arg, row, active_alias, bindings, profile);
      ScalarExpr scalar_arg;
      if (!nested.has_value()) {
        scalar_arg.kind = ScalarExpr::Kind::NullLiteral;
      } else {
        scalar_arg.kind = ScalarExpr::Kind::StringLiteral;
        scalar_arg.string_value = *nested;
      }
      scalar_expr.args.push_back(std::move(scalar_arg));
    }
    return eval_relation_scalar_expr(scalar_expr, row, active_alias, profile);
  }
  if (expr.kind == Kind::CaseWhen) {
    for (size_t i = 0; i < expr.case_when_conditions.size() &&
                       i < expr.case_when_values.size(); ++i) {
      if (!eval_relation_expr(expr.case_when_conditions[i], row, active_alias, profile)) continue;
      return eval_relation_project_expr(expr.case_when_values[i], row, active_alias, bindings,
                                        profile);
    }
    if (expr.case_else != nullptr) {
      return eval_relation_project_expr(*expr.case_else, row, active_alias, bindings, profile);
    }
    return std::nullopt;
  }
  return std::nullopt;
}

bool eval_relation_expr(const Expr& expr,
                        const RelationRow& row,
                        const std::optional<std::string>& active_alias,
                        RelationRuntimeCache::Profile* profile) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    std::optional<std::string> lhs;
    if (cmp.lhs_expr.has_value()) {
      lhs = eval_relation_scalar_expr(*cmp.lhs_expr, row, active_alias, profile);
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
          std::optional<std::string> rhs =
              eval_relation_scalar_expr(rhs_expr, row, active_alias, profile);
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
      rhs = eval_relation_scalar_expr(*cmp.rhs_expr, row, active_alias, profile);
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
  bool left = eval_relation_expr(bin.left, row, active_alias, profile);
  bool right = eval_relation_expr(bin.right, row, active_alias, profile);
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

}  // namespace xsql
