#include "markql/markql.h"

#include <chrono>
#include <optional>
#include <string>

#include "../../util/string_util.h"
#include "engine_execution_internal.h"
#include "relation_runtime_internal.h"
#include "markql_internal.h"

namespace markql {

namespace {

class ScopedProfileTimer {
 public:
  ScopedProfileTimer(RelationRuntimeCache::Profile* profile, uint64_t* target_ns)
      : profile_(profile), target_ns_(target_ns) {
    if (profile_ == nullptr || target_ns_ == nullptr || !profile_->enabled) {
      profile_ = nullptr;
      target_ns_ = nullptr;
      return;
    }
    started_at_ = std::chrono::steady_clock::now();
  }

  ~ScopedProfileTimer() {
    if (profile_ == nullptr || target_ns_ == nullptr) return;
    const auto finished_at = std::chrono::steady_clock::now();
    const uint64_t elapsed_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished_at - started_at_).count());
    *target_ns_ += elapsed_ns;
  }

 private:
  RelationRuntimeCache::Profile* profile_ = nullptr;
  uint64_t* target_ns_ = nullptr;
  std::chrono::steady_clock::time_point started_at_{};
};

void assign_result_column_value(QueryResultRow& row, const std::string& column,
                                const std::optional<std::string>& value) {
  if (!value.has_value()) return;
  if (column == "node_id") {
    if (auto parsed = parse_int64_value(*value); parsed.has_value()) row.node_id = *parsed;
    return;
  }
  if (column == "tag") {
    row.tag = *value;
    return;
  }
  if (column == "text") {
    row.text = *value;
    return;
  }
  if (column == "inner_html") {
    row.inner_html = *value;
    return;
  }
  if (column == "parent_id") {
    if (auto parsed = parse_int64_value(*value); parsed.has_value()) row.parent_id = *parsed;
    return;
  }
  if (column == "sibling_pos") {
    if (auto parsed = parse_int64_value(*value); parsed.has_value()) row.sibling_pos = *parsed;
    return;
  }
  if (column == "max_depth") {
    if (auto parsed = parse_int64_value(*value); parsed.has_value()) row.max_depth = *parsed;
    return;
  }
  if (column == "doc_order") {
    if (auto parsed = parse_int64_value(*value); parsed.has_value()) row.doc_order = *parsed;
    return;
  }
  if (column == "source_uri") {
    row.source_uri = *value;
    return;
  }
  row.computed_fields[column] = *value;
}

}  // namespace

QueryResult query_result_from_relation(const Query& query, const Relation& relation,
                                       RelationRuntimeCache::Profile* profile) {
  ScopedProfileTimer projection_timer(profile,
                                      profile != nullptr ? &profile->projection_time_ns : nullptr);
  QueryResult out;
  out.columns = markql_internal::build_columns(query);
  out.columns_implicit = !markql_internal::is_projection_query(query);
  out.to_list = query.to_list;
  out.to_table = query.to_table;
  out.table_has_header = query.table_has_header;
  out.table_options = to_result_table_options(query.table_options);
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
  out.warnings = relation.warnings;
  out.rows.reserve(relation.rows.size());

  for (const auto& item : query.select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      QueryResultRow row;
      row.node_id = static_cast<int64_t>(relation.rows.size());
      out.rows.push_back(std::move(row));
      return out;
    }
  }

  const std::optional<std::string> active_alias =
      query.source.alias.has_value()
          ? std::optional<std::string>(lower_alias_name(*query.source.alias))
          : std::nullopt;

  if (!markql_internal::is_projection_query(query)) {
    for (const auto& rel_row : relation.rows) {
      const RelationRecord* selected = nullptr;
      for (const auto& item : query.select_items) {
        if (item.self_node_projection) {
          if (active_alias.has_value()) {
            auto it = rel_row.aliases.find(*active_alias);
            if (it != rel_row.aliases.end()) {
              selected = &it->second;
            }
          }
          if (selected == nullptr && !rel_row.aliases.empty()) {
            selected = &rel_row.aliases.begin()->second;
          }
          break;
        }
        const std::string tag_or_alias = lower_alias_name(item.tag);
        if (item.tag == "*") {
          if (active_alias.has_value()) {
            auto it = rel_row.aliases.find(*active_alias);
            if (it != rel_row.aliases.end()) selected = &it->second;
          }
          if (selected == nullptr && !rel_row.aliases.empty()) {
            selected = &rel_row.aliases.begin()->second;
          }
          break;
        }
        auto alias_it = rel_row.aliases.find(tag_or_alias);
        if (alias_it != rel_row.aliases.end()) {
          selected = &alias_it->second;
          break;
        }
        for (const auto& alias_entry : rel_row.aliases) {
          auto tag_it = alias_entry.second.values.find("tag");
          if (tag_it == alias_entry.second.values.end() || !tag_it->second.has_value()) continue;
          if (util::to_lower(*tag_it->second) == tag_or_alias) {
            selected = &alias_entry.second;
            break;
          }
        }
        if (selected != nullptr) break;
      }
      if (selected == nullptr) continue;
      QueryResultRow row;
      fill_result_core_from_record(row, *selected);
      out.rows.push_back(std::move(row));
    }
    return out;
  }

  for (const auto& rel_row : relation.rows) {
    QueryResultRow row;
    const RelationRecord* seed = resolve_record(rel_row, std::nullopt, active_alias);
    if (seed != nullptr) {
      fill_result_core_from_record(row, *seed);
    }
    for (const auto& item : query.select_items) {
      if (!item.field.has_value()) continue;
      std::optional<std::string> value;
      if (item.expr_projection && item.expr.has_value()) {
        value = eval_relation_scalar_expr(*item.expr, rel_row, active_alias, profile);
      } else if (item.expr_projection && item.project_expr.has_value()) {
        value = eval_relation_project_expr(*item.project_expr, rel_row, active_alias,
                                           row.computed_fields, profile);
      } else {
        const std::string lowered_tag = lower_alias_name(item.tag);
        auto it = rel_row.aliases.find(lowered_tag);
        if (it != rel_row.aliases.end()) {
          auto value_it = it->second.values.find(*item.field);
          if (value_it != it->second.values.end()) {
            value = value_it->second;
          }
        } else {
          value = relation_field_by_name(rel_row, *item.field, active_alias);
        }
      }
      assign_result_column_value(row, *item.field, value);
    }
    out.rows.push_back(std::move(row));
  }
  return out;
}

}  // namespace markql
