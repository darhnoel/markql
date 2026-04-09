#pragma once

#include "helper_types.h"

namespace markql::helper {

ResultAnalysis analyze_result(const HelperRequest& request, const ArtifactSummary& artifact,
                              const LintSummary* lint_summary,
                              const ExecutionSummary* execution_summary);

}  // namespace markql::helper
