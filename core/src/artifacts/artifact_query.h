#pragma once

#include "artifacts.h"

namespace xsql::artifacts::detail {

uint64_t prepared_query_required_features();
bool prepared_query_uses_flatbuffers(const ArtifactHeader& header);
std::string build_prepared_meta_payload(const Query& query, const std::string& original_query);
void parse_prepared_meta(const std::string& payload, ArtifactInfo& info);
std::string build_prepared_query_payload(const Query& query);
Query parse_prepared_query_payload(const ArtifactHeader& header, const std::string& payload);

}  // namespace xsql::artifacts::detail
