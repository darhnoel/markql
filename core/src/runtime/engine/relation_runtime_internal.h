#pragma once

#include "markql/markql.h"

#include <optional>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../dom/html_parser.h"
#include "../../lang/markql_parser.h"

namespace markql {

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
  std::string cache_key;
};

struct SourceRowPrefilter {
  std::optional<int64_t> parent_id_eq;
  std::optional<std::string> tag_eq;
  bool impossible = false;
};

struct RelationRuntimeCache {
  struct CteSizeSample {
    std::string name;
    size_t rows = 0;
  };

  struct JoinSample {
    std::string label;
    std::string strategy;
    size_t left_rows = 0;
    size_t right_rows = 0;
    size_t output_rows = 0;
    uint64_t pairs_evaluated = 0;
  };

  struct Profile {
    bool enabled = false;
    uint64_t join_time_ns = 0;
    uint64_t projection_time_ns = 0;
    uint64_t scalar_eval_time_ns = 0;
    uint64_t scalar_eval_active_depth = 0;
    uint64_t relation_index_builds = 0;
    uint64_t relation_index_hits = 0;
    std::vector<CteSizeSample> cte_sizes;
    std::vector<JoinSample> joins;
  };

  std::optional<HtmlDocument> default_document;
  std::optional<std::vector<int64_t>> default_sibling_pos;
  Profile profile;
  std::unordered_map<std::string, std::unordered_map<std::string, std::vector<size_t>>>
      relation_index_cache;
  uint64_t next_relation_cache_id = 1;
};

std::string lower_alias_name(const std::string& alias);
void merge_alias_columns(Relation& rel, const std::string& alias, const RelationRecord& record);
std::optional<int64_t> parse_optional_i64(const std::optional<std::string>& value);
void fill_result_core_from_record(QueryResultRow& out, const RelationRecord& record);
const RelationRecord* resolve_record(const RelationRow& row,
                                     const std::optional<std::string>& qualifier,
                                     const std::optional<std::string>& active_alias);
std::optional<std::string> relation_operand_value(const Operand& operand, const RelationRow& row,
                                                  const std::optional<std::string>& active_alias);
std::optional<std::string> eval_relation_scalar_expr(
    const ScalarExpr& expr, const RelationRow& row, const std::optional<std::string>& active_alias,
    RelationRuntimeCache::Profile* profile = nullptr);
std::optional<std::string> eval_relation_project_expr(
    const Query::SelectItem::FlattenExtractExpr& expr, const RelationRow& row,
    const std::optional<std::string>& active_alias,
    const std::unordered_map<std::string, std::string>& bindings,
    RelationRuntimeCache::Profile* profile = nullptr);
bool eval_relation_expr(const Expr& expr, const RelationRow& row,
                        const std::optional<std::string>& active_alias,
                        RelationRuntimeCache::Profile* profile = nullptr);
int compare_optional_relation_values(const std::optional<std::string>& left,
                                     const std::optional<std::string>& right);
std::optional<std::string> relation_field_by_name(const RelationRow& row, const std::string& field,
                                                  const std::optional<std::string>& active_alias);
QueryResult query_result_from_relation(const Query& query, const Relation& relation,
                                       RelationRuntimeCache::Profile* profile);
bool merge_row_aliases(RelationRow& target, const RelationRow& add, std::string* duplicate_alias);
Relation execute_relation_join_non_lateral(const Query::JoinItem& join, const Relation& left_rel,
                                           const Relation& right_rel,
                                           const std::optional<std::string>& active_alias,
                                           const std::string& join_label,
                                           RelationRuntimeCache* cache);

}  // namespace markql
