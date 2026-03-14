#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "artifacts/artifacts.h"
#include "dom/html_parser.h"
#include "lang/markql_parser.h"
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
  const std::filesystem::path mqp_path =
      std::filesystem::temp_directory_path() / "markql_bench_qast_flatbuffers.mqp";

  xsql::HtmlDocument parsed_doc = xsql::parse_html(html);
  const std::vector<double> query_parse_samples = measure_iterations(iterations, [&]() {
    auto parsed = xsql::parse_query(query);
    if (!parsed.query.has_value()) throw std::runtime_error("benchmark parse failure");
  });

  auto parsed_query = xsql::parse_query(query);
  if (!parsed_query.query.has_value()) throw std::runtime_error("benchmark parse failure");

  const std::vector<double> query_prepare_samples = measure_iterations(iterations, [&]() {
    auto prepared = xsql::artifacts::prepare_query_artifact(query);
    (void)prepared;
  });

  xsql::artifacts::PreparedQueryArtifact prepared = xsql::artifacts::prepare_query_artifact(query);
  const std::vector<double> mqp_write_samples = measure_iterations(iterations, [&]() {
    xsql::artifacts::write_prepared_query_artifact_file(prepared.query, query, mqp_path.string());
  });

  xsql::artifacts::write_prepared_query_artifact_file(prepared.query, query, mqp_path.string());
  const uintmax_t mqp_bytes = std::filesystem::file_size(mqp_path);

  const std::vector<double> mqp_load_samples = measure_iterations(iterations, [&]() {
    (void)xsql::artifacts::read_prepared_query_artifact_file(mqp_path.string());
  });
  xsql::artifacts::PreparedQueryArtifact loaded_prepared =
      xsql::artifacts::read_prepared_query_artifact_file(mqp_path.string());

  xsql::artifacts::write_document_artifact_file(parsed_doc, fixture.string(), mqd_path.string());
  xsql::artifacts::DocumentArtifact loaded_doc =
      xsql::artifacts::read_document_artifact_file(mqd_path.string());

  const std::vector<double> query_text_raw_html_samples = measure_iterations(iterations, [&]() {
    auto parsed = xsql::parse_query(query);
    if (!parsed.query.has_value()) throw std::runtime_error("benchmark parse failure");
    xsql::validate_query_for_execution(*parsed.query);
    (void)xsql::execute_query_with_source(*parsed.query, &html, nullptr, fixture.string());
  });

  const std::vector<double> mqp_raw_html_samples = measure_iterations(iterations, [&]() {
    (void)xsql::artifacts::execute_prepared_query_on_html(loaded_prepared, html, fixture.string());
  });

  const std::vector<double> mqp_mqd_samples = measure_iterations(iterations, [&]() {
    (void)xsql::artifacts::execute_prepared_query_on_document(loaded_prepared, loaded_doc);
  });

  std::cout << std::fixed << std::setprecision(3);
  std::cout << "fixture=" << fixture.string() << " iterations=" << iterations << "\n";
  std::cout << "phase query_parse_ms_median=" << median_ms(query_parse_samples) << "\n";
  std::cout << "phase query_prepare_ms_median=" << median_ms(query_prepare_samples) << "\n";
  std::cout << "phase mqp_write_ms_median=" << median_ms(mqp_write_samples) << "\n";
  std::cout << "phase mqp_load_ms_median=" << median_ms(mqp_load_samples) << "\n";
  std::cout << "phase query_text_on_raw_html_ms_median=" << median_ms(query_text_raw_html_samples) << "\n";
  std::cout << "phase mqp_on_raw_html_ms_median=" << median_ms(mqp_raw_html_samples) << "\n";
  std::cout << "phase mqp_on_mqd_ms_median=" << median_ms(mqp_mqd_samples) << "\n";
  std::cout << "artifact mqp_bytes=" << mqp_bytes << "\n";

  std::filesystem::remove(mqd_path);
  std::filesystem::remove(mqp_path);
  return 0;
}
