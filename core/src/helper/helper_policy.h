#pragma once

#include <string>
#include <string_view>

#include "helper_types.h"

namespace markql::helper {

std::string to_string(HelperMode value);
std::string to_string(ArtifactKind value);
std::string to_string(ControllerState value);
std::string to_string(Diagnosis value);
std::string to_string(AnalysisCategory value);
std::string to_string(RetrievalTopic value);
std::string to_string(ModelDecisionStatus value);
std::string to_string(ModelTask value);
std::string to_string(StepActionKind value);

ArtifactKind next_artifact_kind(ArtifactKind current);
bool can_escalate_artifact(ArtifactKind current);
bool constraint_contains(const HelperRequest& request, std::string_view needle);
RetrievalTopic choose_retrieval_topic(const ControllerSnapshot& snapshot);
RetrievalPack build_retrieval_pack(RetrievalTopic topic);

}  // namespace markql::helper
