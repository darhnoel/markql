#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "artifacts/artifacts.h"
#include "dom/html_parser.h"
#include "lang/markql_parser.h"
#include "runtime/engine/engine_execution_internal.h"

namespace {

volatile size_t g_sink_rows = 0;

double elapsed_ms(const std::chrono::steady_clock::time_point& started_at,
                  const std::chrono::steady_clock::time_point& finished_at) {
  return static_cast<double>(
      std::chrono::duration_cast<std::chrono::microseconds>(finished_at - started_at).count()) /
      1000.0;
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("failed to read file: " + path);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

std::filesystem::path repo_path(const std::string& relative) {
  return std::filesystem::path(__FILE__).parent_path().parent_path() / relative;
}

std::filesystem::path temp_artifact_path(const std::string& stem, const std::string& ext) {
  const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
  const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
  return std::filesystem::temp_directory_path() /
         (stem + "_" + std::to_string(now) + "_" + std::to_string(tid) + ext);
}

double median_ms(std::vector<double> samples) {
  std::sort(samples.begin(), samples.end());
  const size_t mid = samples.size() / 2;
  if ((samples.size() % 2) == 1) return samples[mid];
  return (samples[mid - 1] + samples[mid]) / 2.0;
}

template <typename Fn>
std::vector<double> measure_iterations(int iterations, Fn&& fn) {
  std::vector<double> samples;
  samples.reserve(iterations);
  for (int i = 0; i < iterations; ++i) {
    auto started = std::chrono::steady_clock::now();
    g_sink_rows += fn();
    auto finished = std::chrono::steady_clock::now();
    samples.push_back(elapsed_ms(started, finished));
  }
  return samples;
}

markql::Query parse_and_validate_query(const std::string& query_text) {
  auto parsed = markql::parse_query(query_text);
  if (!parsed.query.has_value()) throw std::runtime_error("benchmark parse failure");
  markql::validate_query_for_execution(*parsed.query);
  return *parsed.query;
}

struct BenchOptions {
  std::filesystem::path fixture = repo_path("examples/html/koku_tk.html");
  std::string query =
      "SELECT a.href, TEXT(a) "
      "FROM document AS a "
      "WHERE a.tag = 'a' AND a.attributes.href LIKE '%.pdf' "
      "ORDER BY doc_order";
  int iterations = 31;
};

BenchOptions parse_args(int argc, char** argv) {
  BenchOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--fixture" && (i + 1) < argc) {
      options.fixture = argv[++i];
      continue;
    }
    if (arg == "--query" && (i + 1) < argc) {
      options.query = argv[++i];
      continue;
    }
    if (arg == "--query-file" && (i + 1) < argc) {
      options.query = read_file(argv[++i]);
      continue;
    }
    if (arg == "--iterations" && (i + 1) < argc) {
      options.iterations = std::stoi(argv[++i]);
      continue;
    }
    if (arg == "--help") {
      std::cout << "Usage: markql_bench_artifacts [--fixture <path>] [--query <sql>] "
                   "[--query-file <path>] [--iterations <n>]\n";
      std::exit(0);
    }
    throw std::runtime_error("unknown argument: " + arg);
  }
  if (options.iterations <= 0) {
    throw std::runtime_error("iterations must be positive");
  }
  return options;
}

void print_metric(const std::string& name, const std::vector<double>& samples) {
  std::cout << "phase " << name << "_ms_median=" << median_ms(samples) << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  const BenchOptions options = parse_args(argc, argv);
  const std::filesystem::path mqd_path = temp_artifact_path("markql_bench_truth_docn", ".mqd");
  const std::filesystem::path mqp_path = temp_artifact_path("markql_bench_truth_qast", ".mqp");

  const std::string baseline_html = read_file(options.fixture.string());
  const markql::HtmlDocument baseline_document = markql::parse_html(baseline_html);
  const markql::Query baseline_query = parse_and_validate_query(options.query);
  const markql::artifacts::PreparedQueryArtifact baseline_prepared =
      markql::artifacts::prepare_query_artifact(options.query);

  markql::artifacts::write_document_artifact_file(
      baseline_document, options.fixture.string(), mqd_path.string());
  markql::artifacts::write_prepared_query_artifact_file(
      baseline_prepared.query, options.query, mqp_path.string());

  const uintmax_t mqd_bytes = std::filesystem::file_size(mqd_path);
  const uintmax_t mqp_bytes = std::filesystem::file_size(mqp_path);

  const std::vector<double> html_read_samples = measure_iterations(options.iterations, [&]() {
    return read_file(options.fixture.string()).size();
  });

  const std::vector<double> html_parse_samples = measure_iterations(options.iterations, [&]() {
    return markql::parse_html(baseline_html).nodes.size();
  });

  const std::vector<double> mqd_load_samples = measure_iterations(options.iterations, [&]() {
    return markql::artifacts::read_document_artifact_file(mqd_path.string()).document.nodes.size();
  });

  const std::vector<double> query_parse_samples = measure_iterations(options.iterations, [&]() {
    auto parsed = markql::parse_query(options.query);
    if (!parsed.query.has_value()) throw std::runtime_error("benchmark parse failure");
    return static_cast<size_t>(parsed.query->select_items.size());
  });

  const std::vector<double> query_prepare_samples = measure_iterations(options.iterations, [&]() {
    return markql::artifacts::prepare_query_artifact(options.query).query.select_items.size();
  });

  const std::vector<double> mqp_load_samples = measure_iterations(options.iterations, [&]() {
    return markql::artifacts::read_prepared_query_artifact_file(mqp_path.string())
        .query.select_items.size();
  });

  const markql::artifacts::DocumentArtifact loaded_doc =
      markql::artifacts::read_document_artifact_file(mqd_path.string());
  const markql::artifacts::PreparedQueryArtifact loaded_prepared =
      markql::artifacts::read_prepared_query_artifact_file(mqp_path.string());

  const std::vector<double> execute_query_text_on_parsed_html_doc_samples =
      measure_iterations(options.iterations, [&]() {
        return markql::execute_query_with_source(
                   baseline_query, nullptr, &baseline_document, options.fixture.string())
            .rows.size();
      });

  const std::vector<double> execute_mqp_on_parsed_html_doc_samples =
      measure_iterations(options.iterations, [&]() {
        return markql::artifacts::execute_prepared_query_on_document(
                   loaded_prepared, {loaded_doc.info, baseline_document})
            .rows.size();
      });

  const std::vector<double> execute_query_text_on_loaded_mqd_doc_samples =
      measure_iterations(options.iterations, [&]() {
        return markql::artifacts::execute_query_text_on_document(options.query, loaded_doc).rows.size();
      });

  const std::vector<double> execute_mqp_on_loaded_mqd_doc_samples =
      measure_iterations(options.iterations, [&]() {
        return markql::artifacts::execute_prepared_query_on_document(loaded_prepared, loaded_doc)
            .rows.size();
      });

  const std::vector<double> raw_html_query_text_total_samples =
      measure_iterations(options.iterations, [&]() {
        const std::string html = read_file(options.fixture.string());
        const markql::HtmlDocument document = markql::parse_html(html);
        const markql::Query query = parse_and_validate_query(options.query);
        return markql::execute_query_with_source(query, nullptr, &document, options.fixture.string())
            .rows.size();
      });

  const std::vector<double> raw_html_mqp_total_samples = measure_iterations(options.iterations, [&]() {
    const std::string html = read_file(options.fixture.string());
    const markql::HtmlDocument document = markql::parse_html(html);
    const markql::artifacts::PreparedQueryArtifact prepared =
        markql::artifacts::read_prepared_query_artifact_file(mqp_path.string());
    return markql::artifacts::execute_prepared_query_on_document(prepared, {loaded_doc.info, document})
        .rows.size();
  });

  const std::vector<double> mqd_query_text_total_samples =
      measure_iterations(options.iterations, [&]() {
        const markql::artifacts::DocumentArtifact document =
            markql::artifacts::read_document_artifact_file(mqd_path.string());
        return markql::artifacts::execute_query_text_on_document(options.query, document).rows.size();
      });

  const std::vector<double> mqd_mqp_total_samples = measure_iterations(options.iterations, [&]() {
    const markql::artifacts::DocumentArtifact document =
        markql::artifacts::read_document_artifact_file(mqd_path.string());
    const markql::artifacts::PreparedQueryArtifact prepared =
        markql::artifacts::read_prepared_query_artifact_file(mqp_path.string());
    return markql::artifacts::execute_prepared_query_on_document(prepared, document).rows.size();
  });

  std::cout << std::fixed << std::setprecision(3);
  std::cout << "fixture=" << options.fixture.string() << " iterations=" << options.iterations
            << "\n";
  std::cout << "query_bytes=" << options.query.size() << "\n";
  print_metric("html_read", html_read_samples);
  print_metric("html_parse", html_parse_samples);
  print_metric("mqd_load", mqd_load_samples);
  print_metric("query_parse", query_parse_samples);
  print_metric("query_prepare", query_prepare_samples);
  print_metric("mqp_load", mqp_load_samples);
  print_metric("execute_query_text_on_parsed_html_doc", execute_query_text_on_parsed_html_doc_samples);
  print_metric("execute_mqp_on_parsed_html_doc", execute_mqp_on_parsed_html_doc_samples);
  print_metric("execute_query_text_on_loaded_mqd_doc", execute_query_text_on_loaded_mqd_doc_samples);
  print_metric("execute_mqp_on_loaded_mqd_doc", execute_mqp_on_loaded_mqd_doc_samples);
  print_metric("raw_html_plus_query_text_cold_total", raw_html_query_text_total_samples);
  print_metric("raw_html_plus_mqp_cold_total", raw_html_mqp_total_samples);
  print_metric("mqd_plus_query_text_cold_total", mqd_query_text_total_samples);
  print_metric("mqd_plus_mqp_cold_total", mqd_mqp_total_samples);
  std::cout << "artifact mqd_bytes=" << mqd_bytes << "\n";
  std::cout << "artifact mqp_bytes=" << mqp_bytes << "\n";
  std::cout << "sink_rows=" << g_sink_rows << "\n";

  std::filesystem::remove(mqd_path);
  std::filesystem::remove(mqp_path);
  return 0;
}
