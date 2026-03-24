#include "artifacts.h"

#include "artifact_document.h"
#include "artifact_internal.h"
#include "artifact_query.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../lang/markql_parser.h"
#include "../runtime/engine/engine_execution_internal.h"

namespace markql::artifacts {

namespace {

using namespace detail;

ArtifactInfo inspect_artifact_bytes(const std::string& data, bool require_compatibility) {
  BinaryReader reader(data);
  ArtifactInfo info;
  info.header = read_header(reader);
  const std::string compatibility_error = compatibility_error_for_header(info.header);
  if (require_compatibility && !compatibility_error.empty()) {
    throw std::runtime_error(compatibility_error);
  }
  if (info.header.format_major != kArtifactFormatMajor ||
      (info.header.required_features & ~kKnownRequiredFeatures) != 0) {
    return info;
  }
  const std::vector<SectionView> sections = read_sections(reader, info.header);
  const SectionView* meta = find_section(sections, "META");
  ensure(meta != nullptr, "Corrupted artifact: missing META section");

  if (info.header.kind == ArtifactKind::DocumentSnapshot) {
    parse_document_meta(meta->payload, info);
  } else if (info.header.kind == ArtifactKind::PreparedQuery) {
    parse_prepared_meta(meta->payload, info);
  }
  return info;
}

}  // namespace

std::string artifact_compatibility_error(const ArtifactHeader& header) {
  return detail::compatibility_error_for_header(header);
}

bool path_has_artifact_magic(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  char magic[4] = {};
  in.read(magic, sizeof(magic));
  if (in.gcount() != static_cast<std::streamsize>(sizeof(magic))) return false;
  return detail::kind_from_magic(std::string(magic, sizeof(magic))) != ArtifactKind::None;
}

ArtifactInfo inspect_artifact_file(const std::string& path) {
  return inspect_artifact_bytes(detail::read_file_bytes(path), false);
}

void write_document_artifact_file(const HtmlDocument& document, const std::string& source_uri,
                                  const std::string& path) {
  detail::validate_document(document);
  std::vector<detail::SectionView> sections;
  sections.push_back(detail::SectionView{
      "META", detail::build_document_meta_payload(source_uri, document.nodes.size())});
  sections.push_back(detail::SectionView{"DOCN", detail::build_document_nodes_payload(document)});
  detail::write_file_bytes(
      path,
      detail::build_artifact_bytes(ArtifactKind::DocumentSnapshot, detail::current_producer_major(),
                                   detail::document_required_features(), sections));
}

DocumentArtifact read_document_artifact_file(const std::string& path) {
  const std::string data = detail::read_file_bytes(path);
  DocumentArtifact artifact;
  artifact.info = inspect_artifact_bytes(data, true);
  detail::ensure(artifact.info.header.kind == ArtifactKind::DocumentSnapshot,
                 "Unsupported artifact: expected .mqd document snapshot");
  detail::BinaryReader reader(data);
  (void)detail::read_header(reader);
  const std::vector<detail::SectionView> sections =
      detail::read_sections(reader, artifact.info.header);
  const detail::SectionView* nodes = detail::find_section(sections, "DOCN");
  detail::ensure(nodes != nullptr, "Corrupted artifact: missing DOCN section");
  artifact.document = detail::parse_document_nodes(artifact.info.header, nodes->payload);
  detail::ensure(artifact.document.nodes.size() == artifact.info.node_count,
                 "Corrupted artifact: node count metadata mismatch");
  return artifact;
}

void write_prepared_query_artifact_file(const Query& query, const std::string& original_query,
                                        const std::string& path) {
  std::vector<detail::SectionView> sections;
  sections.push_back(
      detail::SectionView{"META", detail::build_prepared_meta_payload(query, original_query)});
  sections.push_back(detail::SectionView{"QAST", detail::build_prepared_query_payload(query)});
  detail::write_file_bytes(path, detail::build_artifact_bytes(
                                     ArtifactKind::PreparedQuery, detail::current_producer_major(),
                                     detail::prepared_query_required_features(), sections));
}

PreparedQueryArtifact prepare_query_artifact(const std::string& query_text) {
  auto parsed = parse_query(query_text);
  if (!parsed.query.has_value()) {
    throw std::runtime_error("Query parse error: " + parsed.error->message);
  }
  validate_query_for_execution(*parsed.query);

  PreparedQueryArtifact artifact;
  artifact.info.header.kind = ArtifactKind::PreparedQuery;
  artifact.info.header.format_major = detail::kArtifactFormatMajor;
  artifact.info.header.format_minor = detail::kArtifactFormatMinor;
  artifact.info.header.section_count = 2;
  artifact.info.header.required_features = detail::prepared_query_required_features();
  artifact.info.header.producer_major = detail::current_producer_major();
  artifact.info.header.language_major = detail::current_language_major();
  artifact.info.producer_version = markql::get_version_info().version;
  artifact.info.language_version = markql::get_version_info().version;
  artifact.info.original_query = query_text;
  artifact.info.query_kind = parsed.query->kind;
  artifact.info.source_kind = parsed.query->source.kind;
  artifact.info.metadata_available = true;
  artifact.query = *parsed.query;
  return artifact;
}

PreparedQueryArtifact read_prepared_query_artifact_file(const std::string& path) {
  const std::string data = detail::read_file_bytes(path);
  PreparedQueryArtifact artifact;
  artifact.info = inspect_artifact_bytes(data, true);
  detail::ensure(artifact.info.header.kind == ArtifactKind::PreparedQuery,
                 "Unsupported artifact: expected .mqp prepared query");
  detail::BinaryReader reader(data);
  (void)detail::read_header(reader);
  const std::vector<detail::SectionView> sections =
      detail::read_sections(reader, artifact.info.header);
  const detail::SectionView* query_payload = detail::find_section(sections, "QAST");
  detail::ensure(query_payload != nullptr, "Corrupted artifact: missing QAST section");
  artifact.query =
      detail::parse_prepared_query_payload(artifact.info.header, query_payload->payload);
  validate_query_for_execution(artifact.query);
  detail::ensure(artifact.query.kind == artifact.info.query_kind,
                 "Corrupted artifact: query metadata mismatch");
  detail::ensure(artifact.query.source.kind == artifact.info.source_kind,
                 "Corrupted artifact: source metadata mismatch");
  return artifact;
}

QueryResult execute_prepared_query_on_html(const PreparedQueryArtifact& artifact,
                                           const std::string& html, const std::string& source_uri) {
  if (artifact.query.kind != Query::Kind::Select) {
    return execute_meta_query(artifact.query, source_uri);
  }
  return execute_query_with_source(artifact.query, &html, nullptr, source_uri);
}

QueryResult execute_prepared_query_on_document(const PreparedQueryArtifact& artifact,
                                               const DocumentArtifact& document) {
  if (artifact.query.kind != Query::Kind::Select) {
    return execute_meta_query(artifact.query, document.info.source_uri);
  }
  return execute_query_with_source(artifact.query, nullptr, &document.document,
                                   document.info.source_uri);
}

QueryResult execute_query_text_on_document(const std::string& query_text,
                                           const DocumentArtifact& document) {
  auto parsed = parse_query(query_text);
  if (!parsed.query.has_value()) {
    throw std::runtime_error("Query parse error: " + parsed.error->message);
  }
  validate_query_for_execution(*parsed.query);
  if (parsed.query->kind != Query::Kind::Select) {
    return execute_meta_query(*parsed.query, document.info.source_uri);
  }
  return execute_query_with_source(*parsed.query, nullptr, &document.document,
                                   document.info.source_uri);
}

}  // namespace markql::artifacts
