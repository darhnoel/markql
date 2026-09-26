#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "markql/query_formatter.h"

namespace {

void usage(std::ostream& out) {
  out << "Usage: markql-fmt [--check|--write] <query-file>...\n"
      << "       markql-fmt [--check] -\n\n"
      << "Formats MarkQL queries canonically.\n";
}

std::optional<std::string> read_file(const std::string& path) {
  if (path == "-") {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
  }
  std::ifstream in(path);
  if (!in) return std::nullopt;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
    usage(std::cout);
    return 0;
  }
  std::string mode = "stdout";
  int first_path = 1;
  if (argc > 1 && (std::string(argv[1]) == "--check" || std::string(argv[1]) == "--write")) {
    mode = std::string(argv[1]).substr(2);
    first_path = 2;
  }
  if (argc <= first_path) {
    usage(std::cerr);
    return 2;
  }

  bool had_error = false;
  bool had_changes = false;
  std::vector<std::pair<std::string, std::string>> outputs;
  for (int i = first_path; i < argc; ++i) {
    const std::string path = argv[i];
    const auto text = read_file(path);
    if (!text) {
      std::cerr << path << ": failed to read file\n";
      had_error = true;
      continue;
    }
    const auto formatted = markql::format_query_text(*text);
    if (!formatted.text) {
      std::cerr << path << ": " << formatted.error << "\n";
      had_error = true;
      continue;
    }
    outputs.emplace_back(path, *formatted.text);
    if (*formatted.text != *text) {
      if (mode == "check") std::cout << path << ": would reformat\n";
      had_changes = true;
    }
  }
  if (had_error) return 2;
  if (mode == "write") {
    for (const auto& [path, text] : outputs) {
      if (path == "-") continue;
      std::ofstream out(path, std::ios::trunc);
      if (!out) return 2;
      out << text;
    }
  } else if (mode == "stdout") {
    for (const auto& [path, text] : outputs) std::cout << text;
  }
  return mode == "check" && had_changes ? 1 : 0;
}
