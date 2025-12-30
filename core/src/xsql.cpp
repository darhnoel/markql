#include "xsql/xsql.h"

#include <fstream>
#include <functional>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>

#include "executor.h"
#include "html_parser.h"
#include "query_parser.h"

#ifdef XSQL_USE_CURL
#include <curl/curl.h>
#endif

namespace xsql {

namespace {

std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path);
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

bool starts_with(const std::string& s, const std::string& prefix) {
  return s.rfind(prefix, 0) == 0;
}

std::string to_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

#ifdef XSQL_USE_CURL
size_t write_to_string(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  auto* out = static_cast<std::string*>(userp);
  out->append(static_cast<const char*>(contents), total);
  return total;
}

std::string fetch_url(const std::string& url, int timeout_ms) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("Failed to initialize curl");
  }
  std::string buffer;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "xsql/0.1");
  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  if (res != CURLE_OK) {
    throw std::runtime_error(std::string("Failed to fetch URL: ") + curl_easy_strerror(res));
  }
  return buffer;
}
#endif

}  // namespace

namespace {

bool has_non_tag_self_predicate(const Expr& expr);

bool is_projection_query(const Query& query) {
  for (const auto& item : query.select_items) {
    if (item.field.has_value() || item.aggregate != Query::SelectItem::Aggregate::None) return true;
  }
  return false;
}

bool is_summarize_query(const Query& query) {
  if (query.select_items.size() != 1) return false;
  return query.select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize;
}

bool has_wildcard_tag(const Query& query) {
  for (const auto& item : query.select_items) {
    if (item.tag == "*") return true;
  }
  return false;
}

bool is_wildcard_only(const Query& query) {
  return query.select_items.size() == 1 && query.select_items[0].tag == "*";
}

void validate_projection(const Query& query) {
  bool has_aggregate = false;
  bool has_summarize = false;
  bool has_trim = false;
  for (const auto& item : query.select_items) {
    if (item.aggregate != Query::SelectItem::Aggregate::None) {
      has_aggregate = true;
      if (item.aggregate == Query::SelectItem::Aggregate::Summarize) {
        has_summarize = true;
      }
      break;
    }
  }

  if (has_aggregate && !query.order_by.empty() && !has_summarize) {
    throw std::runtime_error("ORDER BY is not supported with aggregate queries");
  }

  if (!is_projection_query(query)) {
    if (!query.exclude_fields.empty() && !is_wildcard_only(query)) {
      throw std::runtime_error("EXCLUDE requires SELECT *");
    }
    if (!query.exclude_fields.empty()) {
      const std::vector<std::string> allowed = {"node_id", "tag", "attributes", "parent_id", "source_uri"};
      for (const auto& field : query.exclude_fields) {
        if (std::find(allowed.begin(), allowed.end(), field) == allowed.end()) {
          throw std::runtime_error("Unknown EXCLUDE field: " + field);
        }
      }
    }
    if (query.to_list) {
      throw std::runtime_error("TO LIST() requires a projected column");
    }
    if (has_wildcard_tag(query) && query.select_items.size() > 1) {
      throw std::runtime_error("SELECT * cannot be combined with other tags");
    }
    return;
  }
  if (query.to_table) {
    throw std::runtime_error("TO TABLE() cannot be used with projections");
  }
  if (has_aggregate) {
    if (query.select_items.size() != 1) {
      throw std::runtime_error("Aggregate queries require a single select item");
    }
    if (query.to_list) {
      throw std::runtime_error("Aggregate queries do not support TO LIST()");
    }
    if (query.select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize) {
      return;
    }
    if (query.select_items[0].aggregate != Query::SelectItem::Aggregate::Count) {
      throw std::runtime_error("Unsupported aggregate");
    }
    return;
  }
  if (query.to_list && query.select_items.size() != 1) {
    throw std::runtime_error("TO LIST() requires a single projected column");
  }
  for (const auto& item : query.select_items) {
    if (item.trim) {
      has_trim = true;
      break;
    }
  }
  bool has_text_function = false;
  bool has_inner_html_function = false;
  for (const auto& item : query.select_items) {
    if (item.text_function) has_text_function = true;
    if (item.inner_html_function) has_inner_html_function = true;
  }
  if ((has_text_function || has_inner_html_function) && !query.where.has_value()) {
    throw std::runtime_error("TEXT()/INNER_HTML() requires a WHERE clause");
  }
  if (has_text_function || has_inner_html_function) {
    if (!query.where.has_value() || !has_non_tag_self_predicate(*query.where)) {
      throw std::runtime_error("TEXT()/INNER_HTML() requires a non-tag filter (e.g., attributes or parent)");
    }
  }
  if (has_trim && query.select_items.size() != 1) {
    throw std::runtime_error("TRIM() requires a single projected column");
  }
  std::string tag;
  std::optional<size_t> inner_html_depth;
  for (const auto& item : query.select_items) {
    if (!item.field.has_value()) {
      throw std::runtime_error("Cannot mix tag-only and projected fields in SELECT");
    }
    if (tag.empty()) {
      tag = item.tag;
    } else if (tag != item.tag) {
      throw std::runtime_error("Projected fields must use a single tag");
    }
    const std::string& field = *item.field;
    if (field == "text" && !item.text_function) {
      throw std::runtime_error("TEXT() must be used to project text");
    }
    if (field == "inner_html" && !item.inner_html_function) {
      throw std::runtime_error("INNER_HTML() must be used to project inner_html");
    }
    if (item.trim && field == "attributes") {
      throw std::runtime_error("TRIM() does not support attributes");
    }
    if (field == "inner_html") {
      if (item.inner_html_depth.has_value()) {
        if (inner_html_depth.has_value() && *inner_html_depth != *item.inner_html_depth) {
          throw std::runtime_error("inner_html() depth must be consistent");
        }
        inner_html_depth = item.inner_html_depth;
      } else if (inner_html_depth.has_value()) {
        throw std::runtime_error("inner_html() depth must be consistent");
      }
    }
    if (field != "node_id" && field != "tag" && field != "text" &&
        field != "inner_html" && field != "parent_id" && field != "source_uri" && field != "attributes") {
      // Treat other fields as attribute projections (e.g., link.href).
    }
  }
}

void validate_qualifiers(const Query& query) {
  auto is_allowed = [&](const std::optional<std::string>& qualifier) -> bool {
    if (!qualifier.has_value()) return true;
    if (query.source.alias.has_value() && *qualifier == *query.source.alias) return true;
    if (query.source.kind == Source::Kind::Document && *qualifier == "document") return true;
    return false;
  };

  std::function<void(const Expr&)> visit = [&](const Expr& expr) {
    if (std::holds_alternative<CompareExpr>(expr)) {
      const auto& cmp = std::get<CompareExpr>(expr);
      if (!is_allowed(cmp.lhs.qualifier)) {
        throw std::runtime_error("Unknown qualifier: " + *cmp.lhs.qualifier);
      }
      return;
    }
    const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
    visit(bin.left);
    visit(bin.right);
  };

  if (query.where.has_value()) {
    visit(*query.where);
  }
}

void validate_predicates(const Query& query) {
  std::function<void(const Expr&)> visit = [&](const Expr& expr) {
    if (std::holds_alternative<CompareExpr>(expr)) {
      const auto& cmp = std::get<CompareExpr>(expr);
      if (cmp.lhs.field_kind == Operand::FieldKind::AttributesMap) {
        if (cmp.op != CompareExpr::Op::IsNull && cmp.op != CompareExpr::Op::IsNotNull) {
          throw std::runtime_error("attributes supports only IS NULL or IS NOT NULL");
        }
      }
      return;
    }
    const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
    visit(bin.left);
    visit(bin.right);
  };

  if (query.where.has_value()) {
    visit(*query.where);
  }
}

bool has_non_tag_self_predicate(const Expr& expr) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    const auto& cmp = std::get<CompareExpr>(expr);
    return !(cmp.lhs.axis == Operand::Axis::Self && cmp.lhs.field_kind == Operand::FieldKind::Tag);
  }
  const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  return has_non_tag_self_predicate(bin.left) || has_non_tag_self_predicate(bin.right);
}

void validate_order_by(const Query& query) {
  if (query.order_by.empty()) return;
  // Assumption: ORDER BY is limited to core node fields for now (no attributes/expressions).
  for (const auto& order_by : query.order_by) {
    const std::string& field = order_by.field;
    if (is_summarize_query(query)) {
      if (field != "tag" && field != "count") {
        throw std::runtime_error("ORDER BY supports tag or count for SUMMARIZE()");
      }
      continue;
    }
    if (field != "node_id" && field != "tag" && field != "text" && field != "parent_id") {
      throw std::runtime_error("ORDER BY supports node_id, tag, text, or parent_id");
    }
  }
}

void validate_to_table(const Query& query) {
  if (!query.to_table) return;
  if (query.to_list) {
    throw std::runtime_error("TO TABLE() cannot be combined with TO LIST()");
  }
  if (!query.select_items.empty()) {
    if (query.select_items.size() != 1) {
      throw std::runtime_error("TO TABLE() requires a single select item");
    }
    const auto& item = query.select_items[0];
    if (item.aggregate != Query::SelectItem::Aggregate::None || item.field.has_value()) {
      throw std::runtime_error("TO TABLE() requires a tag-only SELECT");
    }
    if (to_lower(item.tag) != "table") {
      throw std::runtime_error("TO TABLE() only supports SELECT table");
    }
  }
}

std::vector<std::string> build_columns(const Query& query) {
  for (const auto& item : query.select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      return {"count"};
    }
    if (item.aggregate == Query::SelectItem::Aggregate::Summarize) {
      return {"tag", "count"};
    }
  }
  if (!is_projection_query(query)) {
    std::vector<std::string> cols = {"node_id", "tag", "attributes", "parent_id", "source_uri"};
    if (!query.exclude_fields.empty()) {
      std::vector<std::string> out;
      out.reserve(cols.size());
      for (const auto& col : cols) {
        if (std::find(query.exclude_fields.begin(), query.exclude_fields.end(), col) ==
            query.exclude_fields.end()) {
          out.push_back(col);
        }
      }
      if (out.empty()) {
        throw std::runtime_error("EXCLUDE removes all columns");
      }
      return out;
    }
    return cols;
  }
  std::vector<std::string> cols;
  cols.reserve(query.select_items.size());
  for (const auto& item : query.select_items) {
    cols.push_back(*item.field);
  }
  return cols;
}

std::string trim_ws(const std::string& s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

bool is_name_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ':';
}

bool is_void_tag(const std::string& tag) {
  static const std::vector<std::string> kVoidTags = {
      "area", "base", "br",   "col",  "embed", "hr",    "img",   "input",
      "link", "meta", "param","source","track","wbr"};
  for (const auto& name : kVoidTags) {
    if (tag == name) return true;
  }
  return false;
}

size_t find_tag_end(const std::string& html, size_t start) {
  bool in_quote = false;
  char quote = '\0';
  for (size_t i = start; i < html.size(); ++i) {
    char c = html[i];
    if (in_quote) {
      if (c == quote) {
        in_quote = false;
      }
      continue;
    }
    if (c == '"' || c == '\'') {
      in_quote = true;
      quote = c;
      continue;
    }
    if (c == '>') return i;
  }
  return std::string::npos;
}

std::string limit_inner_html(const std::string& html, size_t max_depth) {
  std::string out;
  out.reserve(html.size());
  size_t i = 0;
  int depth = 0;
  while (i < html.size()) {
    if (html[i] == '<') {
      if (html.compare(i, 4, "<!--") == 0) {
        size_t end = html.find("-->", i + 4);
        size_t stop = (end == std::string::npos) ? html.size() : end + 3;
        if (depth <= static_cast<int>(max_depth)) {
          out.append(html, i, stop - i);
        }
        i = stop;
        continue;
      }
      bool is_end = (i + 1 < html.size() && html[i + 1] == '/');
      size_t tag_end = find_tag_end(html, i + 1);
      if (tag_end == std::string::npos) {
        out.append(html, i, html.size() - i);
        break;
      }

      size_t name_start = i + (is_end ? 2 : 1);
      while (name_start < tag_end && std::isspace(static_cast<unsigned char>(html[name_start]))) {
        ++name_start;
      }
      size_t name_end = name_start;
      while (name_end < tag_end && is_name_char(html[name_end])) {
        ++name_end;
      }
      std::string tag = to_lower(html.substr(name_start, name_end - name_start));

      bool self_closing = false;
      if (!is_end) {
        size_t j = tag_end;
        while (j > i && std::isspace(static_cast<unsigned char>(html[j - 1]))) {
          --j;
        }
        if (j > i && html[j - 1] == '/') {
          self_closing = true;
        }
        if (is_void_tag(tag)) {
          self_closing = true;
        }
      }

      if (!is_end) {
        int element_depth = depth + 1;
        if (element_depth <= static_cast<int>(max_depth)) {
          out.append(html, i, tag_end - i + 1);
        }
        if (!self_closing) {
          depth++;
        }
      } else {
        if (depth <= static_cast<int>(max_depth)) {
          out.append(html, i, tag_end - i + 1);
        }
        if (depth > 0) {
          depth--;
        }
      }
      i = tag_end + 1;
      continue;
    }
    out.push_back(html[i++]);
  }
  return out;
}

std::vector<std::vector<int64_t>> build_children(const HtmlDocument& doc) {
  std::vector<std::vector<int64_t>> children(doc.nodes.size());
  for (const auto& node : doc.nodes) {
    if (node.parent_id.has_value()) {
      children.at(static_cast<size_t>(*node.parent_id)).push_back(node.id);
    }
  }
  return children;
}

std::optional<size_t> find_inner_html_depth(const Query& query) {
  for (const auto& item : query.select_items) {
    if (!item.field.has_value() || *item.field != "inner_html") continue;
    if (item.inner_html_depth.has_value()) return item.inner_html_depth;
  }
  return std::nullopt;
}

const Query::SelectItem* find_trim_item(const Query& query) {
  if (query.select_items.size() != 1) return nullptr;
  const auto& item = query.select_items[0];
  if (!item.trim) return nullptr;
  return &item;
}

void collect_rows(const HtmlDocument& doc,
                  const std::vector<std::vector<int64_t>>& children,
                  int64_t table_id,
                  std::vector<std::vector<std::string>>& out_rows) {
  std::vector<int64_t> stack;
  stack.push_back(table_id);
  std::vector<int64_t> tr_nodes;
  while (!stack.empty()) {
    int64_t id = stack.back();
    stack.pop_back();
    const HtmlNode& node = doc.nodes.at(static_cast<size_t>(id));
    if (node.tag == "tr") {
      tr_nodes.push_back(id);
      continue;
    }
    const auto& kids = children.at(static_cast<size_t>(id));
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
      stack.push_back(*it);
    }
  }

  for (int64_t tr_id : tr_nodes) {
    std::vector<std::string> row;
    std::vector<int64_t> cell_stack;
    const auto& kids = children.at(static_cast<size_t>(tr_id));
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
      cell_stack.push_back(*it);
    }
    while (!cell_stack.empty()) {
      int64_t id = cell_stack.back();
      cell_stack.pop_back();
      const HtmlNode& node = doc.nodes.at(static_cast<size_t>(id));
      if (node.tag == "td" || node.tag == "th") {
        row.push_back(trim_ws(node.text));
        continue;
      }
      const auto& next = children.at(static_cast<size_t>(id));
      for (auto it = next.rbegin(); it != next.rend(); ++it) {
        cell_stack.push_back(*it);
      }
    }
    if (!row.empty()) {
      out_rows.push_back(row);
    }
  }
}

}  // namespace

QueryResult execute_query_from_document(const std::string& html, const std::string& query) {
  auto parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    throw std::runtime_error("Query parse error: " + parsed.error->message);
  }
  validate_projection(*parsed.query);
  validate_order_by(*parsed.query);
  validate_to_table(*parsed.query);
  validate_qualifiers(*parsed.query);
  validate_predicates(*parsed.query);
  auto inner_html_depth = find_inner_html_depth(*parsed.query);
  const Query::SelectItem* trim_item = find_trim_item(*parsed.query);

  HtmlDocument doc = parse_html(html);
  ExecuteResult exec = execute_query(*parsed.query, doc, "document");
  QueryResult out;
  out.columns = build_columns(*parsed.query);
  out.to_list = parsed.query->to_list;
  out.to_table = parsed.query->to_table;
  if (!parsed.query->select_items.empty() &&
      parsed.query->select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize) {
    std::unordered_map<std::string, size_t> counts;
    for (const auto& node : exec.nodes) {
      ++counts[node.tag];
    }
    std::vector<std::pair<std::string, size_t>> summary;
    summary.reserve(counts.size());
    for (const auto& kv : counts) {
      summary.emplace_back(kv.first, kv.second);
    }
    if (!parsed.query->order_by.empty()) {
      std::sort(summary.begin(), summary.end(),
                [&](const auto& a, const auto& b) {
                  for (const auto& order_by : parsed.query->order_by) {
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
    if (parsed.query->limit.has_value() && summary.size() > *parsed.query->limit) {
      summary.resize(*parsed.query->limit);
    }
    for (const auto& item : summary) {
      QueryResultRow row;
      row.tag = item.first;
      row.node_id = static_cast<int64_t>(item.second);
      row.source_uri = "document";
      out.rows.push_back(std::move(row));
    }
    return out;
  }
  if (parsed.query->to_table) {
    auto children = build_children(doc);
    for (const auto& node : exec.nodes) {
      QueryResult::TableResult table;
      table.node_id = node.id;
      collect_rows(doc, children, node.id, table.rows);
      out.tables.push_back(std::move(table));
    }
    return out;
  }
  for (const auto& item : parsed.query->select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      QueryResultRow row;
      row.node_id = static_cast<int64_t>(exec.nodes.size());
      row.source_uri = "document";
      out.rows.push_back(row);
      return out;
    }
  }
  for (const auto& node : exec.nodes) {
    QueryResultRow row;
    row.node_id = node.id;
    row.tag = node.tag;
    row.text = node.text;
    row.inner_html = inner_html_depth.has_value() ? limit_inner_html(node.inner_html, *inner_html_depth)
                                                  : node.inner_html;
    row.attributes = node.attributes;
    row.source_uri = "document";
    if (trim_item != nullptr && trim_item->field.has_value()) {
      const std::string& field = *trim_item->field;
      if (field == "text") {
        row.text = trim_ws(row.text);
      } else if (field == "inner_html") {
        row.inner_html = trim_ws(row.inner_html);
      } else if (field == "tag") {
        row.tag = trim_ws(row.tag);
      } else if (field == "source_uri") {
        row.source_uri = trim_ws(row.source_uri);
      } else {
        auto it = row.attributes.find(field);
        if (it != row.attributes.end()) {
          it->second = trim_ws(it->second);
        }
      }
    }
    row.parent_id = node.parent_id;
    out.rows.push_back(row);
  }
  return out;
}

QueryResult execute_query_from_file(const std::string& path, const std::string& query) {
  std::string html = read_file(path);
  auto parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    throw std::runtime_error("Query parse error: " + parsed.error->message);
  }
  validate_projection(*parsed.query);
  validate_order_by(*parsed.query);
  validate_to_table(*parsed.query);
  validate_qualifiers(*parsed.query);
  validate_predicates(*parsed.query);
  auto inner_html_depth = find_inner_html_depth(*parsed.query);
  const Query::SelectItem* trim_item = find_trim_item(*parsed.query);

  HtmlDocument doc = parse_html(html);
  ExecuteResult exec = execute_query(*parsed.query, doc, path);
  QueryResult out;
  out.columns = build_columns(*parsed.query);
  out.to_list = parsed.query->to_list;
  out.to_table = parsed.query->to_table;
  if (!parsed.query->select_items.empty() &&
      parsed.query->select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize) {
    std::unordered_map<std::string, size_t> counts;
    for (const auto& node : exec.nodes) {
      ++counts[node.tag];
    }
    std::vector<std::pair<std::string, size_t>> summary;
    summary.reserve(counts.size());
    for (const auto& kv : counts) {
      summary.emplace_back(kv.first, kv.second);
    }
    if (!parsed.query->order_by.empty()) {
      std::sort(summary.begin(), summary.end(),
                [&](const auto& a, const auto& b) {
                  for (const auto& order_by : parsed.query->order_by) {
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
    if (parsed.query->limit.has_value() && summary.size() > *parsed.query->limit) {
      summary.resize(*parsed.query->limit);
    }
    for (const auto& item : summary) {
      QueryResultRow row;
      row.tag = item.first;
      row.node_id = static_cast<int64_t>(item.second);
      row.source_uri = path;
      out.rows.push_back(std::move(row));
    }
    return out;
  }
  if (parsed.query->to_table) {
    auto children = build_children(doc);
    for (const auto& node : exec.nodes) {
      QueryResult::TableResult table;
      table.node_id = node.id;
      collect_rows(doc, children, node.id, table.rows);
      out.tables.push_back(std::move(table));
    }
    return out;
  }
  for (const auto& item : parsed.query->select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      QueryResultRow row;
      row.node_id = static_cast<int64_t>(exec.nodes.size());
      row.source_uri = path;
      out.rows.push_back(row);
      return out;
    }
  }
  for (const auto& node : exec.nodes) {
    QueryResultRow row;
    row.node_id = node.id;
    row.tag = node.tag;
    row.text = node.text;
    row.inner_html = inner_html_depth.has_value() ? limit_inner_html(node.inner_html, *inner_html_depth)
                                                  : node.inner_html;
    row.attributes = node.attributes;
    row.source_uri = path;
    if (trim_item != nullptr && trim_item->field.has_value()) {
      const std::string& field = *trim_item->field;
      if (field == "text") {
        row.text = trim_ws(row.text);
      } else if (field == "inner_html") {
        row.inner_html = trim_ws(row.inner_html);
      } else if (field == "tag") {
        row.tag = trim_ws(row.tag);
      } else if (field == "source_uri") {
        row.source_uri = trim_ws(row.source_uri);
      } else {
        auto it = row.attributes.find(field);
        if (it != row.attributes.end()) {
          it->second = trim_ws(it->second);
        }
      }
    }
    row.parent_id = node.parent_id;
    out.rows.push_back(row);
  }
  return out;
}

QueryResult execute_query_from_url(const std::string& url, const std::string& query, int timeout_ms) {
#ifdef XSQL_USE_CURL
  std::string html = fetch_url(url, timeout_ms);
  auto parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    throw std::runtime_error("Query parse error: " + parsed.error->message);
  }
  validate_projection(*parsed.query);
  validate_order_by(*parsed.query);
  validate_to_table(*parsed.query);
  validate_qualifiers(*parsed.query);
  validate_predicates(*parsed.query);
  auto inner_html_depth = find_inner_html_depth(*parsed.query);
  const Query::SelectItem* trim_item = find_trim_item(*parsed.query);

  HtmlDocument doc = parse_html(html);
  ExecuteResult exec = execute_query(*parsed.query, doc, url);
  QueryResult out;
  out.columns = build_columns(*parsed.query);
  out.to_list = parsed.query->to_list;
  out.to_table = parsed.query->to_table;
  if (!parsed.query->select_items.empty() &&
      parsed.query->select_items[0].aggregate == Query::SelectItem::Aggregate::Summarize) {
    std::unordered_map<std::string, size_t> counts;
    for (const auto& node : exec.nodes) {
      ++counts[node.tag];
    }
    std::vector<std::pair<std::string, size_t>> summary;
    summary.reserve(counts.size());
    for (const auto& kv : counts) {
      summary.emplace_back(kv.first, kv.second);
    }
    if (!parsed.query->order_by.empty()) {
      std::sort(summary.begin(), summary.end(),
                [&](const auto& a, const auto& b) {
                  for (const auto& order_by : parsed.query->order_by) {
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
    if (parsed.query->limit.has_value() && summary.size() > *parsed.query->limit) {
      summary.resize(*parsed.query->limit);
    }
    for (const auto& item : summary) {
      QueryResultRow row;
      row.tag = item.first;
      row.node_id = static_cast<int64_t>(item.second);
      row.source_uri = url;
      out.rows.push_back(std::move(row));
    }
    return out;
  }
  if (parsed.query->to_table) {
    auto children = build_children(doc);
    for (const auto& node : exec.nodes) {
      QueryResult::TableResult table;
      table.node_id = node.id;
      collect_rows(doc, children, node.id, table.rows);
      out.tables.push_back(std::move(table));
    }
    return out;
  }
  for (const auto& item : parsed.query->select_items) {
    if (item.aggregate == Query::SelectItem::Aggregate::Count) {
      QueryResultRow row;
      row.node_id = static_cast<int64_t>(exec.nodes.size());
      row.source_uri = url;
      out.rows.push_back(row);
      return out;
    }
  }
  for (const auto& node : exec.nodes) {
    QueryResultRow row;
    row.node_id = node.id;
    row.tag = node.tag;
    row.text = node.text;
    row.inner_html = inner_html_depth.has_value() ? limit_inner_html(node.inner_html, *inner_html_depth)
                                                  : node.inner_html;
    row.attributes = node.attributes;
    row.source_uri = url;
    if (trim_item != nullptr && trim_item->field.has_value()) {
      const std::string& field = *trim_item->field;
      if (field == "text") {
        row.text = trim_ws(row.text);
      } else if (field == "inner_html") {
        row.inner_html = trim_ws(row.inner_html);
      } else if (field == "tag") {
        row.tag = trim_ws(row.tag);
      } else if (field == "source_uri") {
        row.source_uri = trim_ws(row.source_uri);
      } else {
        auto it = row.attributes.find(field);
        if (it != row.attributes.end()) {
          it->second = trim_ws(it->second);
        }
      }
    }
    row.parent_id = node.parent_id;
    out.rows.push_back(row);
  }
  return out;
#else
  (void)url;
  (void)query;
  (void)timeout_ms;
  throw std::runtime_error("URL fetching is disabled (libcurl not available)");
#endif
}

}  // namespace xsql
