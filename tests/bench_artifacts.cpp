#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "artifacts/artifacts.h"
#include "dom/html_parser.h"
#include "runtime/engine/engine_execution_internal.h"

namespace {

double elapsed_ms(const std::chrono::steady_clock::time_point& started_at,
                  const std::chrono::steady_clock::time_point& finished_at) {
  return static_cast<double>(
      std::chrono::duration_cast<std::chrono::microseconds>(finished_at - started_at).count()) /
      1000.0;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::filesystem::path repo_path(const std::string& relative) {
  return std::filesystem::path(__FILE__).parent_path().parent_path() / relative;
}

template <typename Fn>
std::vector<double> measure_iterations(int iterations, Fn&& fn) {
  std::vector<double> samples;
  samples.reserve(iterations);
  for (int i = 0; i < iterations; ++i) {
    auto started = std::chrono::steady_clock::now();
    fn();
    auto finished = std::chrono::steady_clock::now();
    samples.push_back(elapsed_ms(started, finished));
  }
  return samples;
}

double median_ms(std::vector<double> samples) {
  std::sort(samples.begin(), samples.end());
  const size_t mid = samples.size() / 2;
  if ((samples.size() % 2) == 1) return samples[mid];
  return (samples[mid - 1] + samples[mid]) / 2.0;
}

}  // namespace

int main() {
  const std::filesystem::path fixture = repo_path("examples/html/koku_tk.html");
  const std::string query =
      "SELECT a.href, TEXT(a) "
      "FROM document AS a "
      "WHERE a.tag = 'a' AND a.attributes.href LIKE '%.pdf' "
      "ORDER BY doc_order";
  const int iterations = 31;
  const std::string html = read_file(fixture.string());
  const std::filesystem::path mqd_path =
      std::filesystem::temp_directory_path() / "markql_bench_docn_flatbuffers.mqd";

  xsql::HtmlDocument parsed_doc = xsql::parse_html(html);
  xsql::artifacts::PreparedQueryArtifact prepared = xsql::artifacts::prepare_query_artifact(query);

  const std::vector<double> parse_samples = measure_iterations(iterations, [&]() {
    (void)xsql::parse_html(html);
  });

  const std::vector<double> write_samples = measure_iterations(iterations, [&]() {
    xsql::artifacts::write_document_artifact_file(parsed_doc, fixture.string(), mqd_path.string());
  });

  xsql::artifacts::write_document_artifact_file(parsed_doc, fixture.string(), mqd_path.string());
  const uintmax_t mqd_bytes = std::filesystem::file_size(mqd_path);

  const std::vector<double> read_samples = measure_iterations(iterations, [&]() {
    (void)xsql::artifacts::read_document_artifact_file(mqd_path.string());
  });

  xsql::artifacts::DocumentArtifact loaded_doc =
      xsql::artifacts::read_document_artifact_file(mqd_path.string());

  const std::vector<double> raw_exec_samples = measure_iterations(iterations, [&]() {
    (void)xsql::execute_query_with_source(prepared.query, nullptr, &parsed_doc, fixture.string());
  });

  const std::vector<double> mqd_exec_samples = measure_iterations(iterations, [&]() {
    (void)xsql::artifacts::execute_prepared_query_on_document(prepared, loaded_doc);
  });

  std::cout << std::fixed << std::setprecision(3);
  std::cout << "fixture=" << fixture.string() << " iterations=" << iterations << "\n";
  std::cout << "phase html_parse_ms_median=" << median_ms(parse_samples) << "\n";
  std::cout << "phase mqd_write_ms_median=" << median_ms(write_samples) << "\n";
  std::cout << "phase mqd_read_ms_median=" << median_ms(read_samples) << "\n";
  std::cout << "phase raw_document_execute_ms_median=" << median_ms(raw_exec_samples) << "\n";
  std::cout << "phase mqd_loaded_execute_ms_median=" << median_ms(mqd_exec_samples) << "\n";
  std::cout << "artifact mqd_bytes=" << mqd_bytes << "\n";

  std::filesystem::remove(mqd_path);
  return 0;
}
