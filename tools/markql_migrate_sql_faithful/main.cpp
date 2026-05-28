#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "lang/markql_parser.h"

namespace {

enum class Mode { Check, Diff, Write };

struct Replacement {
  size_t start = 0;
  size_t end = 0;
  std::string text;
  std::string reason;
};

struct FileResult {
  std::string path;
  bool parse_failed = false;
  std::string parse_error;
  std::vector<Replacement> replacements;
};

void usage(std::ostream& out) {
  out << "Usage: markql_migrate_sql_faithful (--check|--diff|--write) <query-file>...\n"
      << "\n"
      << "Internal MarkQL migration codemod. The initial slice only rewrites safe\n"
      << "SELECT self node-row projections to SELECT <alias>.* when the source has\n"
      << "an explicit alias.\n";
}

std::optional<std::string> read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) return std::nullopt;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool write_file(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << text;
  return static_cast<bool>(out);
}

bool replacement_less(const Replacement& left, const Replacement& right) {
  if (left.start != right.start) return left.start < right.start;
  return left.end < right.end;
}

bool has_overlaps(std::vector<Replacement> replacements) {
  std::sort(replacements.begin(), replacements.end(), replacement_less);
  for (size_t i = 1; i < replacements.size(); ++i) {
    if (replacements[i].start < replacements[i - 1].end) return true;
  }
  return false;
}

std::string apply_replacements(const std::string& input, std::vector<Replacement> replacements) {
  std::sort(replacements.begin(), replacements.end(), replacement_less);
  std::string out;
  size_t cursor = 0;
  for (const auto& replacement : replacements) {
    out.append(input, cursor, replacement.start - cursor);
    out += replacement.text;
    cursor = replacement.end;
  }
  out.append(input, cursor, std::string::npos);
  return out;
}

void collect_select_self_rewrites(const markql::Query& query, std::vector<Replacement>& out) {
  if (query.with.has_value()) {
    for (const auto& cte : query.with->ctes) {
      if (cte.query != nullptr) collect_select_self_rewrites(*cte.query, out);
    }
  }
  if (query.source.fragments_query != nullptr) {
    collect_select_self_rewrites(*query.source.fragments_query, out);
  }
  if (query.source.parse_query != nullptr) {
    collect_select_self_rewrites(*query.source.parse_query, out);
  }
  if (query.source.derived_query != nullptr) {
    collect_select_self_rewrites(*query.source.derived_query, out);
  }
  for (const auto& join : query.joins) {
    if (join.right_source.fragments_query != nullptr) {
      collect_select_self_rewrites(*join.right_source.fragments_query, out);
    }
    if (join.right_source.parse_query != nullptr) {
      collect_select_self_rewrites(*join.right_source.parse_query, out);
    }
    if (join.right_source.derived_query != nullptr) {
      collect_select_self_rewrites(*join.right_source.derived_query, out);
    }
  }

  if (!query.source.alias.has_value()) return;
  for (const auto& item : query.select_items) {
    if (!item.self_node_projection) continue;
    if (item.tag != "self") continue;
    out.push_back(Replacement{item.span.start, item.span.start + item.tag.size(),
                              *query.source.alias + ".*", "SELECT self -> SELECT alias.*"});
  }
}

FileResult analyze_file(const std::string& path, const std::string& text) {
  FileResult result;
  result.path = path;
  markql::ParseResult parsed = markql::parse_query(text);
  if (!parsed.query.has_value()) {
    result.parse_failed = true;
    result.parse_error = parsed.error.has_value() ? parsed.error->message : "unknown parse error";
    return result;
  }
  collect_select_self_rewrites(*parsed.query, result.replacements);
  if (has_overlaps(result.replacements)) {
    result.parse_failed = true;
    result.parse_error = "internal error: overlapping rewrite spans";
    result.replacements.clear();
  }
  return result;
}

void print_diff(const std::string& path, const std::string& before,
                const std::vector<Replacement>& replacements) {
  std::cout << "--- " << path << "\n";
  std::cout << "+++ " << path << "\n";
  for (const auto& replacement : replacements) {
    std::cout << "@@ byte " << replacement.start << ":" << replacement.end << " @@ "
              << replacement.reason << "\n";
    std::cout << "- " << before.substr(replacement.start, replacement.end - replacement.start)
              << "\n";
    std::cout << "+ " << replacement.text << "\n";
  }
  std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    usage(std::cerr);
    return 2;
  }

  std::optional<Mode> mode;
  std::vector<std::string> paths;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--check") {
      mode = Mode::Check;
    } else if (arg == "--diff") {
      mode = Mode::Diff;
    } else if (arg == "--write") {
      mode = Mode::Write;
    } else if (arg == "--help" || arg == "-h") {
      usage(std::cout);
      return 0;
    } else {
      paths.push_back(arg);
    }
  }

  if (!mode.has_value() || paths.empty()) {
    usage(std::cerr);
    return 2;
  }

  bool had_error = false;
  bool had_changes = false;
  for (const auto& path : paths) {
    auto text = read_file(path);
    if (!text.has_value()) {
      std::cerr << path << ": failed to read file\n";
      had_error = true;
      continue;
    }
    FileResult result = analyze_file(path, *text);
    if (result.parse_failed) {
      std::cerr << path << ": " << result.parse_error << "\n";
      had_error = true;
      continue;
    }
    if (result.replacements.empty()) continue;

    had_changes = true;
    std::string rewritten = apply_replacements(*text, result.replacements);
    if (*mode == Mode::Check) {
      std::cout << path << ": would rewrite " << result.replacements.size() << " span(s)\n";
    } else if (*mode == Mode::Diff) {
      print_diff(path, *text, result.replacements);
    } else if (*mode == Mode::Write) {
      if (!write_file(path, rewritten)) {
        std::cerr << path << ": failed to write file\n";
        had_error = true;
      } else {
        std::cout << path << ": rewrote " << result.replacements.size() << " span(s)\n";
      }
    }
  }

  if (had_error) return 2;
  if (*mode == Mode::Check && had_changes) return 1;
  return 0;
}
