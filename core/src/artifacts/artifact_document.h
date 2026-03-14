#pragma once

#include "artifact_internal.h"

namespace xsql::artifacts::detail {

uint64_t document_required_features();
bool document_uses_flatbuffers(const ArtifactHeader& header);

std::string build_document_meta_payload(const std::string& source_uri, size_t node_count);
void parse_document_meta(const std::string& payload, ArtifactInfo& info);

std::string build_document_nodes_payload(const HtmlDocument& document);
HtmlDocument parse_document_nodes(const ArtifactHeader& header, const std::string& payload);
void validate_document(const HtmlDocument& document);

}  // namespace xsql::artifacts::detail
