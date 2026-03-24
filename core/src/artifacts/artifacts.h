#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "../dom/html_parser.h"
#include "../lang/ast.h"
#include "markql/markql.h"
#include "markql/version.h"

namespace markql::artifacts {

enum class ArtifactKind {
  None,
  DocumentSnapshot,
  PreparedQuery,
};

struct ArtifactHeader {
  ArtifactKind kind = ArtifactKind::None;
  uint16_t format_major = 0;
  uint16_t format_minor = 0;
  uint32_t section_count = 0;
  uint64_t required_features = 0;
  uint32_t producer_major = 0;
  uint32_t language_major = 0;
  uint64_t payload_checksum = 0;
  uint64_t payload_bytes = 0;
};

struct ArtifactInfo {
  ArtifactHeader header;
  bool metadata_available = false;
  std::string producer_version;
  std::string language_version;
  std::string source_uri;
  std::string original_query;
  Query::Kind query_kind = Query::Kind::Select;
  Source::Kind source_kind = Source::Kind::Document;
  size_t node_count = 0;
};

struct DocumentArtifact {
  ArtifactInfo info;
  HtmlDocument document;
};

struct PreparedQueryArtifact {
  ArtifactInfo info;
  Query query;
};

std::string artifact_compatibility_error(const ArtifactHeader& header);
bool path_has_artifact_magic(const std::string& path);
ArtifactInfo inspect_artifact_file(const std::string& path);

void write_document_artifact_file(const HtmlDocument& document, const std::string& source_uri,
                                  const std::string& path);
DocumentArtifact read_document_artifact_file(const std::string& path);

void write_prepared_query_artifact_file(const Query& query, const std::string& original_query,
                                        const std::string& path);
PreparedQueryArtifact read_prepared_query_artifact_file(const std::string& path);

PreparedQueryArtifact prepare_query_artifact(const std::string& query_text);

QueryResult execute_prepared_query_on_html(const PreparedQueryArtifact& artifact,
                                           const std::string& html, const std::string& source_uri);
QueryResult execute_prepared_query_on_document(const PreparedQueryArtifact& artifact,
                                               const DocumentArtifact& document);
QueryResult execute_query_text_on_document(const std::string& query_text,
                                           const DocumentArtifact& document);

}  // namespace markql::artifacts
