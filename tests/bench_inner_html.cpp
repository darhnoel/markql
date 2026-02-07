#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>

#include "xsql/xsql.h"

namespace {

std::string build_fixture(std::size_t n) {
  std::string html;
  html.reserve(n * 160);
  html += "<html><body>\n";
  for (std::size_t i = 0; i < n; ++i) {
    html += "<div class='card'>\n";
    html += "  <h2> Title ";
    html += std::to_string(i);
    html += " </h2>\n";
    html += "  <p>   Summary line   ";
    html += std::to_string(i);
    html += "   with spaces   </p>\n";
    html += "</div>\n";
  }
  html += "</body></html>\n";
  return html;
}

std::size_t total_inner_html_bytes(const xsql::QueryResult& result) {
  std::size_t bytes = 0;
  for (const auto& row : result.rows) {
    bytes += row.inner_html.size();
  }
  return bytes;
}

void run_case(const std::string& html, const std::string& label, const std::string& query) {
  auto start = std::chrono::steady_clock::now();
  xsql::QueryResult result = xsql::execute_query_from_document(html, query);
  auto end = std::chrono::steady_clock::now();
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  std::size_t bytes = total_inner_html_bytes(result);
  std::cout << label
            << ": rows=" << result.rows.size()
            << " elapsed_ms=" << elapsed_ms
            << " output_bytes=" << bytes
            << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
  std::size_t n = 10000;
  if (argc > 1) {
    n = static_cast<std::size_t>(std::stoull(argv[1]));
  }

  std::string html = build_fixture(n);
  std::cout << "fixture_bytes=" << html.size() << " nodes=" << n << std::endl;
  run_case(html,
           "inner_html_minified",
           "SELECT INNER_HTML(div) FROM document WHERE attributes.class = 'card'");
  run_case(html,
           "inner_html_raw",
           "SELECT RAW_INNER_HTML(div) FROM document WHERE attributes.class = 'card'");
  return 0;
}
