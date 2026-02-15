#include "explore/inner_html_search.h"

#include <algorithm>
#include <cctype>

namespace xsql::cli {

namespace {

enum : int {
  kSourceDescendant = 1,
  kSourceSelfTextOrTag = 2,
  kSourceSelfAttribute = 3,
};

struct ContiguousHit {
  bool found = false;
  size_t position = 0;
  int quality_rank = 0;  // 2=whole-word, 1=word-start, 0=other
};

struct MatchSignal {
  size_t position = 0;
  int quality_rank = 0;
  int raw_score = 0;
};

struct ScopeCandidate {
  int source_priority = 0;
  size_t position = 0;
  int quality_rank = 0;
  int raw_score = 0;
  bool position_in_inner_html = false;
  std::string snippet_source;
};

std::string to_lower_ascii(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

bool is_word_char(char c) {
  unsigned char uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) || c == '_';
}

std::string compact_whitespace(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool in_space = false;
  for (char c : text) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!in_space) {
        out.push_back(' ');
        in_space = true;
      }
      continue;
    }
    in_space = false;
    out.push_back(c);
  }
  size_t start = 0;
  while (start < out.size() && out[start] == ' ') ++start;
  size_t end = out.size();
  while (end > start && out[end - 1] == ' ') --end;
  return out.substr(start, end - start);
}

ContiguousHit find_best_contiguous_hit(std::string_view haystack, std::string_view needle) {
  ContiguousHit best;
  if (needle.empty() || haystack.empty()) return best;

  std::string lower_hay = to_lower_ascii(haystack);
  std::string lower_needle = to_lower_ascii(needle);
  size_t search_from = 0;
  while (search_from <= lower_hay.size()) {
    size_t pos = lower_hay.find(lower_needle, search_from);
    if (pos == std::string::npos) break;

    bool before_ok = (pos == 0) || !is_word_char(haystack[pos - 1]);
    size_t end_pos = pos + lower_needle.size();
    bool after_ok = (end_pos >= haystack.size()) || !is_word_char(haystack[end_pos]);
    bool word_start = before_ok;
    bool whole_word = before_ok && after_ok;
    int quality_rank = (whole_word ? 2 : 0) + (word_start ? 1 : 0);

    if (!best.found || quality_rank > best.quality_rank ||
        (quality_rank == best.quality_rank && pos < best.position)) {
      best.found = true;
      best.position = pos;
      best.quality_rank = quality_rank;
      if (quality_rank == 3 && pos == 0) break;
    }
    search_from = pos + 1;
  }
  return best;
}

bool exact_match_signal(std::string_view haystack, std::string_view needle, MatchSignal* out) {
  if (needle.empty() || haystack.empty()) return false;
  ContiguousHit hit = find_best_contiguous_hit(haystack, needle);
  if (!hit.found) return false;
  if (!out) return true;

  int score = 100000 - static_cast<int>(std::min<size_t>(hit.position, static_cast<size_t>(100000)));
  if (hit.quality_rank >= 1) score += 20000;
  if (hit.quality_rank >= 2) score += 40000;
  out->position = hit.position;
  out->quality_rank = hit.quality_rank;
  out->raw_score = score;
  return true;
}

bool fuzzy_match_signal(std::string_view haystack, std::string_view needle, MatchSignal* out) {
  if (needle.empty() || haystack.empty()) return false;
  std::string lower_hay = to_lower_ascii(haystack);
  std::string lower_needle = to_lower_ascii(needle);

  size_t first_subseq = std::string::npos;
  size_t last_subseq = 0;
  size_t cursor = 0;
  for (char c : lower_needle) {
    size_t pos = lower_hay.find(c, cursor);
    if (pos == std::string::npos) return false;
    if (first_subseq == std::string::npos) first_subseq = pos;
    last_subseq = pos;
    cursor = pos + 1;
  }

  size_t span = (last_subseq >= first_subseq) ? (last_subseq - first_subseq + 1) : 0;
  int score = 100000 - static_cast<int>(span * 100) - static_cast<int>(first_subseq);

  ContiguousHit hit = find_best_contiguous_hit(haystack, needle);
  MatchSignal signal;
  if (hit.found) {
    signal.position = hit.position;
    signal.quality_rank = hit.quality_rank;
    score += 50000;
    if (hit.quality_rank >= 1) score += 20000;
    if (hit.quality_rank >= 2) score += 40000;
  } else {
    signal.position = first_subseq;
    signal.quality_rank = 0;
  }
  signal.raw_score = score;
  if (out) *out = signal;
  return true;
}

std::string build_match_snippet(std::string_view inner_html,
                                size_t match_position,
                                size_t query_len,
                                size_t max_chars) {
  if (inner_html.empty() || max_chars == 0) return "(empty)";

  size_t radius = max_chars / 2;
  size_t start = (match_position > radius) ? (match_position - radius) : 0;
  size_t end = std::min(inner_html.size(), match_position + std::max<size_t>(query_len, 1) + radius);
  std::string snippet = compact_whitespace(inner_html.substr(start, end - start));
  if (snippet.empty()) return "(empty)";
  if (start > 0) snippet = "..." + snippet;
  if (end < inner_html.size()) snippet += "...";
  return snippet;
}

bool is_better_scope_candidate(const ScopeCandidate& left, const ScopeCandidate& right) {
  if (left.source_priority != right.source_priority) return left.source_priority > right.source_priority;
  if (left.quality_rank != right.quality_rank) return left.quality_rank > right.quality_rank;
  if (left.position != right.position) return left.position < right.position;
  return left.raw_score > right.raw_score;
}

int compare_ranked_match(const InnerHtmlSearchMatch& left, const InnerHtmlSearchMatch& right) {
  if (left.source_priority != right.source_priority) {
    return (left.source_priority > right.source_priority) ? -1 : 1;
  }
  if (left.quality_rank != right.quality_rank) {
    return (left.quality_rank > right.quality_rank) ? -1 : 1;
  }
  if (left.depth != right.depth) {
    return (left.depth > right.depth) ? -1 : 1;
  }
  if (left.position != right.position) {
    return (left.position < right.position) ? -1 : 1;
  }
  if (left.score != right.score) {
    return (left.score > right.score) ? -1 : 1;
  }
  if (left.node_id != right.node_id) {
    return (left.node_id < right.node_id) ? -1 : 1;
  }
  return 0;
}

std::vector<InnerHtmlSearchMatch> search_inner_html_impl(const xsql::HtmlDocument& doc,
                                                         const std::string& query,
                                                         size_t max_results,
                                                         bool include_snippet,
                                                         bool sort_results,
                                                         const std::vector<int64_t>* candidate_node_ids,
                                                         InnerHtmlSearchMode mode) {
  std::vector<InnerHtmlSearchMatch> matches;
  if (query.empty() || max_results == 0) return matches;

  std::vector<int> depth_cache(doc.nodes.size(), -1);
  auto depth_for_node = [&](int64_t node_id) {
    if (node_id < 0 || static_cast<size_t>(node_id) >= doc.nodes.size()) return 0;
    size_t idx = static_cast<size_t>(node_id);
    if (depth_cache[idx] >= 0) return depth_cache[idx];

    std::vector<size_t> chain;
    chain.reserve(16);
    int64_t cur = node_id;
    size_t guard = 0;
    while (cur >= 0 && static_cast<size_t>(cur) < doc.nodes.size() && guard < doc.nodes.size()) {
      size_t cur_idx = static_cast<size_t>(cur);
      if (depth_cache[cur_idx] >= 0) break;
      chain.push_back(cur_idx);
      const auto& node = doc.nodes[cur_idx];
      if (!node.parent_id.has_value() || *node.parent_id < 0 ||
          static_cast<size_t>(*node.parent_id) >= doc.nodes.size()) {
        cur = -1;
        break;
      }
      cur = *node.parent_id;
      ++guard;
    }

    int base_depth = 0;
    if (cur >= 0 && static_cast<size_t>(cur) < doc.nodes.size() &&
        depth_cache[static_cast<size_t>(cur)] >= 0) {
      base_depth = depth_cache[static_cast<size_t>(cur)] + 1;
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      depth_cache[*it] = base_depth;
      ++base_depth;
    }
    return depth_cache[idx] < 0 ? 0 : depth_cache[idx];
  };

  auto compute_signal = [&](std::string_view haystack, MatchSignal* out) {
    if (mode == InnerHtmlSearchMode::Fuzzy) {
      return fuzzy_match_signal(haystack, query, out);
    }
    return exact_match_signal(haystack, query, out);
  };

  size_t estimated_count = candidate_node_ids ? candidate_node_ids->size() : doc.nodes.size();
  matches.reserve(std::min(max_results, estimated_count));
  size_t best_idx = 0;

  auto consume_node = [&](const xsql::HtmlNode& node) {
    ScopeCandidate best_scope{};
    bool has_scope_match = false;

    auto consider = [&](std::string_view text, int source_priority, bool position_in_inner_html) {
      if (text.empty()) return;
      MatchSignal signal;
      if (!compute_signal(text, &signal)) return;

      ScopeCandidate candidate;
      candidate.source_priority = source_priority;
      candidate.position = signal.position;
      candidate.quality_rank = signal.quality_rank;
      candidate.raw_score = signal.raw_score;
      candidate.position_in_inner_html = position_in_inner_html;
      if (!position_in_inner_html && include_snippet) {
        candidate.snippet_source.assign(text.begin(), text.end());
      }

      if (!has_scope_match || is_better_scope_candidate(candidate, best_scope)) {
        best_scope = std::move(candidate);
        has_scope_match = true;
      }
    };

    if (!node.attributes.empty()) {
      std::vector<std::pair<std::string, std::string>> attrs(node.attributes.begin(), node.attributes.end());
      std::sort(attrs.begin(), attrs.end(),
                [](const auto& left, const auto& right) { return left.first < right.first; });
      for (const auto& [key, value] : attrs) {
        consider(key, kSourceSelfAttribute, false);
        consider(value, kSourceSelfAttribute, false);
      }
    }
    consider(node.tag, kSourceSelfTextOrTag, false);
    consider(node.text, kSourceSelfTextOrTag, false);
    consider(node.inner_html, kSourceDescendant, true);

    if (!has_scope_match) return;

    InnerHtmlSearchMatch match;
    match.node_id = node.id;
    match.score = best_scope.raw_score;
    match.source_priority = best_scope.source_priority;
    match.quality_rank = best_scope.quality_rank;
    match.depth = depth_for_node(node.id);
    match.position = best_scope.position;
    match.position_in_inner_html = best_scope.position_in_inner_html;
    if (include_snippet) {
      if (best_scope.position_in_inner_html) {
        match.snippet = build_match_snippet(node.inner_html, best_scope.position, query.size(), 160);
      } else {
        match.snippet =
            build_match_snippet(best_scope.snippet_source, best_scope.position, query.size(), 160);
      }
    }

    if (matches.empty() || compare_ranked_match(match, matches[best_idx]) < 0) {
      best_idx = matches.size();
    }
    matches.push_back(std::move(match));
  };

  if (candidate_node_ids == nullptr) {
    for (const auto& node : doc.nodes) {
      consume_node(node);
    }
  } else {
    for (int64_t node_id : *candidate_node_ids) {
      if (node_id < 0 || static_cast<size_t>(node_id) >= doc.nodes.size()) continue;
      consume_node(doc.nodes[static_cast<size_t>(node_id)]);
    }
  }

  if (sort_results) {
    std::sort(matches.begin(),
              matches.end(),
              [](const InnerHtmlSearchMatch& left, const InnerHtmlSearchMatch& right) {
                return compare_ranked_match(left, right) < 0;
              });
  } else if (!matches.empty() && best_idx < matches.size() && best_idx != 0) {
    std::swap(matches[0], matches[best_idx]);
  }
  if (matches.size() > max_results) {
    matches.resize(max_results);
  }
  return matches;
}

}  // namespace

bool fuzzy_match_score(std::string_view haystack,
                       std::string_view needle,
                       size_t* first_position,
                       int* score) {
  MatchSignal signal;
  if (!fuzzy_match_signal(haystack, needle, &signal)) return false;
  if (first_position) *first_position = signal.position;
  if (score) *score = signal.raw_score;
  return true;
}

std::string make_inner_html_snippet(std::string_view inner_html,
                                    size_t match_position,
                                    size_t query_len,
                                    size_t max_chars) {
  return build_match_snippet(inner_html, match_position, query_len, max_chars);
}

std::vector<InnerHtmlSearchMatch> fuzzy_search_inner_html(const xsql::HtmlDocument& doc,
                                                          const std::string& query,
                                                          size_t max_results,
                                                          bool include_snippet,
                                                          bool sort_results,
                                                          const std::vector<int64_t>* candidate_node_ids) {
  return search_inner_html_impl(doc,
                                query,
                                max_results,
                                include_snippet,
                                sort_results,
                                candidate_node_ids,
                                InnerHtmlSearchMode::Fuzzy);
}

std::vector<InnerHtmlSearchMatch> exact_search_inner_html(const xsql::HtmlDocument& doc,
                                                          const std::string& query,
                                                          size_t max_results,
                                                          bool include_snippet,
                                                          bool sort_results,
                                                          const std::vector<int64_t>* candidate_node_ids) {
  return search_inner_html_impl(doc,
                                query,
                                max_results,
                                include_snippet,
                                sort_results,
                                candidate_node_ids,
                                InnerHtmlSearchMode::Exact);
}

}  // namespace xsql::cli
