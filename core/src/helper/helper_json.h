#pragma once

#include <string>

#include "helper_types.h"

namespace markql::helper {

std::string to_json(const RetrievalPack& pack);
std::string to_json(const ModelDecision& decision);
std::string to_json(const FinalSuggestion& suggestion);
std::string to_json(const ControllerStep& step);

}  // namespace markql::helper
