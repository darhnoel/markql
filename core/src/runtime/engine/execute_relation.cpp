#include "xsql/xsql.h"

#include <algorithm>
#include <cctype>
#include <chrono>
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
#include "engine_execution_internal.h"
#include "relation_runtime_internal.h"
#include "xsql_internal.h"

namespace xsql {

namespace {

bool query_uses_relation_runtime(const Query& query,
                                 const std::unordered_map<std::string, Relation>* ctes,
                                 const RelationRow* outer_row) {
  if (outer_row != nullptr) return true;
  if (ctes != nullptr && !ctes->empty()) return true;
  if (query.with.has_value() && !query.with->ctes.empty()) return true;
  if (!query.joins.empty()) return true;
  if (query.source.kind == Source::Kind::CteRef ||
      query.source.kind == Source::Kind::DerivedSubquery) {
    return true;
  }
  for (const auto& order : query.order_by) {
    if (order.field.find('.') != std::string::npos) return true;
  }
  return false;
}

bool relation_runtime_profile_enabled() {
  static const bool enabled = []() {
    const char* raw = std::getenv("MARKQL_REL_PROFILE");
    if (raw == nullptr) return false;
    std::string value = util::to_lower(util::trim_ws(raw));
    return !value.empty() && value != "0" && value != "false" &&
           value != "no" && value != "off";
  }();
  return enabled;
}

double ns_to_ms(uint64_t value_ns) {
  return static_cast<double>(value_ns) / 1000000.0;
}

std::string join_type_name(Query::JoinItem::Type type) {
  if (type == Query::JoinItem::Type::Inner) return "inner";
  if (type == Query::JoinItem::Type::Left) return "left";
  return "cross";
}

std::string join_label_for_index(const Query::JoinItem& join, size_t index) {
  std::string label = "join#" + std::to_string(index + 1);
  label += ":" + join_type_name(join.type);
  if (join.right_source.alias.has_value()) {
    label += ":" + lower_alias_name(*join.right_source.alias);
  }
  return label;
}

void maybe_emit_relation_runtime_profile(const RelationRuntimeCache::Profile& profile) {
  if (!profile.enabled) return;
  std::fprintf(
      stderr,
      "[markql rel_profile] timings_ms join=%.3f projection=%.3f scalar_expr=%.3f\n",
      ns_to_ms(profile.join_time_ns),
      ns_to_ms(profile.projection_time_ns),
      ns_to_ms(profile.scalar_eval_time_ns));
  std::fprintf(
      stderr,
      "[markql rel_profile] relation_indexes builds=%llu hits=%llu\n",
      static_cast<unsigned long long>(profile.relation_index_builds),
      static_cast<unsigned long long>(profile.relation_index_hits));
  for (const auto& cte : profile.cte_sizes) {
    std::fprintf(stderr,
                 "[markql rel_profile] cte=%s rows=%zu\n",
                 cte.name.c_str(),
                 cte.rows);
  }
  for (const auto& join : profile.joins) {
    std::fprintf(stderr,
                 "[markql rel_profile] %s strategy=%s left_rows=%zu right_rows=%zu output_rows=%zu pairs=%llu\n",
                 join.label.c_str(),
                 join.strategy.c_str(),
                 join.left_rows,
                 join.right_rows,
                 join.output_rows,
                 static_cast<unsigned long long>(join.pairs_evaluated));
  }
}

bool operand_targets_source_row(const Operand& operand,
                                const std::optional<std::string>& active_alias) {
  if (operand.axis != Operand::Axis::Self) return false;
  if (!operand.qualifier.has_value()) return true;
  if (!active_alias.has_value()) return false;
  return lower_alias_name(*operand.qualifier) == *active_alias;
}

std::optional<std::string> compare_rhs_single_value(const CompareExpr& cmp,
                                                    const RelationRow* outer_row,
                                                    const std::optional<std::string>& active_alias) {
  if (cmp.rhs_expr.has_value()) {
    static const RelationRow kEmptyRow;
    const RelationRow& row = outer_row != nullptr ? *outer_row : kEmptyRow;
    return eval_relation_scalar_expr(*cmp.rhs_expr, row, active_alias);
  }
  if (cmp.rhs.values.size() == 1) {
    return cmp.rhs.values.front();
  }
  return std::nullopt;
}

void collect_source_prefilter_constraints(const Expr& expr,
                                          const std::optional<std::string>& active_alias,
                                          const RelationRow* outer_row,
                                          SourceRowPrefilter& out) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    if (cmp.op != CompareExpr::Op::Eq) return;
    const Operand* lhs = nullptr;
    if (cmp.lhs_expr.has_value() && cmp.lhs_expr->kind == ScalarExpr::Kind::Operand) {
      lhs = &cmp.lhs_expr->operand;
    } else {
      lhs = &cmp.lhs;
    }
    if (!operand_targets_source_row(*lhs, active_alias)) return;
    std::optional<std::string> rhs = compare_rhs_single_value(cmp, outer_row, active_alias);
    if (!rhs.has_value()) return;
    if (lhs->field_kind == Operand::FieldKind::Tag) {
      std::string lowered = util::to_lower(*rhs);
      if (out.tag_eq.has_value() && *out.tag_eq != lowered) {
        out.impossible = true;
      } else {
        out.tag_eq = lowered;
      }
      return;
    }
    if (lhs->field_kind == Operand::FieldKind::ParentId) {
      auto parsed = parse_int64_value(*rhs);
      if (!parsed.has_value()) return;
      if (out.parent_id_eq.has_value() && *out.parent_id_eq != *parsed) {
        out.impossible = true;
      } else {
        out.parent_id_eq = *parsed;
      }
      return;
    }
    return;
  }
  if (std::holds_alternative<std::shared_ptr<BinaryExpr>>(expr)) {
    const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
    if (bin.op != BinaryExpr::Op::And) return;
    collect_source_prefilter_constraints(bin.left, active_alias, outer_row, out);
    collect_source_prefilter_constraints(bin.right, active_alias, outer_row, out);
    return;
  }
}

QueryResult execute_query_with_source_context(
    const Query& query,
    const std::string* default_html,
    const HtmlDocument* default_document,
    const std::string& default_source_uri,
    const std::unordered_map<std::string, Relation>* ctes,
    const RelationRow* outer_row,
    RelationRuntimeCache* cache);

Relation relation_from_query_result(QueryResult result, const std::string& alias_name) {
  Relation out;
  const std::string alias = lower_alias_name(alias_name);
  out.cache_key = "relation_result";
  out.warnings = std::move(result.warnings);
  for (auto& row : result.rows) {
    RelationRow rel_row;
    RelationRecord record;
    record.values["node_id"] = std::to_string(row.node_id);
    record.values["tag"] = row.tag;
    record.values["text"] = row.text;
    record.values["inner_html"] = row.inner_html;
    if (row.parent_id.has_value()) {
      record.values["parent_id"] = std::to_string(*row.parent_id);
    } else {
      record.values["parent_id"] = std::nullopt;
    }
    record.values["sibling_pos"] = std::to_string(row.sibling_pos);
    record.values["max_depth"] = std::to_string(row.max_depth);
    record.values["doc_order"] = std::to_string(row.doc_order);
    record.values["source_uri"] = row.source_uri;
    for (const auto& col : result.columns) {
      record.values[col] = field_value_string(row, col);
    }
    for (auto& attr : row.attributes) {
      record.attributes[attr.first] = attr.second;
      record.values[attr.first] = attr.second;
    }
    rel_row.aliases[alias] = std::move(record);
    merge_alias_columns(out, alias, rel_row.aliases[alias]);
    out.rows.push_back(std::move(rel_row));
  }
  return out;
}

std::vector<int64_t> build_sibling_positions(const HtmlDocument& doc) {
  std::vector<int64_t> sibling_pos(doc.nodes.size(), 1);
  std::vector<std::vector<int64_t>> children(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (node.parent_id.has_value()) {
      children.at(static_cast<size_t>(*node.parent_id)).push_back(node.id);
    }
  }
  for (const auto& kids : children) {
    for (size_t i = 0; i < kids.size(); ++i) {
      sibling_pos.at(static_cast<size_t>(kids[i])) = static_cast<int64_t>(i + 1);
    }
  }
  return sibling_pos;
}

Relation relation_from_document(const HtmlDocument& doc,
                                const std::string& alias_name,
                                const std::string& source_uri,
                                const std::vector<int64_t>* sibling_pos_override,
                                const SourceRowPrefilter* prefilter) {
  Relation out;
  const std::string alias = lower_alias_name(alias_name);
  std::vector<int64_t> local_sibling_pos;
  if (sibling_pos_override == nullptr) {
    local_sibling_pos = build_sibling_positions(doc);
    sibling_pos_override = &local_sibling_pos;
  }
  std::unordered_map<int64_t, const HtmlNode*> node_by_id;
  node_by_id.reserve(doc.nodes.size());
  for (const auto& n : doc.nodes) {
    node_by_id[n.id] = &n;
  }
  for (const auto& node : doc.nodes) {
    if (prefilter != nullptr) {
      if (prefilter->impossible) continue;
      if (prefilter->parent_id_eq.has_value()) {
        if (!node.parent_id.has_value() || *node.parent_id != *prefilter->parent_id_eq) continue;
      }
      if (prefilter->tag_eq.has_value() && node.tag != *prefilter->tag_eq) continue;
    }
    RelationRow rel_row;
    RelationRecord record;
    record.values["node_id"] = std::to_string(node.id);
    record.values["tag"] = node.tag;
    record.values["text"] = node.text;
    record.values["inner_html"] = node.inner_html;
    if (node.parent_id.has_value()) {
      record.values["parent_id"] = std::to_string(*node.parent_id);
    } else {
      record.values["parent_id"] = std::nullopt;
    }
    record.values["sibling_pos"] =
        std::to_string(sibling_pos_override->at(static_cast<size_t>(node.id)));
    record.values["max_depth"] = std::to_string(node.max_depth);
    record.values["doc_order"] = std::to_string(node.doc_order);
    record.values["source_uri"] = source_uri;
    for (const auto& attr : node.attributes) {
      record.attributes[attr.first] = attr.second;
      record.values[attr.first] = attr.second;
    }
    if (node.parent_id.has_value()) {
      auto parent_it = node_by_id.find(*node.parent_id);
      if (parent_it != node_by_id.end() && parent_it->second != nullptr) {
        const HtmlNode& parent = *parent_it->second;
        record.values["parent.node_id"] = std::to_string(parent.id);
        record.values["parent.tag"] = parent.tag;
        record.values["parent.text"] = parent.text;
        record.values["parent.inner_html"] = parent.inner_html;
        if (parent.parent_id.has_value()) {
          record.values["parent.parent_id"] = std::to_string(*parent.parent_id);
        } else {
          record.values["parent.parent_id"] = std::nullopt;
        }
        record.values["parent.sibling_pos"] =
            std::to_string(sibling_pos_override->at(static_cast<size_t>(parent.id)));
        record.values["parent.max_depth"] = std::to_string(parent.max_depth);
        record.values["parent.doc_order"] = std::to_string(parent.doc_order);
        for (const auto& attr : parent.attributes) {
          record.values["parent." + attr.first] = attr.second;
        }
      }
    }
    rel_row.aliases[alias] = std::move(record);
    merge_alias_columns(out, alias, rel_row.aliases[alias]);
    out.rows.push_back(std::move(rel_row));
  }
  return out;
}

Relation evaluate_source_relation(const Source& source,
                                  const std::string* default_html,
                                  const HtmlDocument* default_document,
                                  const std::string& default_source_uri,
                                  const std::unordered_map<std::string, Relation>* ctes,
                                  const RelationRow* outer_row,
                                  RelationRuntimeCache* cache,
                                  const SourceRowPrefilter* prefilter) {
  if (source.kind == Source::Kind::CteRef) {
    const std::string lookup = lower_alias_name(source.value);
    if (ctes == nullptr || ctes->find(lookup) == ctes->end()) {
      throw std::runtime_error("Unknown CTE source '" + source.value + "'");
    }
    Relation rel = ctes->at(lookup);
    const std::string target_alias =
        source.alias.has_value() ? lower_alias_name(*source.alias) : lookup;
    if (target_alias != lookup) {
      for (auto& row : rel.rows) {
        auto it = row.aliases.find(lookup);
        if (it == row.aliases.end()) continue;
        RelationRecord record = std::move(it->second);
        row.aliases.erase(it);
        row.aliases[target_alias] = std::move(record);
      }
      auto schema_it = rel.alias_columns.find(lookup);
      if (schema_it != rel.alias_columns.end()) {
        rel.alias_columns[target_alias] = std::move(schema_it->second);
        rel.alias_columns.erase(schema_it);
      }
    }
    return rel;
  }
  if (source.kind == Source::Kind::DerivedSubquery) {
    if (source.derived_query == nullptr) {
      throw std::runtime_error("Derived table source is missing a subquery");
    }
    if (!source.alias.has_value()) {
      throw std::runtime_error("Derived table requires an alias");
    }
    QueryResult sub = execute_query_with_source_context(
        *source.derived_query, default_html, default_document, default_source_uri, ctes, outer_row, cache);
    return relation_from_query_result(std::move(sub), *source.alias);
  }

  HtmlDocument doc;
  const std::vector<int64_t>* sibling_pos = nullptr;
  std::string source_uri = default_source_uri;
  std::vector<std::string> warnings;
  if (source.kind == Source::Kind::Document) {
    // WHY: WITH/JOIN/LATERAL can revisit FROM doc many times; parse once per statement.
    if (cache != nullptr && cache->default_document.has_value()) {
      doc = *cache->default_document;
    } else if (default_document != nullptr) {
      doc = *default_document;
      if (cache != nullptr) cache->default_document = doc;
    } else {
      doc = parse_html(*default_html);
      if (cache != nullptr) {
        cache->default_document = doc;
      }
    }
    if (cache != nullptr) {
      if (!cache->default_sibling_pos.has_value()) {
        cache->default_sibling_pos = build_sibling_positions(doc);
      }
      sibling_pos = &*cache->default_sibling_pos;
    }
  } else if (source.kind == Source::Kind::Path) {
    doc = parse_html(xsql_internal::read_file(source.value));
    source_uri = source.value;
  } else if (source.kind == Source::Kind::Url) {
    doc = parse_html(xsql_internal::fetch_url(source.value, 5000));
    source_uri = source.value;
  } else if (source.kind == Source::Kind::RawHtml) {
    if (source.value.size() > xsql_internal::kMaxRawHtmlBytes) {
      throw std::runtime_error("RAW() HTML exceeds maximum size");
    }
    doc = parse_html(source.value);
    source_uri = "raw";
  } else if (source.kind == Source::Kind::Fragments) {
    FragmentSource fragments;
    if (source.fragments_raw.has_value()) {
      fragments.fragments.push_back(*source.fragments_raw);
    } else if (source.fragments_query != nullptr) {
      const Query& subquery = *source.fragments_query;
      QueryResult sub = execute_query_with_source_context(
          subquery, default_html, default_document, default_source_uri, ctes, nullptr, cache);
      fragments = collect_html_fragments(sub, "FRAGMENTS");
    } else {
      throw std::runtime_error("FRAGMENTS requires a subquery or RAW('<...>') input");
    }
    doc = build_fragments_document(fragments);
    source_uri = "fragment";
    warnings.push_back("FRAGMENTS is deprecated; use PARSE(...) instead.");
  } else if (source.kind == Source::Kind::Parse) {
    FragmentSource fragments;
    if (source.parse_expr != nullptr) {
      std::optional<std::string> value = eval_parse_source_expr(*source.parse_expr);
      if (!value.has_value()) {
        throw std::runtime_error("PARSE() requires a non-null HTML string expression");
      }
      std::string trimmed = util::trim_ws(*value);
      if (trimmed.empty() || !looks_like_html_fragment(trimmed)) {
        throw std::runtime_error("PARSE() expects an HTML string expression");
      }
      fragments.fragments.push_back(std::move(trimmed));
    } else if (source.parse_query != nullptr) {
      QueryResult sub = execute_query_with_source_context(
          *source.parse_query, default_html, default_document, default_source_uri, ctes, nullptr, cache);
      fragments = collect_html_fragments(sub, "PARSE");
    } else {
      throw std::runtime_error("PARSE() requires an expression or subquery input");
    }
    doc = build_fragments_document(fragments);
    source_uri = "parse";
  } else {
    throw std::runtime_error("Unsupported source kind in relation runtime");
  }
  const std::string alias =
      source.alias.has_value() ? *source.alias : std::string("__self");
  Relation rel = relation_from_document(doc, alias, source_uri, sibling_pos, prefilter);
  for (const auto& warning : warnings) {
    rel.warnings.push_back(warning);
  }
  return rel;
}

Relation evaluate_query_relation(
    const Query& query,
    const std::string* default_html,
    const HtmlDocument* default_document,
    const std::string& default_source_uri,
    const std::unordered_map<std::string, Relation>* parent_ctes,
    const RelationRow* outer_row,
    RelationRuntimeCache* cache) {
  RelationRuntimeCache::Profile* profile = cache != nullptr ? &cache->profile : nullptr;
  const std::optional<std::string> active_alias =
      query.source.alias.has_value()
          ? std::optional<std::string>(lower_alias_name(*query.source.alias))
          : std::nullopt;

  std::unordered_map<std::string, Relation> local_ctes;
  size_t expected_cte_count = query.with.has_value() ? query.with->ctes.size() : 0;
  if (parent_ctes != nullptr) {
    local_ctes.reserve(parent_ctes->size() + expected_cte_count);
    local_ctes.insert(parent_ctes->begin(), parent_ctes->end());
  } else if (expected_cte_count > 0) {
    local_ctes.reserve(expected_cte_count);
  }
  std::vector<std::string> warnings;
  if (query.with.has_value()) {
    for (const auto& cte : query.with->ctes) {
      if (cte.query == nullptr) {
        throw std::runtime_error("CTE '" + cte.name + "' is missing a subquery");
      }
      QueryResult cte_result = execute_query_with_source_context(
          *cte.query, default_html, default_document, default_source_uri, &local_ctes, nullptr, cache);
      Relation cte_relation = relation_from_query_result(std::move(cte_result), cte.name);
      if (cache != nullptr) {
        cte_relation.cache_key =
            "cte:" + lower_alias_name(cte.name) + "#" + std::to_string(cache->next_relation_cache_id++);
      }
      warnings.insert(warnings.end(),
                      cte_relation.warnings.begin(),
                      cte_relation.warnings.end());
      if (profile != nullptr && profile->enabled) {
        profile->cte_sizes.push_back(RelationRuntimeCache::CteSizeSample{
            cte.name, cte_relation.rows.size()});
      }
      local_ctes[lower_alias_name(cte.name)] = std::move(cte_relation);
    }
  }

  std::optional<SourceRowPrefilter> source_prefilter;
  if (query.source.kind == Source::Kind::Document &&
      query.where.has_value()) {
    SourceRowPrefilter candidate;
    collect_source_prefilter_constraints(*query.where, active_alias, outer_row, candidate);
    if (candidate.impossible ||
        candidate.parent_id_eq.has_value() ||
        candidate.tag_eq.has_value()) {
      source_prefilter = std::move(candidate);
    }
  }

  Relation from_rel = evaluate_source_relation(
      query.source, default_html, default_document, default_source_uri, &local_ctes, outer_row, cache,
      source_prefilter.has_value() ? &*source_prefilter : nullptr);
  warnings.insert(warnings.end(), from_rel.warnings.begin(), from_rel.warnings.end());

  Relation current;
  if (outer_row == nullptr) {
    current = std::move(from_rel);
  } else {
    current.alias_columns = from_rel.alias_columns;
    current.cache_key = from_rel.cache_key;
    current.rows.reserve(from_rel.rows.size());
    for (const auto& base_row : from_rel.rows) {
      RelationRow merged;
      std::string duplicate;
      if (!merge_row_aliases(merged, *outer_row, &duplicate)) {
        throw std::runtime_error("Duplicate source alias '" + duplicate + "' in FROM");
      }
      if (!merge_row_aliases(merged, base_row, &duplicate)) {
        throw std::runtime_error("Duplicate source alias '" + duplicate + "' in FROM");
      }
      current.rows.push_back(std::move(merged));
    }
    for (const auto& alias_entry : outer_row->aliases) {
      merge_alias_columns(current, alias_entry.first, alias_entry.second);
    }
  }

  for (size_t join_index = 0; join_index < query.joins.size(); ++join_index) {
    const auto& join = query.joins[join_index];
    const std::string join_label = join_label_for_index(join, join_index);
    if (join.lateral) {
      const bool profiling_enabled = profile != nullptr && profile->enabled;
      const auto started_at = profiling_enabled ? std::chrono::steady_clock::now()
                                                : std::chrono::steady_clock::time_point{};
      uint64_t pairs_evaluated = 0;
      Relation next;
      next.alias_columns = current.alias_columns;
      next.cache_key = current.cache_key;
      for (const auto& left_row : current.rows) {
        Relation right_rel = evaluate_source_relation(
            join.right_source, default_html, default_document, default_source_uri, &local_ctes, &left_row, cache,
            nullptr);
        warnings.insert(warnings.end(), right_rel.warnings.begin(), right_rel.warnings.end());
        for (const auto& alias_cols : right_rel.alias_columns) {
          next.alias_columns[alias_cols.first].insert(
              alias_cols.second.begin(), alias_cols.second.end());
        }
        for (const auto& right_row : right_rel.rows) {
          ++pairs_evaluated;
          RelationRow merged = left_row;
          std::string duplicate;
          if (!merge_row_aliases(merged, right_row, &duplicate)) {
            throw std::runtime_error("Duplicate source alias '" + duplicate + "' in FROM");
          }
          next.rows.push_back(std::move(merged));
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
            "lateral_nested_loop",
            current.rows.size(),
            0,
            next.rows.size(),
            pairs_evaluated});
      }
      current = std::move(next);
      continue;
    }

    Relation right_rel = evaluate_source_relation(
        join.right_source, default_html, default_document, default_source_uri, &local_ctes, nullptr, cache,
        nullptr);
    warnings.insert(warnings.end(), right_rel.warnings.begin(), right_rel.warnings.end());
    current = execute_relation_join_non_lateral(
        join, current, right_rel, active_alias, join_label, cache);
  }

  if (query.where.has_value()) {
    Relation filtered;
    filtered.alias_columns = current.alias_columns;
    filtered.cache_key = current.cache_key;
    filtered.rows.reserve(current.rows.size());
    for (auto& row : current.rows) {
      if (eval_relation_expr(*query.where, row, active_alias, profile)) {
        filtered.rows.push_back(std::move(row));
      }
    }
    current = std::move(filtered);
  }

  if (!query.order_by.empty()) {
    std::stable_sort(current.rows.begin(), current.rows.end(),
                     [&](const RelationRow& left, const RelationRow& right) {
                       for (const auto& order : query.order_by) {
                         int cmp = compare_optional_relation_values(
                             relation_field_by_name(left, order.field, active_alias),
                             relation_field_by_name(right, order.field, active_alias));
                         if (cmp == 0) continue;
                         return order.descending ? (cmp > 0) : (cmp < 0);
                       }
                       return false;
                     });
  }
  if (query.limit.has_value() && current.rows.size() > *query.limit) {
    current.rows.resize(*query.limit);
  }
  current.warnings = std::move(warnings);
  return current;
}

QueryResult execute_query_with_source_context(
    const Query& query,
    const std::string* default_html,
    const HtmlDocument* default_document,
    const std::string& default_source_uri,
    const std::unordered_map<std::string, Relation>* ctes,
    const RelationRow* outer_row,
    RelationRuntimeCache* cache) {
  if (!query_uses_relation_runtime(query, ctes, outer_row)) {
    return execute_query_with_source_legacy(query, default_html, default_document, default_source_uri);
  }
  const bool is_top_level_relation_query = (cache == nullptr);
  RelationRuntimeCache local_cache;
  RelationRuntimeCache* active_cache = cache != nullptr ? cache : &local_cache;
  if (is_top_level_relation_query) {
    active_cache->profile = RelationRuntimeCache::Profile{};
    active_cache->profile.enabled = relation_runtime_profile_enabled();
  }
  Relation relation = evaluate_query_relation(
      query, default_html, default_document, default_source_uri, ctes, outer_row, active_cache);
  QueryResult out = query_result_from_relation(query, relation, &active_cache->profile);
  if (is_top_level_relation_query) {
    maybe_emit_relation_runtime_profile(active_cache->profile);
  }
  return out;
}


}  // namespace

QueryResult execute_query_with_source_relation_entry(const Query& query,
                                                     const std::string* default_html,
                                                     const HtmlDocument* default_document,
                                                     const std::string& default_source_uri) {
  return execute_query_with_source_context(
      query, default_html, default_document, default_source_uri, nullptr, nullptr, nullptr);
}

}  // namespace xsql
