#include "helper_controller.h"

#include "helper_policy.h"

namespace markql::helper {

namespace {

FinalSuggestion build_final_suggestion(const ControllerSnapshot& snapshot, const std::string& status,
                                       const std::string& reason, Diagnosis diagnosis) {
  FinalSuggestion out;
  out.status = status;
  out.mode = snapshot.request.mode;
  out.query = snapshot.current_query;
  if (out.query.empty() && snapshot.has_model_decision) out.query = snapshot.model_decision.query;
  out.diagnosis = diagnosis;
  out.reason = reason;
  out.artifact_used = snapshot.has_artifact ? snapshot.artifact.kind : ArtifactKind::None;
  out.retrieval_topic =
      snapshot.has_retrieval_pack ? snapshot.retrieval_pack.topic : choose_retrieval_topic(snapshot);
  if (snapshot.has_lint_summary) out.lint_summary = snapshot.lint_summary;
  if (snapshot.has_execution_summary) out.execution_summary = snapshot.execution_summary;
  return out;
}

ControllerStep make_step(ControllerState state, StepActionKind action, const std::string& reason) {
  ControllerStep step;
  step.state = state;
  step.action = action;
  step.reason = reason;
  return step;
}

}  // namespace

ControllerStep plan_next_step(const ControllerSnapshot& snapshot) {
  if (snapshot.state == ControllerState::Done) {
    ControllerStep step = make_step(ControllerState::Done, StepActionKind::Done, "already done");
    step.final_suggestion =
        build_final_suggestion(snapshot, "done", "helper already reached DONE", Diagnosis::None);
    return step;
  }

  if (snapshot.state == ControllerState::Blocked) {
    ControllerStep step =
        make_step(ControllerState::Blocked, StepActionKind::Blocked, "already blocked");
    step.final_suggestion = build_final_suggestion(snapshot, "blocked",
                                                   "helper already reached BLOCKED",
                                                   Diagnosis::Unknown);
    return step;
  }

  if (!snapshot.has_artifact) {
    ControllerStep step = make_step(ControllerState::InspectCompact, StepActionKind::InspectArtifact,
                                    "start from compact families on the common path");
    step.requested_artifact = ArtifactKind::CompactFamilies;
    return step;
  }

  if (!snapshot.has_retrieval_pack) {
    ControllerStep step = make_step(ControllerState::RetrievePack,
                                    StepActionKind::BuildRetrievalPack,
                                    "build one tiny retrieval pack for the current step");
    step.requested_pack = choose_retrieval_topic(snapshot);
    return step;
  }

  if (snapshot.current_query.empty() && !snapshot.has_model_decision) {
    ControllerStep step =
        make_step(ControllerState::ModelSuggest, StepActionKind::CallModel,
                  "need one next query suggestion from the current goal and artifact");
    step.model_task = ModelTask::InterpretAndSuggest;
    return step;
  }

  if (snapshot.has_model_decision && snapshot.current_query.empty()) {
    if (snapshot.model_decision.status == ModelDecisionStatus::NeedMoreArtifact) {
      if (!can_escalate_artifact(snapshot.artifact.kind)) {
        ControllerStep step = make_step(
            ControllerState::Blocked, StepActionKind::Blocked,
            "model requested more artifact detail but full HTML is already the current level");
        step.final_suggestion = build_final_suggestion(
            snapshot, "blocked",
            "model requested more artifact detail but no further escalation is available",
            Diagnosis::ArtifactTooLossy);
        return step;
      }
      ControllerStep step = make_step(ControllerState::EscalateArtifact,
                                      StepActionKind::EscalateArtifact,
                                      "escalate one artifact level only");
      step.requested_artifact =
          snapshot.model_decision.requested_artifact != ArtifactKind::None
              ? snapshot.model_decision.requested_artifact
              : next_artifact_kind(snapshot.artifact.kind);
      return step;
    }
    if (snapshot.model_decision.status == ModelDecisionStatus::Blocked) {
      ControllerStep step =
          make_step(ControllerState::Blocked, StepActionKind::Blocked,
                    snapshot.model_decision.reason.empty() ? "model reported blocked"
                                                           : snapshot.model_decision.reason);
      step.final_suggestion = build_final_suggestion(snapshot, "blocked", step.reason,
                                                     snapshot.model_decision.diagnosis);
      return step;
    }
    if (snapshot.model_decision.status == ModelDecisionStatus::QueryReady &&
        !snapshot.model_decision.query.empty()) {
      ControllerStep step =
          make_step(ControllerState::LintQuery, StepActionKind::LintQuery,
                    "lint the single suggested query before execution");
      step.query = snapshot.model_decision.query;
      step.diagnosis = snapshot.model_decision.diagnosis;
      return step;
    }
  }

  if (!snapshot.current_query.empty() && !snapshot.has_lint_summary) {
    ControllerStep step =
        make_step(ControllerState::LintQuery, StepActionKind::LintQuery,
                  "lint the current query before execution");
    step.query = snapshot.current_query;
    return step;
  }

  if (snapshot.has_lint_summary && !snapshot.lint_summary.ok) {
    const RetrievalTopic desired_pack = choose_retrieval_topic(snapshot);
    if (!snapshot.has_retrieval_pack || snapshot.retrieval_pack.topic != desired_pack) {
      ControllerStep step = make_step(ControllerState::RetrievePack,
                                      StepActionKind::BuildRetrievalPack,
                                      "refresh the retrieval pack for the current lint failure");
      step.requested_pack = desired_pack;
      step.diagnosis = snapshot.lint_summary.category;
      return step;
    }
    if (snapshot.repair_loops >= 2) {
      ControllerStep step =
          make_step(ControllerState::Blocked, StepActionKind::Blocked,
                    "maximum repair loops reached after lint failure");
      step.final_suggestion = build_final_suggestion(snapshot, "blocked", step.reason,
                                                     snapshot.lint_summary.category);
      return step;
    }
    ControllerStep step =
        make_step(ControllerState::ModelRepair, StepActionKind::CallModel,
                  "repair the query shape from a short lint summary");
    step.model_task = ModelTask::RepairFromSummary;
    step.diagnosis = snapshot.lint_summary.category;
    return step;
  }

  if (snapshot.has_lint_summary && snapshot.lint_summary.ok && !snapshot.has_execution_summary) {
    ControllerStep step =
        make_step(ControllerState::ExecuteQuery, StepActionKind::ExecuteQuery,
                  "execute the lint-clean query to classify the next step");
    step.query = snapshot.current_query;
    return step;
  }

  if (snapshot.has_execution_summary && !snapshot.has_result_analysis) {
    ControllerStep step =
        make_step(ControllerState::AnalyzeResult, StepActionKind::AnalyzeResult,
                  "classify row scope vs field scope before any repair");
    return step;
  }

  if (snapshot.has_result_analysis) {
    if (snapshot.result_analysis.done) {
      ControllerStep step = make_step(ControllerState::Done, StepActionKind::Done,
                                      snapshot.result_analysis.reason);
      step.final_suggestion = build_final_suggestion(snapshot, "done",
                                                     snapshot.result_analysis.reason,
                                                     snapshot.result_analysis.diagnosis);
      return step;
    }
    if (snapshot.result_analysis.should_escalate) {
      if (!can_escalate_artifact(snapshot.artifact.kind)) {
        ControllerStep step =
            make_step(ControllerState::Blocked, StepActionKind::Blocked,
                      "artifact escalation requested but no further artifact level is available");
        step.final_suggestion = build_final_suggestion(snapshot, "blocked", step.reason,
                                                       Diagnosis::ArtifactTooLossy);
        return step;
      }
      ControllerStep step =
          make_step(ControllerState::EscalateArtifact, StepActionKind::EscalateArtifact,
                    snapshot.result_analysis.reason);
      step.requested_artifact = next_artifact_kind(snapshot.artifact.kind);
      step.diagnosis = snapshot.result_analysis.diagnosis;
      return step;
    }
    if (snapshot.result_analysis.should_repair) {
      const RetrievalTopic desired_pack = choose_retrieval_topic(snapshot);
      if (!snapshot.has_retrieval_pack || snapshot.retrieval_pack.topic != desired_pack) {
        ControllerStep step =
            make_step(ControllerState::RetrievePack, StepActionKind::BuildRetrievalPack,
                      "refresh the retrieval pack for the current diagnosis");
        step.requested_pack = desired_pack;
        step.diagnosis = snapshot.result_analysis.diagnosis;
        return step;
      }
      if (snapshot.repair_loops >= 2) {
        ControllerStep step =
            make_step(ControllerState::Blocked, StepActionKind::Blocked,
                      "maximum repair loops reached after deterministic analysis");
        step.final_suggestion = build_final_suggestion(snapshot, "blocked", step.reason,
                                                       snapshot.result_analysis.diagnosis);
        return step;
      }
      ControllerStep step =
          make_step(ControllerState::ModelRepair, StepActionKind::CallModel,
                    snapshot.result_analysis.reason);
      step.model_task = ModelTask::RepairFromSummary;
      step.diagnosis = snapshot.result_analysis.diagnosis;
      return step;
    }
  }

  ControllerStep step = make_step(ControllerState::Blocked, StepActionKind::Blocked,
                                  "controller could not determine the next bounded step");
  step.final_suggestion =
      build_final_suggestion(snapshot, "blocked", step.reason, Diagnosis::Unknown);
  return step;
}

}  // namespace markql::helper
