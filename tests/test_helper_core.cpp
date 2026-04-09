#include <vector>

#include "test_harness.h"

#include "helper/helper_controller.h"
#include "helper/helper_policy.h"
#include "helper/helper_result_analysis.h"

namespace {

using namespace markql::helper;

void test_helper_analysis_classifies_null_fields_as_field_scope() {
  HelperRequest request;
  request.mode = HelperMode::Repair;
  ArtifactSummary artifact;
  artifact.kind = ArtifactKind::Families;
  artifact.lossy = false;
  ExecutionSummary exec;
  exec.ok = true;
  exec.has_rows = true;
  exec.row_count = 3;
  exec.null_field_count = 2;
  ResultAnalysis analysis = analyze_result(request, artifact, nullptr, &exec);
  expect_true(analysis.category == AnalysisCategory::RightRowsNullFields,
              "null fields classify as right_rows_null_fields");
  expect_true(analysis.diagnosis == Diagnosis::FieldScope,
              "null fields classify as field_scope");
}

void test_helper_analysis_escalates_lossy_empty_artifact() {
  HelperRequest request;
  ArtifactSummary artifact;
  artifact.kind = ArtifactKind::CompactFamilies;
  artifact.lossy = true;
  ExecutionSummary exec;
  exec.ok = true;
  exec.has_rows = false;
  exec.row_count = 0;
  ResultAnalysis analysis = analyze_result(request, artifact, nullptr, &exec);
  expect_true(analysis.category == AnalysisCategory::ArtifactTooLossy,
              "lossy empty artifact requests escalation");
  expect_true(analysis.should_escalate, "lossy empty artifact sets should_escalate");
}

void test_helper_controller_starts_with_compact_artifact() {
  ControllerSnapshot snapshot;
  snapshot.request.goal_text = "extract title";
  ControllerStep step = plan_next_step(snapshot);
  expect_true(step.action == StepActionKind::InspectArtifact,
              "controller starts by inspecting artifact");
  expect_true(step.requested_artifact == ArtifactKind::CompactFamilies,
              "controller requests compact families first");
}

void test_helper_controller_requests_model_after_pack() {
  ControllerSnapshot snapshot;
  snapshot.request.goal_text = "extract title";
  snapshot.has_artifact = true;
  snapshot.artifact.kind = ArtifactKind::CompactFamilies;
  snapshot.artifact.lossy = true;
  snapshot.has_retrieval_pack = true;
  snapshot.retrieval_pack = build_retrieval_pack(RetrievalTopic::RowSelection);
  ControllerStep step = plan_next_step(snapshot);
  expect_true(step.action == StepActionKind::CallModel, "controller asks model for first query");
  expect_true(step.model_task == ModelTask::InterpretAndSuggest,
              "first model task is interpret_and_suggest");
}

void test_helper_controller_repairs_after_lint_failure() {
  ControllerSnapshot snapshot;
  snapshot.request.goal_text = "extract title";
  snapshot.has_artifact = true;
  snapshot.artifact.kind = ArtifactKind::CompactFamilies;
  snapshot.artifact.lossy = true;
  snapshot.has_retrieval_pack = true;
  snapshot.retrieval_pack = build_retrieval_pack(RetrievalTopic::Grammar);
  snapshot.current_query = "SELECT FROM doc";
  snapshot.has_lint_summary = true;
  snapshot.lint_summary.ok = false;
  snapshot.lint_summary.category = Diagnosis::Grammar;
  snapshot.lint_summary.headline = "Missing projection after SELECT";
  ControllerStep step = plan_next_step(snapshot);
  expect_true(step.action == StepActionKind::CallModel, "lint failure triggers model repair");
  expect_true(step.model_task == ModelTask::RepairFromSummary,
              "lint failure uses repair_from_summary");
  expect_true(step.diagnosis == Diagnosis::Grammar, "lint failure keeps grammar diagnosis");
}

void test_helper_controller_done_after_likely_success() {
  ControllerSnapshot snapshot;
  snapshot.request.goal_text = "extract title";
  snapshot.current_query = "SELECT title FROM doc";
  snapshot.has_artifact = true;
  snapshot.artifact.kind = ArtifactKind::Families;
  snapshot.has_retrieval_pack = true;
  snapshot.retrieval_pack = build_retrieval_pack(RetrievalTopic::StableExtraction);
  snapshot.has_lint_summary = true;
  snapshot.lint_summary.ok = true;
  snapshot.has_execution_summary = true;
  snapshot.execution_summary.ok = true;
  snapshot.execution_summary.has_rows = true;
  snapshot.execution_summary.row_count = 2;
  snapshot.has_result_analysis = true;
  snapshot.result_analysis.done = true;
  snapshot.result_analysis.reason = "query looks like a good next step";
  ControllerStep step = plan_next_step(snapshot);
  expect_true(step.action == StepActionKind::Done, "likely success terminates helper");
  expect_true(step.final_suggestion.status == "done", "final suggestion status is done");
}

}  // namespace

void register_helper_core_tests(std::vector<TestCase>& tests) {
  tests.push_back({"helper_analysis_classifies_null_fields_as_field_scope",
                   test_helper_analysis_classifies_null_fields_as_field_scope});
  tests.push_back({"helper_analysis_escalates_lossy_empty_artifact",
                   test_helper_analysis_escalates_lossy_empty_artifact});
  tests.push_back({"helper_controller_starts_with_compact_artifact",
                   test_helper_controller_starts_with_compact_artifact});
  tests.push_back({"helper_controller_requests_model_after_pack",
                   test_helper_controller_requests_model_after_pack});
  tests.push_back({"helper_controller_repairs_after_lint_failure",
                   test_helper_controller_repairs_after_lint_failure});
  tests.push_back({"helper_controller_done_after_likely_success",
                   test_helper_controller_done_after_likely_success});
}
