#include "markql/markql.h"

#include <stdexcept>
#include <string>

#include "../../artifacts/artifacts.h"
#include "../../dom/html_parser.h"
#include "../../lang/markql_parser.h"
#include "../../util/string_util.h"
#include "engine_execution_internal.h"
#include "markql_internal.h"

namespace markql {

namespace {

bool is_plain_count_star_document_query(const Query& query) {
  if (query.kind != Query::Kind::Select) return false;
  if (query.source.kind != Source::Kind::Document) return false;
  if (query.with.has_value()) return false;
  if (!query.joins.empty()) return false;
  if (query.where.has_value()) return false;
  if (!query.order_by.empty()) return false;
  if (!query.exclude_fields.empty()) return false;
  if (query.limit.has_value()) return false;
  if (query.to_list || query.to_table) return false;
  if (query.export_sink.has_value()) return false;
  if (query.select_items.size() != 1) return false;
  const auto& item = query.select_items.front();
  return item.aggregate == Query::SelectItem::Aggregate::Count && item.tag == "*";
}

QueryResult build_count_star_result(const Query& query,
                                    int64_t count,
                                    const std::string& source_uri) {
  QueryResult out;
  out.columns = markql_internal::build_columns(query);
  out.columns_implicit = !markql_internal::is_projection_query(query);
  out.to_list = query.to_list;
  out.to_table = query.to_table;
  out.table_has_header = query.table_has_header;
  out.table_options = to_result_table_options(query.table_options);
  QueryResultRow row;
  row.node_id = count;
  row.source_uri = source_uri;
  out.rows.push_back(std::move(row));
  return out;
}

}  // namespace

QueryResult execute_query_with_source_legacy(const Query& query,
                                             const std::string* default_html,
                                             const HtmlDocument* default_document,
                                             const std::string& default_source_uri) {
  std::string effective_source_uri = default_source_uri;
  if (is_plain_count_star_document_query(query)) {
    // WHY: COUNT(*) FROM doc does not need per-node inner_html/text materialization.
    int64_t count = default_document != nullptr
                        ? static_cast<int64_t>(default_document->nodes.size())
                        : count_html_nodes_fast(*default_html);
    return build_count_star_result(query, count, effective_source_uri);
  }
  if (query.source.kind == Source::Kind::RawHtml) {
    if (query.source.value.size() > markql_internal::kMaxRawHtmlBytes) {
      throw std::runtime_error("RAW() HTML exceeds maximum size");
    }
    HtmlDocument doc = parse_html(query.source.value);
    effective_source_uri = "raw";
    return execute_query_ast(query, doc, effective_source_uri);
  }
  if (query.source.kind == Source::Kind::Fragments) {
    FragmentSource fragments;
    if (query.source.fragments_raw.has_value()) {
      if (query.source.fragments_raw->size() > markql_internal::kMaxRawHtmlBytes) {
        throw std::runtime_error("FRAGMENTS RAW() input exceeds maximum size");
      }
      fragments.fragments.push_back(*query.source.fragments_raw);
    } else if (query.source.fragments_query != nullptr) {
      const Query& subquery = *query.source.fragments_query;
      validate_query_for_execution(subquery);
      if (subquery.source.kind == Source::Kind::Path || subquery.source.kind == Source::Kind::Url) {
        throw std::runtime_error("FRAGMENTS subquery cannot use URL or file sources");
      }
      QueryResult sub_result =
          execute_query_with_source(subquery, default_html, default_document, default_source_uri);
      fragments = collect_html_fragments(sub_result, "FRAGMENTS");
    } else {
      throw std::runtime_error("FRAGMENTS requires a subquery or RAW('<...>') input");
    }
    HtmlDocument doc = build_fragments_document(fragments);
    effective_source_uri = "fragment";
    QueryResult out = execute_query_ast(query, doc, effective_source_uri);
    out.warnings.push_back("FRAGMENTS is deprecated; use PARSE(...) instead.");
    return out;
  }
  if (query.source.kind == Source::Kind::Parse) {
    FragmentSource fragments;
    if (query.source.parse_expr != nullptr) {
      std::optional<std::string> value = eval_parse_source_expr(*query.source.parse_expr);
      if (!value.has_value()) {
        throw std::runtime_error("PARSE() requires a non-null HTML string expression");
      }
      std::string trimmed = util::trim_ws(*value);
      if (trimmed.empty()) {
        throw std::runtime_error("PARSE() produced no HTML fragments");
      }
      if (!looks_like_html_fragment(trimmed)) {
        throw std::runtime_error("PARSE() expects an HTML string expression");
      }
      if (trimmed.size() > markql_internal::kMaxFragmentHtmlBytes) {
        throw std::runtime_error("PARSE() HTML fragment exceeds maximum size");
      }
      fragments.fragments.push_back(std::move(trimmed));
    } else if (query.source.parse_query != nullptr) {
      const Query& subquery = *query.source.parse_query;
      validate_query_for_execution(subquery);
      if (subquery.source.kind == Source::Kind::Path || subquery.source.kind == Source::Kind::Url) {
        throw std::runtime_error("PARSE() subquery cannot use URL or file sources");
      }
      QueryResult sub_result =
          execute_query_with_source(subquery, default_html, default_document, default_source_uri);
      fragments = collect_html_fragments(sub_result, "PARSE()");
    } else {
      throw std::runtime_error("PARSE() requires a scalar expression or subquery input");
    }
    HtmlDocument doc = build_fragments_document(fragments);
    effective_source_uri = "parse";
    return execute_query_ast(query, doc, effective_source_uri);
  }
  HtmlDocument doc = default_document != nullptr ? *default_document : parse_html(*default_html);
  return execute_query_ast(query, doc, effective_source_uri);
}

/// Executes a parsed query over provided HTML and assembles QueryResult.
/// MUST apply validation before execution and MUST propagate errors as exceptions.
/// Inputs are HTML/source/query; outputs are QueryResult with no side effects.
QueryResult execute_query_from_html(const std::string& html,
                                    const std::string& source_uri,
                                    const std::string& query) {
  auto parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    throw std::runtime_error("Query parse error: " + parsed.error->message);
  }
  validate_query_for_execution(*parsed.query);
  if (parsed.query->kind != Query::Kind::Select) {
    return execute_meta_query(*parsed.query, source_uri);
  }
  return execute_query_with_source(*parsed.query, &html, nullptr, source_uri);
}

/// Executes a query over in-memory HTML with document as the source label.
/// MUST not perform IO and MUST propagate parse/validation failures.
/// Inputs are HTML/query; outputs are QueryResult with no side effects.
QueryResult execute_query_from_document(const std::string& html, const std::string& query) {
  return execute_query_from_html(html, "document", query);
}

/// Executes a query over a file and uses the path as source label.
/// MUST read from disk and MUST propagate IO failures as exceptions.
/// Inputs are path/query; outputs are QueryResult with file IO side effects.
QueryResult execute_query_from_file(const std::string& path, const std::string& query) {
  if (artifacts::path_has_artifact_magic(path)) {
    artifacts::ArtifactInfo info = artifacts::inspect_artifact_file(path);
    if (info.header.kind == artifacts::ArtifactKind::DocumentSnapshot) {
      artifacts::DocumentArtifact document = artifacts::read_document_artifact_file(path);
      return artifacts::execute_query_text_on_document(query, document);
    }
    throw std::runtime_error("Prepared query artifacts (.mqp) cannot be used as input documents");
  }
  std::string html = markql_internal::read_file(path);
  return execute_query_from_html(html, path, query);
}

/// Executes a query over a URL and uses the URL as source label.
/// MUST honor timeout_ms and MUST propagate network failures as exceptions.
/// Inputs are url/query/timeout; outputs are QueryResult with network side effects.
QueryResult execute_query_from_url(const std::string& url, const std::string& query, int timeout_ms) {
  std::string html = markql_internal::fetch_url(url, timeout_ms);
  return execute_query_from_html(html, url, query);
}

}  // namespace markql
