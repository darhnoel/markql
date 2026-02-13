#include "explore/inner_html_search.h"

#include <algorithm>
#include <cctype>

namespace xsql::cli {

namespace {

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

}  // namespace

bool fuzzy_match_score(std::string_view haystack,
                       std::string_view needle,
                       size_t* first_position,
                       int* score) {
  if (needle.empty() || haystack.empty()) return false;
  std::string lower_hay = to_lower_ascii(haystack);
  std::string lower_needle = to_lower_ascii(needle);

  size_t first = std::string::npos;
  size_t last = 0;
  size_t cursor = 0;
  for (char c : lower_needle) {
    size_t pos = lower_hay.find(c, cursor);
    if (pos == std::string::npos) return false;
    if (first == std::string::npos) first = pos;
    last = pos;
    cursor = pos + 1;
  }

  size_t span = (last >= first) ? (last - first + 1) : 0;
  int out_score = 100000 - static_cast<int>(span * 100) - static_cast<int>(first);

  bool contiguous_found = false;
  size_t contiguous_best_pos = 0;
  bool contiguous_word_start = false;
  bool contiguous_whole_word = false;
  int contiguous_best_rank = -1;
  size_t search_from = 0;
  while (search_from <= lower_hay.size()) {
    size_t pos = lower_hay.find(lower_needle, search_from);
    if (pos == std::string::npos) break;
    bool before_ok = (pos == 0) || !is_word_char(haystack[pos - 1]);
    size_t end_pos = pos + lower_needle.size();
    bool after_ok = (end_pos >= haystack.size()) || !is_word_char(haystack[end_pos]);
    bool word_start = before_ok;
    bool whole_word = before_ok && after_ok;
    int rank = (whole_word ? 2 : 0) + (word_start ? 1 : 0);
    if (!contiguous_found || rank > contiguous_best_rank ||
        (rank == contiguous_best_rank && pos < contiguous_best_pos)) {
      contiguous_found = true;
      contiguous_best_pos = pos;
      contiguous_word_start = word_start;
      contiguous_whole_word = whole_word;
      contiguous_best_rank = rank;
      if (rank == 3 && pos == 0) break;
    }
    search_from = pos + 1;
  }
  if (contiguous_found) {
    first = contiguous_best_pos;
    out_score += 50000;
    if (contiguous_word_start) out_score += 20000;
    if (contiguous_whole_word) out_score += 40000;
  }

  if (first_position) *first_position = first;
  if (score) *score = out_score;
  return true;
}

std::string make_inner_html_snippet(std::string_view inner_html,
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

std::vector<InnerHtmlSearchMatch> fuzzy_search_inner_html(const xsql::HtmlDocument& doc,
                                                          const std::string& query,
                                                          size_t max_results,
                                                          bool include_snippet,
                                                          bool sort_results,
                                                          const std::vector<int64_t>* candidate_node_ids) {
  std::vector<InnerHtmlSearchMatch> matches;
  if (query.empty() || max_results == 0) return matches;

  size_t estimated_count = candidate_node_ids ? candidate_node_ids->size() : doc.nodes.size();
  matches.reserve(std::min(max_results, estimated_count));
  size_t best_idx = 0;
  auto consume_node = [&](const xsql::HtmlNode& node) {
    if (node.inner_html.empty()) return;
    size_t first = 0;
    int score = 0;
    if (!fuzzy_match_score(node.inner_html, query, &first, &score)) return;
    InnerHtmlSearchMatch match;
    match.node_id = node.id;
    match.score = score;
    match.position = first;
    if (include_snippet) {
      match.snippet = make_inner_html_snippet(node.inner_html, first, query.size(), 160);
    }
    if (matches.empty() || score > matches[best_idx].score ||
        (score == matches[best_idx].score && node.id < matches[best_idx].node_id)) {
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
    std::sort(matches.begin(), matches.end(),
              [](const InnerHtmlSearchMatch& left, const InnerHtmlSearchMatch& right) {
                if (left.score != right.score) return left.score > right.score;
                return left.node_id < right.node_id;
              });
  } else if (!matches.empty() && best_idx < matches.size() && best_idx != 0) {
    std::swap(matches[0], matches[best_idx]);
  }
  if (matches.size() > max_results) {
    matches.resize(max_results);
  }
  return matches;
}

}  // namespace xsql::cli
