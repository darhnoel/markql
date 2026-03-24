#include "cli_utils.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#ifdef __linux__
#include <unistd.h>
#endif

#include "lang/markql_parser.h"
#include "lang/parser/lexer.h"
#include "render/duckbox_renderer.h"
#include "runtime/engine/markql_internal.h"
#include "ui/color.h"

#ifdef MARKQL_USE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
#endif
namespace markql::cli {

std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path);
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string read_stdin() {
  std::ostringstream buffer;
  buffer << std::cin.rdbuf();
  return buffer.str();
}

std::string trim_semicolon(std::string value) {
  while (!value.empty() &&
         (value.back() == ';' || std::isspace(static_cast<unsigned char>(value.back())))) {
    value.pop_back();
  }
  return value;
}

std::string colorize_json(const std::string& input, bool enable) {
  if (!enable) return input;
  std::string out;
  out.reserve(input.size() * 2);
  bool in_string = false;
  bool escape = false;
  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        in_string = false;
        out += '"';
        out += kColor.reset;
        continue;
      }
      out += c;
      continue;
    }

    if (c == '"') {
      in_string = true;
      out += kColor.green;
      out += '"';
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
      out += kColor.cyan;
      while (i < input.size() &&
             (std::isdigit(static_cast<unsigned char>(input[i])) || input[i] == '.' ||
              input[i] == '-' || input[i] == 'e' || input[i] == 'E' || input[i] == '+')) {
        out += input[i++];
      }
      --i;
      out += kColor.reset;
      continue;
    }

    if (input.compare(i, 4, "true") == 0 || input.compare(i, 5, "false") == 0) {
      size_t len = input.compare(i, 4, "true") == 0 ? 4 : 5;
      out += kColor.yellow;
      out.append(input, i, len);
      out += kColor.reset;
      i += len - 1;
      continue;
    }

    if (input.compare(i, 4, "null") == 0) {
      out += kColor.magenta;
      out.append(input, i, 4);
      out += kColor.reset;
      i += 3;
      continue;
    }

    if (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',') {
      out += kColor.dim;
      out += c;
      out += kColor.reset;
      continue;
    }

    out += c;
  }
  return out;
}

TruncateResult truncate_output(const std::string& text, size_t head_lines, size_t tail_lines) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(text.substr(start));
      break;
    }
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }

  if (lines.size() <= head_lines + tail_lines) {
    // WHY: avoid truncation when the output already fits within the window.
    return {text, false};
  }

  std::ostringstream oss;
  for (size_t i = 0; i < head_lines; ++i) {
    oss << lines[i] << "\n";
  }
  oss << "... (abbreviated; use .display_mode more or --display_mode more, or redirect to a file) "
         "...\n";
  for (size_t i = lines.size() - tail_lines; i < lines.size(); ++i) {
    oss << lines[i];
    if (i + 1 < lines.size()) {
      oss << "\n";
    }
  }
  return {oss.str(), true};
}

bool is_url(const std::string& value) {
  return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

std::string load_html_input(const std::string& input, int timeout_ms) {
  if (is_url(input)) {
    return markql::markql_internal::fetch_url(input, timeout_ms);
  }
  return read_file(input);
}

std::optional<size_t> read_process_rss_bytes() {
#ifdef __linux__
  std::ifstream statm("/proc/self/statm");
  if (!statm.is_open()) return std::nullopt;
  unsigned long ignored_pages = 0;
  unsigned long resident_pages = 0;
  if (!(statm >> ignored_pages >> resident_pages)) {
    return std::nullopt;
  }
  long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) return std::nullopt;
  return static_cast<size_t>(resident_pages) * static_cast<size_t>(page_size);
#else
  return std::nullopt;
#endif
}

std::string format_bytes_iec(size_t bytes) {
  static const char* kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  double value = static_cast<double>(bytes);
  size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < (sizeof(kUnits) / sizeof(kUnits[0]))) {
    value /= 1024.0;
    ++unit;
  }
  std::ostringstream oss;
  if (unit == 0) {
    oss << bytes << " " << kUnits[unit];
  } else {
    oss << std::fixed << std::setprecision(2) << value << " " << kUnits[unit];
  }
  return oss.str();
}

void print_query_runtime_summary(std::optional<size_t> rss_before_bytes,
                                 std::optional<size_t> rss_after_bytes, long long elapsed_ms) {
  std::cout << "Time: " << elapsed_ms << " ms" << std::endl;
  if (!rss_after_bytes.has_value()) {
    std::cout << "Memory (RSS): n/a" << std::endl;
    return;
  }
  std::cout << "Memory (RSS): " << format_bytes_iec(*rss_after_bytes);
  if (rss_before_bytes.has_value()) {
    if (*rss_after_bytes >= *rss_before_bytes) {
      std::cout << " (delta +" << format_bytes_iec(*rss_after_bytes - *rss_before_bytes) << ")";
    } else {
      std::cout << " (delta -" << format_bytes_iec(*rss_before_bytes - *rss_after_bytes) << ")";
    }
  }
  std::cout << std::endl;
}

std::string rewrite_from_path_if_needed(const std::string& query) {
  std::string lower = query;
  for (char& c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  size_t pos = 0;
  while (true) {
    pos = lower.find("from", pos);
    if (pos == std::string::npos) return query;
    bool left_ok = (pos == 0) || std::isspace(static_cast<unsigned char>(lower[pos - 1]));
    bool right_ok =
        (pos + 4 >= lower.size()) || std::isspace(static_cast<unsigned char>(lower[pos + 4]));
    if (!left_ok || !right_ok) {
      pos += 4;
      continue;
    }
    size_t i = pos + 4;
    while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i]))) {
      ++i;
    }
    if (i >= query.size()) return query;
    char first = query[i];
    if (first == '\'' || first == '"') return query;
    size_t start = i;
    while (i < query.size() && !std::isspace(static_cast<unsigned char>(query[i])) &&
           query[i] != ';') {
      ++i;
    }
    if (start == i) return query;
    std::string token = query.substr(start, i - start);
    if (token.find('(') != std::string::npos || token.find(')') != std::string::npos) {
      return query;
    }
    bool looks_like_path = token.find('/') != std::string::npos ||
                           token.find('.') != std::string::npos ||
                           (!token.empty() && (token[0] == '.' || token[0] == '~'));
    if (!looks_like_path) return query;
    std::string rewritten = query.substr(0, start);
    rewritten.push_back('\'');
    rewritten += token;
    rewritten.push_back('\'');
    rewritten += query.substr(i);
    return rewritten;
  }
}

std::string sanitize_pasted_line(std::string line) {
  std::string cleaned;
  size_t start = 0;
  while (start <= line.size()) {
    size_t end = line.find('\n', start);
    std::string chunk =
        (end == std::string::npos) ? line.substr(start) : line.substr(start, end - start);
    if (!chunk.empty() && chunk.back() == '\r') {
      chunk.pop_back();
    }
    if (chunk.rfind("markql> ", 0) == 0) {
      chunk = chunk.substr(8);
    } else if (chunk.rfind("markql> ", 0) == 0) {
      chunk = chunk.substr(6);
    } else if (chunk.rfind("> ", 0) == 0) {
      chunk = chunk.substr(2);
    }
    cleaned += chunk;
    if (end == std::string::npos) {
      break;
    }
    cleaned.push_back('\n');
    start = end + 1;
  }
  while (!cleaned.empty() && std::isspace(static_cast<unsigned char>(cleaned.front()))) {
    cleaned.erase(cleaned.begin());
  }
  while (!cleaned.empty() && std::isspace(static_cast<unsigned char>(cleaned.back()))) {
    cleaned.pop_back();
  }
  return cleaned;
}

std::optional<QuerySource> parse_query_source(const std::string& query) {
  std::string cleaned = trim_semicolon(query);
  auto parsed = markql::parse_query(cleaned);
  if (!parsed.query.has_value()) return std::nullopt;

  auto lower = [](const std::string& in) {
    std::string out = in;
    for (char& c : out) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
  };

  auto trim_ws = [](std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
      text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
      text.pop_back();
    }
    return text;
  };

  auto source_needs_input = [&](const markql::Query& owner, const markql::Source& source,
                                const auto& self_source, const auto& self_query) -> bool {
    if (source.kind == markql::Source::Kind::RawHtml) return false;
    if (source.kind == markql::Source::Kind::Path || source.kind == markql::Source::Kind::Url)
      return false;
    if (source.kind == markql::Source::Kind::DerivedSubquery) {
      if (source.derived_query != nullptr)
        return self_query(*source.derived_query, self_source, self_query);
      return true;
    }
    if (source.kind == markql::Source::Kind::CteRef) {
      if (owner.with.has_value()) {
        const std::string target = lower(source.value);
        for (const auto& cte : owner.with->ctes) {
          if (lower(cte.name) != target || cte.query == nullptr) continue;
          return self_query(*cte.query, self_source, self_query);
        }
      }
      return true;
    }
    if (source.kind == markql::Source::Kind::Fragments) {
      if (source.fragments_raw.has_value()) return false;
      if (source.fragments_query != nullptr)
        return self_query(*source.fragments_query, self_source, self_query);
      return true;
    }
    if (source.kind == markql::Source::Kind::Parse) {
      if (source.parse_expr != nullptr) return false;
      if (source.parse_query != nullptr)
        return self_query(*source.parse_query, self_source, self_query);
      return true;
    }
    return true;
  };

  auto query_needs_input = [&](const markql::Query& q, const auto& self_source,
                               const auto& self_query) -> bool {
    bool needs_input = source_needs_input(q, q.source, self_source, self_query);
    if (q.with.has_value()) {
      for (const auto& cte : q.with->ctes) {
        if (cte.query == nullptr) continue;
        needs_input = needs_input || self_query(*cte.query, self_source, self_query);
      }
    }
    return needs_input;
  };

  QuerySource source;
  source.kind = parsed.query->source.kind;
  source.value = parsed.query->source.value;
  if (parsed.query->source.span.end > parsed.query->source.span.start &&
      parsed.query->source.span.end <= cleaned.size()) {
    source.source_token = lower(
        trim_ws(cleaned.substr(parsed.query->source.span.start,
                               parsed.query->source.span.end - parsed.query->source.span.start)));
  }
  if (parsed.query->source.kind == markql::Source::Kind::Document) {
    if (source.source_token.has_value() &&
        (*source.source_token == "doc" || *source.source_token == "document")) {
      // WHY: `FROM doc [AS x]` should still resolve to the default document input.
      source.alias = "doc";
    } else {
      source.alias = parsed.query->source.alias;
    }
  } else {
    source.alias.reset();
  }
  source.statement_kind = parsed.query->kind;
  source.needs_input = query_needs_input(*parsed.query, source_needs_input, query_needs_input);
  return source;
}

LexInspection inspect_sql_input(const std::string& query) {
  LexInspection out;
  markql::Lexer lexer(query);
  Token token = lexer.next();
  if (token.type == TokenType::Invalid) {
    out.has_error = true;
    out.error_message = token.text;
    out.error_position = token.pos;
    return out;
  }
  out.empty_after_comments = token.type == TokenType::End;
  return out;
}

std::pair<size_t, size_t> line_col_from_offset(const std::string& text, size_t offset) {
  size_t clamped = std::min(offset, text.size());
  size_t line = 1;
  size_t col = 1;
  for (size_t i = 0; i < clamped; ++i) {
    if (text[i] == '\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
  }
  return {line, col};
}

bool is_valid_utf8(const std::string& text) {
  size_t i = 0;
  while (i < text.size()) {
    unsigned char lead = static_cast<unsigned char>(text[i]);
    if ((lead & 0x80) == 0) {
      ++i;
      continue;
    }

    size_t len = 0;
    uint32_t cp = 0;
    uint32_t min_cp = 0;
    if ((lead & 0xE0) == 0xC0) {
      len = 2;
      cp = lead & 0x1F;
      min_cp = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
      len = 3;
      cp = lead & 0x0F;
      min_cp = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
      len = 4;
      cp = lead & 0x07;
      min_cp = 0x10000;
    } else {
      return false;
    }
    if (i + len > text.size()) return false;
    for (size_t j = 1; j < len; ++j) {
      unsigned char c = static_cast<unsigned char>(text[i + j]);
      if ((c & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (c & 0x3F);
    }
    if (cp < min_cp) return false;
    if (cp > 0x10FFFF) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    i += len;
  }
  return true;
}

std::string escape_control_for_terminal(const std::string& text) {
  std::ostringstream out;
  out << std::uppercase << std::hex;
  for (unsigned char ch : text) {
    if (ch == '\n') {
      out << "\\n";
    } else if (ch == '\r') {
      out << "\\r";
    } else if (ch == '\t') {
      out << "\\t";
    } else if (ch < 0x20 || ch == 0x7F) {
      out << "\\x" << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
      out << std::setfill(' ');
    } else {
      out << static_cast<char>(ch);
    }
  }
  return out.str();
}

std::vector<std::string> collect_source_uris(const markql::QueryResult& result) {
  std::vector<std::string> sources;
  std::unordered_set<std::string> seen;
  for (const auto& row : result.rows) {
    if (seen.insert(row.source_uri).second) {
      sources.push_back(row.source_uri);
    }
  }
  return sources;
}

void apply_source_uri_policy(markql::QueryResult& result, const std::vector<std::string>& sources) {
  if (!result.columns_implicit || result.source_uri_excluded) {
    return;
  }
  if (result.to_list || result.to_table) {
    return;
  }
  if (sources.size() <= 1) {
    return;
  }
  if (std::find(result.columns.begin(), result.columns.end(), "source_uri") !=
      result.columns.end()) {
    return;
  }
  result.columns.insert(result.columns.begin(), "source_uri");
}

size_t count_table_rows(const markql::QueryResult::TableResult& table, bool has_header) {
  if (table.rows.empty()) {
    return 0;
  }
  if (!has_header) {
    return table.rows.size();
  }
  return table.rows.size() > 0 ? table.rows.size() - 1 : 0;
}

size_t count_result_rows(const markql::QueryResult& result) {
  return result.rows.size();
}

bool build_show_input_result(const std::string& source_uri, markql::QueryResult& out,
                             std::string& error) {
  if (source_uri.empty()) {
    error = "No input loaded. Use .load <path|url> or --input <path|url>.";
    return false;
  }
  out.columns = {"key", "value"};
  markql::QueryResultRow row;
  row.attributes["key"] = "source_uri";
  row.attributes["value"] = source_uri;
  out.rows.push_back(std::move(row));
  return true;
}

bool build_show_inputs_result(const std::vector<std::string>& sources,
                              const std::string& fallback_source, markql::QueryResult& out,
                              std::string& error) {
  std::vector<std::string> effective = sources;
  if (effective.empty() && !fallback_source.empty()) {
    effective.push_back(fallback_source);
  }
  if (effective.empty()) {
    error = "No input loaded. Use .load <path|url> or --input <path|url>.";
    return false;
  }
  out.columns = {"source_uri"};
  for (const auto& source : effective) {
    markql::QueryResultRow row;
    row.source_uri = source;
    out.rows.push_back(std::move(row));
  }
  return true;
}

std::string render_table_duckbox(const markql::QueryResult::TableResult& table, bool has_header,
                                 bool highlight, bool is_tty, size_t max_rows) {
  size_t max_cols = 0;
  for (const auto& row : table.rows) {
    if (row.size() > max_cols) {
      max_cols = row.size();
    }
  }
  if (max_cols == 0) {
    return "(empty table)";
  }
  markql::QueryResult view;
  size_t data_start = 0;
  std::vector<std::string> column_keys;
  column_keys.reserve(max_cols);
  if (!table.rows.empty() && has_header) {
    std::vector<std::string> headers = table.rows.front();
    data_start = 1;
    if (headers.size() < max_cols) {
      headers.resize(max_cols);
    }
    std::unordered_map<std::string, int> seen;
    for (size_t i = 0; i < max_cols; ++i) {
      std::string name = headers[i];
      if (name.empty()) {
        name = "col" + std::to_string(i + 1);
      }
      auto& count = seen[name];
      std::string key = name;
      if (count > 0) {
        key = name + "_" + std::to_string(count + 1);
      }
      ++count;
      column_keys.push_back(key);
    }
  } else {
    for (size_t i = 0; i < max_cols; ++i) {
      column_keys.push_back("col" + std::to_string(i + 1));
    }
  }
  view.columns = column_keys;
  for (size_t r = data_start; r < table.rows.size(); ++r) {
    const auto& row_values = table.rows[r];
    markql::QueryResultRow row;
    size_t limit = std::min(row_values.size(), column_keys.size());
    for (size_t i = 0; i < limit; ++i) {
      row.attributes[column_keys[i]] = row_values[i];
    }
    view.rows.push_back(std::move(row));
  }
  markql::render::DuckboxOptions options;
  options.max_width = 0;
  options.max_rows = max_rows;
  options.highlight = highlight;
  options.is_tty = is_tty;
  return markql::render::render_duckbox(view, options);
}

std::string export_kind_label(markql::QueryResult::ExportSink::Kind kind) {
  if (kind == markql::QueryResult::ExportSink::Kind::Csv) return "CSV";
  if (kind == markql::QueryResult::ExportSink::Kind::Parquet) return "Parquet";
  if (kind == markql::QueryResult::ExportSink::Kind::Json) return "JSON";
  if (kind == markql::QueryResult::ExportSink::Kind::Ndjson) return "NDJSON";
  return "Export";
}

}  // namespace markql::cli
