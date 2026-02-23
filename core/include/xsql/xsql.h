#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xsql {

struct ParsedDocumentHandle;

/// Represents a single materialized row so callers can format or export results consistently.
/// MUST keep fields aligned with the executor/output contract to avoid schema drift.
/// Inputs/outputs are the row fields; side effects are none but consumers may rely on defaults.
struct QueryResultRow {
  int64_t node_id = 0;
  std::string tag;
  std::string text;
  std::string inner_html;
  std::unordered_map<std::string, double> term_scores;
  std::unordered_map<std::string, std::string> attributes;
  std::unordered_map<std::string, std::string> computed_fields;
  std::optional<int64_t> parent_id;
  int64_t sibling_pos = 0;
  int64_t max_depth = 0;
  int64_t doc_order = 0;
  std::string source_uri;
};

/// Carries the full query output, including row sets, tables, and export intent.
/// MUST keep columns consistent with rows/tables and MUST NOT mix incompatible modes.
/// Inputs/outputs are populated by execution; side effects include downstream export behavior.
struct QueryResult {
  struct TableOptions {
    enum class TrimEmptyCols { Off, Trailing, All } trim_empty_cols = TrimEmptyCols::Off;
    enum class EmptyIs { BlankOrNull, NullOnly, BlankOnly } empty_is = EmptyIs::BlankOrNull;
    enum class Format { Rect, Sparse } format = Format::Rect;
    enum class SparseShape { Long, Wide } sparse_shape = SparseShape::Long;
    bool trim_empty_rows = false;
    size_t stop_after_empty_rows = 0;
    bool header_normalize = true;
    bool header_normalize_explicit = false;
  };
  struct ExportSink {
    /// Describes an export target so the CLI can write files without re-parsing the query.
    /// MUST use Kind::None to indicate no export and MUST carry a valid path otherwise.
    /// Inputs are kind/path; side effects occur when the CLI performs the write.
    enum class Kind { None, Csv, Parquet, Json, Ndjson } kind = Kind::None;
    // WHY: defaulting to None prevents accidental file writes when a sink is not specified.
    std::string path;
  };
  std::vector<std::string> columns;
  std::vector<QueryResultRow> rows;
  /// True when output columns come from implicit defaults (e.g., SELECT * or tag-only).
  bool columns_implicit = false;
  /// True when EXCLUDE explicitly removed source_uri in the query.
  bool source_uri_excluded = false;
  bool to_list = false;
  struct TableResult {
    /// Holds extracted HTML table rows when TO TABLE() is requested.
    /// MUST preserve row order from the source table and MUST NOT mutate cell contents.
    /// Inputs are node_id/rows; side effects are none but output formatting depends on them.
    int64_t node_id = 0;
    std::vector<std::string> headers;
    std::vector<std::string> header_keys;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<std::pair<std::string, std::string>>> sparse_wide_rows;
  };
  std::vector<TableResult> tables;
  bool to_table = false;
  bool table_has_header = true;
  TableOptions table_options;
  ExportSink export_sink;
};

/// Executes a query over an in-memory HTML document for zero-IO operation.
/// MUST receive valid HTML and MUST treat the input as immutable.
/// Inputs are HTML/query; failures throw exceptions and side effects are none.
QueryResult execute_query_from_document(const std::string& html, const std::string& query);
/// Parses HTML once and returns a reusable handle for repeated query execution.
std::shared_ptr<const ParsedDocumentHandle> prepare_document(const std::string& html,
                                                             const std::string& source_uri = "document");
/// Executes a query using a prepared document handle.
QueryResult execute_query_from_prepared_document(const std::shared_ptr<const ParsedDocumentHandle>& prepared,
                                                 const std::string& query);
/// Executes a query over a file path and loads the file contents internally.
/// MUST read from disk and MUST report errors via exceptions on IO failures.
/// Inputs are path/query; side effects include file reads and thrown errors.
QueryResult execute_query_from_file(const std::string& path, const std::string& query);
/// Executes a query over a URL using the configured network backend.
/// MUST honor timeout_ms and MUST fail if network support is unavailable.
/// Inputs are url/query/timeout; side effects include network IO and thrown errors.
QueryResult execute_query_from_url(const std::string& url, const std::string& query, int timeout_ms);

}  // namespace xsql
