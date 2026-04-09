#include "helper_result_analysis.h"

namespace markql::helper {

ResultAnalysis analyze_result(const HelperRequest& request, const ArtifactSummary& artifact,
                              const LintSummary* lint_summary,
                              const ExecutionSummary* execution_summary) {
  (void)request;
  ResultAnalysis analysis;
  if (lint_summary != nullptr && !lint_summary->ok) {
    if (lint_summary->category == Diagnosis::Grammar) {
      analysis.category = AnalysisCategory::GrammarFailure;
      analysis.diagnosis = Diagnosis::Grammar;
      analysis.reason = lint_summary->headline.empty() ? "grammar failure during lint"
                                                       : lint_summary->headline;
      analysis.should_repair = true;
      return analysis;
    }
    analysis.category = AnalysisCategory::SemanticFailure;
    analysis.diagnosis = Diagnosis::Semantic;
    analysis.reason = lint_summary->headline.empty() ? "semantic or lint failure during lint"
                                                     : lint_summary->headline;
    analysis.should_repair = true;
    return analysis;
  }

  if (execution_summary == nullptr) {
    analysis.category = AnalysisCategory::LikelySuccess;
    analysis.diagnosis = Diagnosis::None;
    analysis.reason = "no execution summary available yet";
    analysis.should_execute = true;
    return analysis;
  }

  if (!execution_summary->ok) {
    analysis.category = AnalysisCategory::SemanticFailure;
    analysis.diagnosis = Diagnosis::Semantic;
    analysis.reason = execution_summary->headline.empty() ? "execution failed"
                                                          : execution_summary->headline;
    analysis.should_repair = true;
    return analysis;
  }

  if (artifact.lossy && !execution_summary->has_rows &&
      artifact.kind != ArtifactKind::FullHtml) {
    analysis.category = AnalysisCategory::ArtifactTooLossy;
    analysis.diagnosis = Diagnosis::ArtifactTooLossy;
    analysis.reason = "current artifact is too lossy to explain the empty result safely";
    analysis.should_escalate = true;
    return analysis;
  }

  if (execution_summary->expected_row_count.has_value() &&
      execution_summary->row_count != *execution_summary->expected_row_count) {
    analysis.category = AnalysisCategory::WrongRowCount;
    analysis.diagnosis = Diagnosis::RowScope;
    analysis.reason = "row count does not match expectation; repair row scope first";
    analysis.should_repair = true;
    return analysis;
  }

  if (!execution_summary->has_rows || execution_summary->row_count == 0) {
    analysis.category = AnalysisCategory::EmptyOutput;
    analysis.diagnosis = Diagnosis::RowScope;
    analysis.reason = "query returned no rows; repair row scope before field logic";
    analysis.should_repair = true;
    return analysis;
  }

  if (execution_summary->null_field_count > 0 && execution_summary->blank_field_count > 0) {
    analysis.category = AnalysisCategory::MixedInstability;
    analysis.diagnosis = Diagnosis::Mixed;
    analysis.reason = "rows exist but field output is mixed; supplier logic or artifact detail is unstable";
    analysis.should_repair = true;
    return analysis;
  }

  if (execution_summary->null_field_count > 0) {
    analysis.category = AnalysisCategory::RightRowsNullFields;
    analysis.diagnosis = Diagnosis::FieldScope;
    analysis.reason = "rows look present but fields are NULL; repair supplier logic";
    analysis.should_repair = true;
    return analysis;
  }

  if (execution_summary->blank_field_count > 0 && artifact.lossy &&
      artifact.kind != ArtifactKind::FullHtml) {
    analysis.category = AnalysisCategory::ArtifactTooLossy;
    analysis.diagnosis = Diagnosis::ArtifactTooLossy;
    analysis.reason = "blank fields on a lossy artifact suggest one-step artifact escalation";
    analysis.should_escalate = true;
    return analysis;
  }

  analysis.category = AnalysisCategory::LikelySuccess;
  analysis.diagnosis = Diagnosis::None;
  analysis.reason = "query looks like a good next step";
  analysis.done = true;
  return analysis;
}

}  // namespace markql::helper
