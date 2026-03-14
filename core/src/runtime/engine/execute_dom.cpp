#include "markql/markql.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "../executor/executor_internal.h"
#include "../executor.h"
#include "../../dom/html_parser.h"
#include "../../lang/markql_parser.h"
#include "../../util/string_util.h"
#include "dom_descendants_internal.h"
#include "dom_projection_internal.h"
#include "engine_execution_internal.h"
#include "markql_internal.h"

namespace markql {

namespace {
struct ScopedProjectBenchStats {
  ProjectBenchStats stats;
  ~ScopedProjectBenchStats() { maybe_emit_project_bench_stats(stats); }
};

}  // namespace

QueryResult execute_query_ast(const Query& query, const HtmlDocument& doc, const std::string& source_uri) {
  ExecuteResult exec = execute_query(query, doc, source_uri);
  ScopedProjectBenchStats scoped_project_bench_stats;
  ProjectBenchStats* project_bench_stats =
      project_bench_stats_enabled() ? &scoped_project_bench_stats.stats : nullptr;
  QueryResult out;
  out.columns = markql_internal::build_columns(query);
  out.columns_implicit = !markql_internal::is_projection_query(query);
  out.source_uri_excluded =
      std::find(query.exclude_fields.begin(), query.exclude_fields.end(), "source_uri") !=
      query.exclude_fields.end();
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
  if (query.export_sink.has_value() &&
      (query.to_table || markql_internal::is_table_select(query)) &&
      exec.nodes.size() != 1) {
    throw std::runtime_error(
        "Export requires a single table result; add a filter to select one table");
  }
  if (!query.select_items.empty() &&
      query.select_items[0].aggregate == Query::SelectItem::Aggregate::Tfidf) {
    out.rows = markql_internal::build_tfidf_rows(query, exec.nodes);
    return out;
  }
  if (!query.select_items.empty() &&
      query.select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize) {
    std::unordered_map<std::string, size_t> counts;
    for (const auto& node : exec.nodes) {
      ++counts[node.tag];
    }
    std::vector<std::pair<std::string, size_t>> summary;
    summary.reserve(counts.size());
    for (const auto& kv : counts) {
      summary.emplace_back(kv.first, kv.second);
    }
    if (!query.order_by.empty()) {
      std::sort(summary.begin(), summary.end(),
                [&](const auto& a, const auto& b) {
                  for (const auto& order_by : query.order_by) {
                    int cmp = 0;
                    if (order_by.field == "count") {
                      if (a.second < b.second) cmp = -1;
                      else if (a.second > b.second) cmp = 1;
                    } else {
                      if (a.first < b.first) cmp = -1;
                      else if (a.first > b.first) cmp = 1;
                    }
                    if (cmp == 0) continue;
                    if (order_by.descending) {
                      return cmp > 0;
                    }
                    return cmp < 0;
                  }
                  return false;
                });
    } else {
      std::sort(summary.begin(), summary.end(),
                [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second > b.second;
                  return a.first < b.first;
                });
    }
    if (query.limit.has_value() && summary.size() > *query.limit) {
      summary.resize(*query.limit);
    }
    for (const auto& item : summary) {
      QueryResultRow row;
      row.tag = item.first;
      row.node_id = static_cast<int64_t>(item.second);
      row.source_uri = source_uri;
      out.rows.push_back(std::move(row));
    }
    return out;
  }
  // WHY: table extraction bypasses row projections to preserve table layout.
  if (query.to_table ||
      (query.export_sink.has_value() && markql_internal::is_table_select(query))) {
    auto children = markql_internal::build_children(doc);
    for (const auto& node : exec.nodes) {
      QueryResult::TableResult table;
      table.node_id = node.id;
      markql_internal::collect_rows(doc, children, node.id, table.rows);
      if (!table_uses_default_output(query)) {
        materialize_table_result(
            table.rows, query.table_has_header, query.table_options, table);
      }
      out.tables.push_back(std::move(table));
    }
    return out;
  }
  const Query::SelectItem* flatten_item = nullptr;
  const Query::SelectItem* flatten_extract_item = nullptr;
  for (const auto& item : query.select_items) {
    if (item.flatten_text) {
      flatten_item = &item;
    }
    if (item.flatten_extract) {
      flatten_extract_item = &item;
    }
  }
  if (flatten_extract_item != nullptr) {
    auto children = markql_internal::build_children(doc);
    std::vector<int64_t> sibling_positions(doc.nodes.size(), 1);
    for (size_t parent = 0; parent < children.size(); ++parent) {
      const auto& kids = children[parent];
      for (size_t idx = 0; idx < kids.size(); ++idx) {
        sibling_positions.at(static_cast<size_t>(kids[idx])) = static_cast<int64_t>(idx + 1);
      }
    }
    std::string base_tag = util::to_lower(flatten_extract_item->tag);
    bool tag_is_alias = query.source.alias.has_value() &&
                        util::to_lower(*query.source.alias) == base_tag;
    bool match_all_tags = tag_is_alias || base_tag == "document";
    struct FlattenExtractRow {
      const HtmlNode* node = nullptr;
      QueryResultRow row;
    };
    std::vector<FlattenExtractRow> rows;
    rows.reserve(doc.nodes.size());
    for (const auto& node : doc.nodes) {
      if (!match_all_tags && node.tag != base_tag) {
        continue;
      }
      if (query.where.has_value()) {
        if (!executor_internal::eval_expr(*query.where, doc, children, node)) {
          continue;
        }
      }
      QueryResultRow row;
      row.node_id = node.id;
      row.tag = node.tag;
      row.text = node.text;
      row.inner_html = node.inner_html;
      row.attributes = node.attributes;
      row.source_uri = source_uri;
      row.sibling_pos = sibling_positions.at(static_cast<size_t>(node.id));
      row.max_depth = node.max_depth;
      row.doc_order = node.doc_order;
      row.parent_id = node.parent_id;

      ProjectRowEvalCache row_eval_cache;
      row_eval_cache.stats = project_bench_stats;
      row_eval_cache.reset_for_row(children, node.id);
      for (size_t i = 0; i < flatten_extract_item->flatten_extract_aliases.size(); ++i) {
        const auto& alias = flatten_extract_item->flatten_extract_aliases[i];
        const auto& expr = flatten_extract_item->flatten_extract_exprs[i];
        std::optional<std::string> value =
            eval_flatten_extract_expr(expr, node, doc, children, row.computed_fields, &row_eval_cache);
        if (!value.has_value()) continue;
        row.computed_fields[alias] = *value;
      }
      rows.push_back(FlattenExtractRow{&node, std::move(row)});
    }
    if (!query.order_by.empty()) {
      std::stable_sort(rows.begin(), rows.end(),
                       [&](const auto& left, const auto& right) {
                         for (const auto& order_by : query.order_by) {
                           int cmp = executor_internal::compare_nodes(*left.node, *right.node, order_by.field);
                           if (cmp == 0) continue;
                           if (order_by.descending) {
                             return cmp > 0;
                           }
                           return cmp < 0;
                         }
                         return false;
                       });
    }
    if (query.limit.has_value() && rows.size() > *query.limit) {
      rows.resize(*query.limit);
    }
    out.rows.reserve(rows.size());
    for (auto& entry : rows) {
      out.rows.push_back(std::move(entry.row));
    }
    return out;
  }
  if (flatten_item != nullptr) {
    auto children = markql_internal::build_children(doc);
    std::vector<int64_t> sibling_positions(doc.nodes.size(), 1);
    for (size_t parent = 0; parent < children.size(); ++parent) {
      const auto& kids = children[parent];
      for (size_t idx = 0; idx < kids.size(); ++idx) {
        sibling_positions.at(static_cast<size_t>(kids[idx])) = static_cast<int64_t>(idx + 1);
      }
    }
    DescendantTagFilter descendant_filter;
    if (query.where.has_value()) {
      collect_descendant_tag_filter(*query.where, descendant_filter);
    }
    std::string base_tag = util::to_lower(flatten_item->tag);
    bool tag_is_alias = query.source.alias.has_value() &&
                        util::to_lower(*query.source.alias) == base_tag;
    bool match_all_tags = tag_is_alias || base_tag == "document";
    struct FlattenRow {
      const HtmlNode* node = nullptr;
      QueryResultRow row;
    };
    std::vector<FlattenRow> rows;
    rows.reserve(doc.nodes.size());
    for (const auto& node : doc.nodes) {
      if (!match_all_tags && node.tag != base_tag) {
        continue;
      }
      if (query.where.has_value()) {
        if (!executor_internal::eval_expr_flatten_base(*query.where, doc, children, node)) {
          continue;
        }
      }
      QueryResultRow row;
      row.node_id = node.id;
      row.tag = node.tag;
      row.text = node.text;
      row.inner_html = node.inner_html;
      row.attributes = node.attributes;
      row.source_uri = source_uri;
      row.sibling_pos = sibling_positions.at(static_cast<size_t>(node.id));
      row.max_depth = node.max_depth;
      row.doc_order = node.doc_order;
      row.parent_id = node.parent_id;

      std::vector<int64_t> descendants;
      bool depth_is_default = !flatten_item->flatten_depth.has_value();
      if (depth_is_default) {
        collect_descendants_any_depth(children, node.id, descendants);
      } else {
        collect_descendants_at_depth(children, node.id, *flatten_item->flatten_depth, descendants);
      }
      std::vector<std::string> values;
      for (int64_t id : descendants) {
        const auto& child = doc.nodes.at(static_cast<size_t>(id));
        bool matched = true;
        for (const auto& pred : descendant_filter.predicates) {
          if (!match_descendant_predicate(child, pred)) {
            matched = false;
            break;
          }
        }
        if (!matched) continue;
        std::string direct = markql_internal::extract_direct_text_strict(child.inner_html);
        std::string normalized = normalize_flatten_text(direct);
        if (normalized.empty()) {
          direct = markql_internal::extract_direct_text(child.inner_html);
          normalized = normalize_flatten_text(direct);
        }
        if (depth_is_default && normalized.empty()) {
          continue;
        }
        values.push_back(std::move(normalized));
      }
      for (size_t i = 0; i < flatten_item->flatten_aliases.size(); ++i) {
        if (i < values.size()) {
          row.computed_fields[flatten_item->flatten_aliases[i]] = values[i];
        }
      }
      rows.push_back(FlattenRow{&node, std::move(row)});
    }
    if (!query.order_by.empty()) {
      std::stable_sort(rows.begin(), rows.end(),
                       [&](const auto& left, const auto& right) {
                         for (const auto& order_by : query.order_by) {
                           int cmp = executor_internal::compare_nodes(*left.node, *right.node, order_by.field);
                           if (cmp == 0) continue;
                           if (order_by.descending) {
                             return cmp > 0;
                           }
                           return cmp < 0;
                         }
                         return false;
                       });
    }
    if (query.limit.has_value() && rows.size() > *query.limit) {
      rows.resize(*query.limit);
    }
    out.rows.reserve(rows.size());
    for (auto& entry : rows) {
      out.rows.push_back(std::move(entry.row));
    }
    return out;
  }
  for (const auto& item : query.select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      QueryResultRow row;
      row.node_id = static_cast<int64_t>(exec.nodes.size());
      row.source_uri = source_uri;
      out.rows.push_back(row);
      return out;
    }
  }
  auto inner_html_depth = markql_internal::find_inner_html_depth(query);
  bool inner_html_auto_depth = markql_internal::has_inner_html_auto_depth(query);
  std::unordered_set<std::string> trim_fields;
  trim_fields.reserve(query.select_items.size());
  for (const auto& item : query.select_items) {
    if (!item.trim || !item.field.has_value()) {
      continue;
    }
    trim_fields.insert(*item.field);
  }
  bool use_text_function = false;
  bool use_inner_html_function = false;
  bool use_raw_inner_html_function = false;
  bool has_project_expr = false;
  for (const auto& item : query.select_items) {
    if (item.field.has_value() && *item.field == "text" && item.text_function) {
      use_text_function = true;
    }
    if (item.field.has_value() && *item.field == "inner_html" && item.inner_html_function) {
      use_inner_html_function = true;
      if (item.raw_inner_html_function) {
        use_raw_inner_html_function = true;
      }
    }
    if (item.project_expr.has_value()) {
      has_project_expr = true;
    }
  }
  auto children = markql_internal::build_children(doc);
  std::vector<int64_t> sibling_positions(doc.nodes.size(), 1);
  for (size_t parent = 0; parent < children.size(); ++parent) {
    const auto& kids = children[parent];
    for (size_t idx = 0; idx < kids.size(); ++idx) {
      sibling_positions.at(static_cast<size_t>(kids[idx])) = static_cast<int64_t>(idx + 1);
    }
  }
  for (const auto& node : exec.nodes) {
    QueryResultRow row;
    row.node_id = node.id;
    row.tag = node.tag;
    std::optional<size_t> effective_inner_html_depth = inner_html_depth;
    if (!effective_inner_html_depth.has_value() && use_inner_html_function) {
      // WHY: MAX_DEPTH means "full subtree for this row" without guessing a literal depth.
      effective_inner_html_depth = inner_html_auto_depth
                                       ? static_cast<size_t>(std::max<int64_t>(0, node.max_depth))
                                       : 1;
    }
    row.text = use_text_function ? markql_internal::extract_direct_text(node.inner_html) : node.text;
    row.inner_html = effective_inner_html_depth.has_value()
                         ? markql_internal::limit_inner_html(node.inner_html, *effective_inner_html_depth)
                         : node.inner_html;
    if (use_inner_html_function && !use_raw_inner_html_function) {
      row.inner_html = util::minify_html(row.inner_html);
    }
    row.attributes = node.attributes;
    row.source_uri = source_uri;
    row.sibling_pos = sibling_positions.at(static_cast<size_t>(node.id));
    row.max_depth = node.max_depth;
    row.doc_order = node.doc_order;
    ProjectRowEvalCache row_eval_cache;
    ProjectRowEvalCache* row_eval_cache_ptr = nullptr;
    if (has_project_expr) {
      row_eval_cache.stats = project_bench_stats;
      row_eval_cache.reset_for_row(children, node.id);
      row_eval_cache_ptr = &row_eval_cache;
    }
    for (const auto& item : query.select_items) {
      if (!item.expr_projection || !item.field.has_value()) continue;
      if (item.project_expr.has_value()) {
        std::optional<std::string> value =
            eval_flatten_extract_expr(*item.project_expr, node, doc, children, row.computed_fields,
                                      row_eval_cache_ptr);
        if (!value.has_value()) continue;
        row.computed_fields[*item.field] = *value;
        continue;
      }
      if (!item.expr.has_value()) continue;
      ScalarProjectionValue value = eval_select_scalar_expr(*item.expr, node, &doc, &children);
      if (projection_is_null(value)) continue;
      row.computed_fields[*item.field] = projection_to_string(value);
    }
    for (const auto& field : trim_fields) {
      if (field == "text") {
        row.text = util::trim_ws(row.text);
      } else if (field == "inner_html") {
        row.inner_html = util::trim_ws(row.inner_html);
      } else if (field == "tag") {
        row.tag = util::trim_ws(row.tag);
      } else if (field == "source_uri") {
        row.source_uri = util::trim_ws(row.source_uri);
      } else {
        auto it = row.attributes.find(field);
        if (it != row.attributes.end()) {
          it->second = util::trim_ws(it->second);
        }
      }
    }
    row.parent_id = node.parent_id;
    out.rows.push_back(row);
  }
  return out;
}

}  // namespace markql
