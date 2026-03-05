#pragma once

#include "xsql/xsql.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../dom/html_parser.h"
#include "../../lang/markql_parser.h"

namespace xsql {

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

struct RelationRuntimeCache {
  std::optional<HtmlDocument> default_document;
  std::optional<std::vector<int64_t>> default_sibling_pos;
};

std::string lower_alias_name(const std::string& alias);
void merge_alias_columns(Relation& rel,
                         const std::string& alias,
                         const RelationRecord& record);
std::optional<int64_t> parse_optional_i64(const std::optional<std::string>& value);
void fill_result_core_from_record(QueryResultRow& out, const RelationRecord& record);
const RelationRecord* resolve_record(const RelationRow& row,
                                     const std::optional<std::string>& qualifier,
                                     const std::optional<std::string>& active_alias);
std::optional<std::string> relation_operand_value(const Operand& operand,
                                                  const RelationRow& row,
                                                  const std::optional<std::string>& active_alias);
std::optional<std::string> eval_relation_scalar_expr(const ScalarExpr& expr,
                                                     const RelationRow& row,
                                                     const std::optional<std::string>& active_alias);
std::optional<std::string> eval_relation_project_expr(
    const Query::SelectItem::FlattenExtractExpr& expr,
    const RelationRow& row,
    const std::optional<std::string>& active_alias,
    const std::unordered_map<std::string, std::string>& bindings);
bool eval_relation_expr(const Expr& expr,
                        const RelationRow& row,
                        const std::optional<std::string>& active_alias);
int compare_optional_relation_values(const std::optional<std::string>& left,
                                     const std::optional<std::string>& right);
std::optional<std::string> relation_field_by_name(const RelationRow& row,
                                                  const std::string& field,
                                                  const std::optional<std::string>& active_alias);
QueryResult query_result_from_relation(const Query& query, const Relation& relation);
bool merge_row_aliases(RelationRow& target,
                       const RelationRow& add,
                       std::string* duplicate_alias);
Relation execute_relation_join_non_lateral(const Query::JoinItem& join,
                                           const Relation& left_rel,
                                           const Relation& right_rel,
                                           const std::optional<std::string>& active_alias);

}  // namespace xsql
