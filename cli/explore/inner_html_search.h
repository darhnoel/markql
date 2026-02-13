#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "html_parser.h"

namespace xsql::cli {

struct InnerHtmlSearchMatch {
  int64_t node_id = 0;
  int score = 0;
  size_t position = 0;
  std::string snippet;
};

/// Computes a fuzzy match score for `needle` within `haystack`.
/// MUST return false when all needle characters cannot be found in order.
bool fuzzy_match_score(std::string_view haystack,
                       std::string_view needle,
                       size_t* first_position,
                       int* score);

/// Builds a nearby snippet around the matched position for right-pane preview.
/// MUST return compact single-line text and preserve nearest context.
std::string make_inner_html_snippet(std::string_view inner_html,
                                    size_t match_position,
                                    size_t query_len,
                                    size_t max_chars);

/// Searches node.inner_html values with fuzzy matching and returns ranked matches.
/// MUST sort by score descending and node id ascending for deterministic output.
std::vector<InnerHtmlSearchMatch> fuzzy_search_inner_html(const xsql::HtmlDocument& doc,
                                                          const std::string& query,
                                                          size_t max_results,
                                                          bool include_snippet = true,
                                                          bool sort_results = true,
                                                          const std::vector<int64_t>* candidate_node_ids = nullptr);

}  // namespace xsql::cli
