#pragma once

#include "../lang/ast.h"

#include <string>

namespace markql::artifacts::detail {

std::string build_prepared_query_payload_flatbuffers(const Query& query);
Query parse_prepared_query_payload_flatbuffers(const std::string& payload);

}  // namespace markql::artifacts::detail
