#include "test_harness.h"
#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>

#include <flatbuffers/vector.h>

#include "artifacts/artifacts.h"
#include "cli_args.h"
#include "cli_utils.h"
#include "document_generated.h"
#include "xsql/diagnostics.h"

namespace {

namespace docnfb = xsql::artifacts::docnfb;

std::filesystem::path temp_path(const std::string& name) {
  return std::filesystem::temp_directory_path() / name;
}

std::filesystem::path repo_path(const std::string& relative) {
  return std::filesystem::path(__FILE__).parent_path().parent_path() / relative;
}

std::string read_binary(const std::filesystem::path& path) {
  return read_file_to_string(path);
}

uint16_t read_u16_le(const std::string& data, size_t offset) {
  return static_cast<uint16_t>(static_cast<unsigned char>(data[offset])) |
         (static_cast<uint16_t>(static_cast<unsigned char>(data[offset + 1])) << 8);
}

uint32_t read_u32_le(const std::string& data, size_t offset) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) {
    value |= static_cast<uint32_t>(static_cast<unsigned char>(data[offset + i])) << (i * 8);
  }
  return value;
}

uint64_t read_u64_le(const std::string& data, size_t offset) {
  uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) {
    value |= static_cast<uint64_t>(static_cast<unsigned char>(data[offset + i])) << (i * 8);
  }
  return value;
}

void write_u32_le(std::string& data, size_t offset, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) {
    data[offset + i] = static_cast<char>((value >> (i * 8)) & 0xffu);
  }
}

void write_u64_le(std::string& data, size_t offset, uint64_t value) {
  for (size_t i = 0; i < 8; ++i) {
    data[offset + i] = static_cast<char>((value >> (i * 8)) & 0xffu);
  }
}

uint64_t fnv1a64(const std::string& data) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : data) {
    hash ^= static_cast<uint64_t>(byte);
    hash *= 1099511628211ull;
  }
  return hash;
}

size_t header_bytes(const std::string& data) {
  return static_cast<size_t>(read_u32_le(data, 8));
}

void rewrite_payload_checksum(std::string& data) {
  const size_t payload_offset = header_bytes(data);
  std::string payload = data.substr(payload_offset);
  write_u64_le(data, 32, fnv1a64(payload));
}

size_t find_section_payload_offset(const std::string& data, const std::string& tag) {
  const size_t payload_offset = header_bytes(data);
  const uint32_t section_count = read_u32_le(data, 12);
  size_t offset = payload_offset;
  for (uint32_t i = 0; i < section_count; ++i) {
    std::string current_tag = data.substr(offset, 4);
    const uint64_t section_size = read_u64_le(data, offset + 4);
    offset += 12;
    if (current_tag == tag) return offset;
    offset += static_cast<size_t>(section_size);
  }
  throw std::runtime_error("missing section: " + tag);
}

size_t find_meta_node_count_offset(const std::string& data) {
  const size_t meta_payload = find_section_payload_offset(data, "META");
  const uint64_t producer_len = read_u64_le(data, meta_payload);
  const size_t language_len_offset = meta_payload + 8 + static_cast<size_t>(producer_len);
  const uint64_t language_len = read_u64_le(data, language_len_offset);
  const size_t source_len_offset = language_len_offset + 8 + static_cast<size_t>(language_len);
  const uint64_t source_len = read_u64_le(data, source_len_offset);
  return source_len_offset + 8 + static_cast<size_t>(source_len);
}

size_t find_docn_nodes_vector_size_offset(const std::string& data) {
  const size_t docn_payload = find_section_payload_offset(data, "DOCN");
  const char* payload_ptr = data.data() + docn_payload;
  const docnfb::Document* document = docnfb::GetDocument(payload_ptr);
  const flatbuffers::Vector<flatbuffers::Offset<docnfb::Node>>* nodes = document->nodes();
  const char* vector_ptr = reinterpret_cast<const char*>(nodes);
  return docn_payload + static_cast<size_t>(vector_ptr - payload_ptr);
}

void expect_artifact_error_contains(const std::function<void()>& fn,
                                    const std::string& expected,
                                    const std::string& message) {
  bool threw = false;
  try {
    fn();
  } catch (const std::exception& ex) {
    threw = true;
    expect_true(std::string(ex.what()).find(expected) != std::string::npos, message);
  }
  expect_true(threw, message + " throws");
}

void write_binary(const std::filesystem::path& path, const std::string& data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string canonical_rows_json(const xsql::QueryResult& result) {
  return xsql::cli::build_json(result, xsql::ColumnNameMode::Normalize);
}

void test_artifact_cli_args() {
  const char* argv[] = {
      "markql",
      "--input",
      "page.html",
      "--write-mqd",
      "page.mqd",
  };
  int argc = static_cast<int>(sizeof(argv) / sizeof(argv[0]));
  xsql::cli::CliOptions options;
  std::string error;
  bool ok = xsql::cli::parse_cli_args(argc, const_cast<char**>(argv), options, error);
  expect_true(ok, "artifact flags parse");
  expect_true(options.input == "page.html", "artifact input parsed");
  expect_true(options.write_mqd == "page.mqd", "write-mqd parsed");
}

void test_artifact_html_and_mqd_results_match() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::string query =
      "SELECT a.href, TEXT(a) FROM document WHERE attributes.href IS NOT NULL ORDER BY doc_order";
  const std::filesystem::path artifact_path = temp_path("xsql_test_basic.mqd");
  const std::string html = read_file_to_string(fixture);
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(html), fixture.string(), artifact_path.string());

  xsql::QueryResult direct = xsql::execute_query_from_file(fixture.string(), query);
  xsql::QueryResult via_artifact = xsql::execute_query_from_file(artifact_path.string(), query);
  expect_true(canonical_rows_json(direct) == canonical_rows_json(via_artifact),
              "html query matches mqd query");
  std::filesystem::remove(artifact_path);
}

void test_artifact_query_and_mqp_results_match() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::string html = read_file_to_string(fixture);
  const std::string query =
      "SELECT a.href, TEXT(a) FROM document WHERE attributes.href IS NOT NULL ORDER BY doc_order";
  const std::filesystem::path artifact_path = temp_path("xsql_test_basic.mqp");
  xsql::artifacts::PreparedQueryArtifact prepared = xsql::artifacts::prepare_query_artifact(query);
  xsql::artifacts::write_prepared_query_artifact_file(prepared.query, query, artifact_path.string());
  xsql::artifacts::PreparedQueryArtifact loaded =
      xsql::artifacts::read_prepared_query_artifact_file(artifact_path.string());

  xsql::QueryResult direct = xsql::execute_query_from_file(fixture.string(), query);
  xsql::QueryResult via_artifact =
      xsql::artifacts::execute_prepared_query_on_html(loaded, html, fixture.string());
  expect_true(canonical_rows_json(direct) == canonical_rows_json(via_artifact),
              "query text matches mqp query");
  std::filesystem::remove(artifact_path);
}

void test_artifact_mqp_on_mqd_matches_direct() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::string html = read_file_to_string(fixture);
  const std::string query =
      "SELECT a.href, TEXT(a) FROM document WHERE attributes.href IS NOT NULL ORDER BY doc_order";
  const std::filesystem::path mqd_path = temp_path("xsql_test_combo.mqd");
  const std::filesystem::path mqp_path = temp_path("xsql_test_combo.mqp");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(html), fixture.string(), mqd_path.string());
  xsql::artifacts::PreparedQueryArtifact prepared = xsql::artifacts::prepare_query_artifact(query);
  xsql::artifacts::write_prepared_query_artifact_file(prepared.query, query, mqp_path.string());

  xsql::QueryResult direct = xsql::execute_query_from_file(fixture.string(), query);
  xsql::artifacts::PreparedQueryArtifact loaded_query =
      xsql::artifacts::read_prepared_query_artifact_file(mqp_path.string());
  xsql::artifacts::DocumentArtifact loaded_doc =
      xsql::artifacts::read_document_artifact_file(mqd_path.string());
  xsql::QueryResult via_both =
      xsql::artifacts::execute_prepared_query_on_document(loaded_query, loaded_doc);
  expect_true(canonical_rows_json(direct) == canonical_rows_json(via_both),
              "mqp on mqd matches direct execution");

  std::filesystem::remove(mqd_path);
  std::filesystem::remove(mqp_path);
}

void test_artifact_lint_behavior_unchanged() {
  const std::string query = "SELECT FROM doc";
  const std::string expected =
      "[{\"severity\":\"ERROR\",\"code\":\"MQL-SYN-0001\",\"message\":\"Expected tag identifier\","
      "\"help\":\"Check SQL clause order: WITH ... SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ... TO ...\","
      "\"doc_ref\":\"docs/book/appendix-grammar.md\","
      "\"span\":{\"start_line\":1,\"start_col\":8,\"end_line\":1,\"end_col\":9,\"byte_start\":7,\"byte_end\":8},"
      "\"snippet\":\" --> line 1, col 8\\n  |\\n1 | SELECT FROM doc\\n  |        ^\","
      "\"related\":[]}]";
  const std::string actual = xsql::render_diagnostics_json(xsql::lint_query(query));
  expect_true(actual == expected, "lint json remains stable");
}

void test_artifact_document_round_trip_is_deterministic() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::string html = read_file_to_string(fixture);
  const std::filesystem::path path_a = temp_path("xsql_det_a.mqd");
  const std::filesystem::path path_b = temp_path("xsql_det_b.mqd");
  xsql::HtmlDocument document = xsql::parse_html(html);
  xsql::artifacts::write_document_artifact_file(document, fixture.string(), path_a.string());
  xsql::artifacts::write_document_artifact_file(document, fixture.string(), path_b.string());
  expect_true(read_binary(path_a) == read_binary(path_b), "mqd bytes deterministic");
  std::filesystem::remove(path_a);
  std::filesystem::remove(path_b);
}

void test_artifact_query_round_trip_is_deterministic() {
  const std::string query =
      "SELECT a.href, TEXT(a) FROM document WHERE attributes.href IS NOT NULL ORDER BY doc_order";
  const std::filesystem::path path_a = temp_path("xsql_det_a.mqp");
  const std::filesystem::path path_b = temp_path("xsql_det_b.mqp");
  xsql::artifacts::PreparedQueryArtifact prepared = xsql::artifacts::prepare_query_artifact(query);
  xsql::artifacts::write_prepared_query_artifact_file(prepared.query, query, path_a.string());
  xsql::artifacts::write_prepared_query_artifact_file(prepared.query, query, path_b.string());
  expect_true(read_binary(path_a) == read_binary(path_b), "mqp bytes deterministic");
  std::filesystem::remove(path_a);
  std::filesystem::remove(path_b);
}

void test_artifact_document_major_version_rejected() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::string html = read_file_to_string(fixture);
  const std::filesystem::path path = temp_path("xsql_bad_major.mqd");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(html), fixture.string(), path.string());
  std::string bytes = read_binary(path);
  bytes[4] = 3;
  bytes[5] = 0;
  write_binary(path, bytes);
  bool threw = false;
  try {
    (void)xsql::artifacts::read_document_artifact_file(path.string());
  } catch (const std::exception& ex) {
    threw = true;
    expect_true(std::string(ex.what()).find("major version mismatch") != std::string::npos,
                "mqd major version rejection is clear");
  }
  expect_true(threw, "mqd major version rejected");
  std::filesystem::remove(path);
}

void test_artifact_query_major_version_rejected() {
  const std::string query =
      "SELECT a.href, TEXT(a) FROM document WHERE attributes.href IS NOT NULL ORDER BY doc_order";
  const std::filesystem::path path = temp_path("xsql_bad_major.mqp");
  xsql::artifacts::PreparedQueryArtifact prepared = xsql::artifacts::prepare_query_artifact(query);
  xsql::artifacts::write_prepared_query_artifact_file(prepared.query, query, path.string());
  std::string bytes = read_binary(path);
  bytes[4] = 3;
  bytes[5] = 0;
  write_binary(path, bytes);
  bool threw = false;
  try {
    (void)xsql::artifacts::read_prepared_query_artifact_file(path.string());
  } catch (const std::exception& ex) {
    threw = true;
    expect_true(std::string(ex.what()).find("major version mismatch") != std::string::npos,
                "mqp major version rejection is clear");
  }
  expect_true(threw, "mqp major version rejected");
  std::filesystem::remove(path);
}

void test_artifact_corrupted_files_fail_safely() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::string html = read_file_to_string(fixture);
  const std::filesystem::path path = temp_path("xsql_corrupt.mqd");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(html), fixture.string(), path.string());
  std::string bytes = read_binary(path);
  bytes.resize(12);
  write_binary(path, bytes);
  bool threw = false;
  try {
    (void)xsql::artifacts::read_document_artifact_file(path.string());
  } catch (const std::exception& ex) {
    threw = true;
    expect_true(std::string(ex.what()).find("Corrupted artifact") != std::string::npos ||
                    std::string(ex.what()).find("Unsupported artifact") != std::string::npos,
                "corrupted artifact error is safe");
  }
  expect_true(threw, "corrupted artifact rejected");
  std::filesystem::remove(path);
}

void test_artifact_inspect_reports_metadata() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::string html = read_file_to_string(fixture);
  const std::string query =
      "SELECT a.href, TEXT(a) FROM document WHERE attributes.href IS NOT NULL ORDER BY doc_order";
  const std::filesystem::path mqd_path = temp_path("xsql_info.mqd");
  const std::filesystem::path mqp_path = temp_path("xsql_info.mqp");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(html), fixture.string(), mqd_path.string());
  xsql::artifacts::PreparedQueryArtifact prepared = xsql::artifacts::prepare_query_artifact(query);
  xsql::artifacts::write_prepared_query_artifact_file(prepared.query, query, mqp_path.string());

  xsql::artifacts::ArtifactInfo doc_info = xsql::artifacts::inspect_artifact_file(mqd_path.string());
  expect_true(doc_info.header.kind == xsql::artifacts::ArtifactKind::DocumentSnapshot,
              "inspect mqd kind");
  expect_true(doc_info.header.section_count == 2, "inspect mqd section count");
  expect_true(doc_info.header.format_minor == 1, "inspect mqd format minor");
  expect_true(doc_info.header.required_features == 1, "inspect mqd required feature bit");
  expect_true(doc_info.source_uri == fixture.string(), "inspect mqd source uri");
  expect_true(doc_info.language_version == doc_info.producer_version,
              "inspect mqd language version");
  expect_true(doc_info.node_count > 0, "inspect mqd node count");

  xsql::artifacts::ArtifactInfo query_info =
      xsql::artifacts::inspect_artifact_file(mqp_path.string());
  expect_true(query_info.header.kind == xsql::artifacts::ArtifactKind::PreparedQuery,
              "inspect mqp kind");
  expect_true(query_info.original_query == query, "inspect mqp original query");

  std::filesystem::remove(mqd_path);
  std::filesystem::remove(mqp_path);
}

void test_artifact_bad_magic_rejected() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::filesystem::path path = temp_path("xsql_bad_magic.mqd");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(read_file_to_string(fixture)), fixture.string(), path.string());
  std::string bytes = read_binary(path);
  bytes[0] = 'B';
  write_binary(path, bytes);
  expect_artifact_error_contains(
      [&]() { (void)xsql::artifacts::read_document_artifact_file(path.string()); },
      "unknown magic header",
      "bad magic rejected clearly");
  std::filesystem::remove(path);
}

void test_artifact_unknown_required_flag_rejected() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::filesystem::path path = temp_path("xsql_required_flag.mqd");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(read_file_to_string(fixture)), fixture.string(), path.string());
  std::string bytes = read_binary(path);
  write_u64_le(bytes, 16, 2);
  write_binary(path, bytes);
  expect_artifact_error_contains(
      [&]() { (void)xsql::artifacts::read_document_artifact_file(path.string()); },
      "unknown required feature flags",
      "unknown required flags rejected");
  std::filesystem::remove(path);
}

void test_artifact_checksum_mismatch_rejected() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::filesystem::path path = temp_path("xsql_checksum.mqd");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(read_file_to_string(fixture)), fixture.string(), path.string());
  std::string bytes = read_binary(path);
  const size_t payload_offset = header_bytes(bytes);
  bytes[payload_offset + 20] ^= 0x01;
  write_binary(path, bytes);
  expect_artifact_error_contains(
      [&]() { (void)xsql::artifacts::read_document_artifact_file(path.string()); },
      "checksum mismatch",
      "checksum mismatch rejected");
  std::filesystem::remove(path);
}

void test_artifact_section_length_rejected() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::filesystem::path path = temp_path("xsql_section_length.mqd");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(read_file_to_string(fixture)), fixture.string(), path.string());
  std::string bytes = read_binary(path);
  const size_t meta_size_offset = header_bytes(bytes) + 4;
  write_u64_le(bytes, meta_size_offset, read_u64_le(bytes, meta_size_offset) + 1024ull * 1024ull);
  rewrite_payload_checksum(bytes);
  write_binary(path, bytes);
  expect_artifact_error_contains(
      [&]() { (void)xsql::artifacts::read_document_artifact_file(path.string()); },
      "truncated section payload",
      "section length corruption rejected");
  std::filesystem::remove(path);
}

void test_artifact_huge_node_count_rejected() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::filesystem::path path = temp_path("xsql_node_count.mqd");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(read_file_to_string(fixture)), fixture.string(), path.string());
  std::string bytes = read_binary(path);
  const size_t node_count_offset = find_meta_node_count_offset(bytes);
  write_u64_le(bytes, node_count_offset, 1000001ull);
  rewrite_payload_checksum(bytes);
  write_binary(path, bytes);
  expect_artifact_error_contains(
      [&]() { (void)xsql::artifacts::read_document_artifact_file(path.string()); },
      "node count is too large",
      "huge node count rejected");
  std::filesystem::remove(path);
}

void test_artifact_docn_flatbuffer_verifier_rejected() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::filesystem::path path = temp_path("xsql_docn_verifier.mqd");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(read_file_to_string(fixture)), fixture.string(), path.string());
  std::string bytes = read_binary(path);
  const size_t docn_payload = find_section_payload_offset(bytes, "DOCN");
  write_u32_le(bytes, docn_payload, 0x7fffffffu);
  rewrite_payload_checksum(bytes);
  write_binary(path, bytes);
  expect_artifact_error_contains(
      [&]() { (void)xsql::artifacts::read_document_artifact_file(path.string()); },
      "FlatBuffer verification failed",
      "docn flatbuffer verifier rejects hostile payload");
  std::filesystem::remove(path);
}

void test_artifact_node_count_metadata_mismatch_rejected() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::filesystem::path path = temp_path("xsql_node_count_meta.mqd");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(read_file_to_string(fixture)), fixture.string(), path.string());
  std::string bytes = read_binary(path);
  const size_t node_count_offset = find_meta_node_count_offset(bytes);
  write_u64_le(bytes, node_count_offset, 1);
  rewrite_payload_checksum(bytes);
  write_binary(path, bytes);
  expect_artifact_error_contains(
      [&]() { (void)xsql::artifacts::read_document_artifact_file(path.string()); },
      "node count metadata mismatch",
      "metadata node count mismatch rejected");
  std::filesystem::remove(path);
}

void test_artifact_invalid_utf8_rejected() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::filesystem::path path = temp_path("xsql_utf8.mqd");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(read_file_to_string(fixture)), fixture.string(), path.string());
  std::string bytes = read_binary(path);
  const size_t meta_payload = find_section_payload_offset(bytes, "META");
  const uint64_t producer_len = read_u64_le(bytes, meta_payload);
  const size_t language_len_offset = meta_payload + 8 + static_cast<size_t>(producer_len);
  const uint64_t language_len = read_u64_le(bytes, language_len_offset);
  const size_t source_len_offset = language_len_offset + 8 + static_cast<size_t>(language_len);
  const size_t source_bytes = source_len_offset + 8;
  bytes[source_bytes] = static_cast<char>(0xFF);
  rewrite_payload_checksum(bytes);
  write_binary(path, bytes);
  expect_artifact_error_contains(
      [&]() { (void)xsql::artifacts::read_document_artifact_file(path.string()); },
      "not valid UTF-8",
      "invalid utf8 rejected");
  std::filesystem::remove(path);
}

void test_artifact_terminal_escape_sanitization() {
  const std::filesystem::path fixture = repo_path("docs/fixtures/basic.html");
  const std::filesystem::path path = temp_path("xsql_escape_info.mqd");
  xsql::artifacts::write_document_artifact_file(
      xsql::parse_html(read_file_to_string(fixture)), fixture.string(), path.string());
  std::string bytes = read_binary(path);
  const size_t meta_payload = find_section_payload_offset(bytes, "META");
  const uint64_t producer_len = read_u64_le(bytes, meta_payload);
  const size_t language_len_offset = meta_payload + 8 + static_cast<size_t>(producer_len);
  const uint64_t language_len = read_u64_le(bytes, language_len_offset);
  const size_t source_len_offset = language_len_offset + 8 + static_cast<size_t>(language_len);
  const uint64_t source_len = read_u64_le(bytes, source_len_offset);
  expect_true(source_len >= 6, "source uri has enough space for escape mutation");
  const size_t source_bytes = source_len_offset + 8;
  bytes[source_bytes] = '\x1B';
  bytes[source_bytes + 1] = '[';
  bytes[source_bytes + 2] = '3';
  bytes[source_bytes + 3] = '1';
  bytes[source_bytes + 4] = 'm';
  bytes[source_bytes + 5] = 'X';
  rewrite_payload_checksum(bytes);
  write_binary(path, bytes);
  xsql::artifacts::ArtifactInfo info = xsql::artifacts::inspect_artifact_file(path.string());
  std::string safe = xsql::cli::escape_control_for_terminal(info.source_uri);
  expect_true(safe.find('\x1B') == std::string::npos, "escaped text omits raw escape bytes");
  expect_true(safe.find("\\x1B") != std::string::npos, "escaped text renders escape visibly");
  std::filesystem::remove(path);
}

}  // namespace

void register_artifact_tests(std::vector<TestCase>& tests) {
  tests.push_back({"artifact_cli_args", test_artifact_cli_args});
  tests.push_back({"artifact_html_and_mqd_results_match", test_artifact_html_and_mqd_results_match});
  tests.push_back({"artifact_query_and_mqp_results_match", test_artifact_query_and_mqp_results_match});
  tests.push_back({"artifact_mqp_on_mqd_matches_direct", test_artifact_mqp_on_mqd_matches_direct});
  tests.push_back({"artifact_lint_behavior_unchanged", test_artifact_lint_behavior_unchanged});
  tests.push_back({"artifact_document_round_trip_is_deterministic",
                   test_artifact_document_round_trip_is_deterministic});
  tests.push_back({"artifact_query_round_trip_is_deterministic",
                   test_artifact_query_round_trip_is_deterministic});
  tests.push_back({"artifact_document_major_version_rejected",
                   test_artifact_document_major_version_rejected});
  tests.push_back({"artifact_query_major_version_rejected",
                   test_artifact_query_major_version_rejected});
  tests.push_back({"artifact_corrupted_files_fail_safely",
                   test_artifact_corrupted_files_fail_safely});
  tests.push_back({"artifact_inspect_reports_metadata",
                   test_artifact_inspect_reports_metadata});
  tests.push_back({"artifact_bad_magic_rejected", test_artifact_bad_magic_rejected});
  tests.push_back({"artifact_unknown_required_flag_rejected",
                   test_artifact_unknown_required_flag_rejected});
  tests.push_back({"artifact_checksum_mismatch_rejected",
                   test_artifact_checksum_mismatch_rejected});
  tests.push_back({"artifact_section_length_rejected", test_artifact_section_length_rejected});
  tests.push_back({"artifact_huge_node_count_rejected", test_artifact_huge_node_count_rejected});
  tests.push_back({"artifact_docn_flatbuffer_verifier_rejected",
                   test_artifact_docn_flatbuffer_verifier_rejected});
  tests.push_back({"artifact_node_count_metadata_mismatch_rejected",
                   test_artifact_node_count_metadata_mismatch_rejected});
  tests.push_back({"artifact_invalid_utf8_rejected", test_artifact_invalid_utf8_rejected});
  tests.push_back({"artifact_terminal_escape_sanitization",
                   test_artifact_terminal_escape_sanitization});
}
