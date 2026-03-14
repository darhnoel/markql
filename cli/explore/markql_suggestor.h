#pragma once

#include <cstdint>
#include <string>

#include "dom/html_parser.h"

namespace markql::cli {

enum class MarkqlSuggestionStrategy {
  None,
  Project,
  Flatten,
};

struct MarkqlSuggestion {
  MarkqlSuggestionStrategy strategy = MarkqlSuggestionStrategy::None;
  int confidence = 0;  // 0-100
  std::string reason;
  std::string statement;
};

/// Builds a deterministic MarkQL statement suggestion for the selected node.
/// MUST return a full executable statement in `statement` when strategy != None.
MarkqlSuggestion suggest_markql_statement(const markql::HtmlDocument& doc, int64_t selected_node_id);

}  // namespace markql::cli
