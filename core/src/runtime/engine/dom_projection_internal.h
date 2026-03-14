#pragma once

#include "markql/markql.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../dom/html_parser.h"
#include "../../lang/markql_parser.h"

namespace markql {

struct ProjectBenchStats {
  uint64_t selector_calls = 0;
  uint64_t scope_builds = 0;
  uint64_t tag_cache_builds = 0;
  uint64_t candidate_nodes_examined = 0;
};

bool project_bench_stats_enabled();
void maybe_emit_project_bench_stats(const ProjectBenchStats& stats);

struct ProjectRowEvalCache {
  std::vector<int64_t> scope_nodes;
  std::unordered_map<std::string, std::vector<int64_t>> tag_nodes;
  ProjectBenchStats* stats = nullptr;

  void reset_for_row(const std::vector<std::vector<int64_t>>& children, int64_t node_id);
  const std::vector<int64_t>& nodes_for_tag(const std::string& extract_tag, const HtmlDocument& doc);
};

std::string normalize_flatten_text(const std::string& value);

struct ScalarProjectionValue {
  enum class Kind { Null, String, Number } kind = Kind::Null;
  std::string string_value;
  int64_t number_value = 0;
};

bool projection_is_null(const ScalarProjectionValue& value);
std::string projection_to_string(const ScalarProjectionValue& value);
ScalarProjectionValue eval_select_scalar_expr(
    const ScalarExpr& expr,
    const HtmlNode& node,
    const HtmlDocument* doc = nullptr,
    const std::vector<std::vector<int64_t>>* children = nullptr);

std::optional<std::string> eval_flatten_extract_expr(
    const Query::SelectItem::FlattenExtractExpr& expr,
    const HtmlNode& base_node,
    const HtmlDocument& doc,
    const std::vector<std::vector<int64_t>>& children,
    const std::unordered_map<std::string, std::string>& bindings,
    ProjectRowEvalCache* row_cache);

std::optional<std::string> eval_parse_source_expr(const ScalarExpr& expr);

}  // namespace markql
