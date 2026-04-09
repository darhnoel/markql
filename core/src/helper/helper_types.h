#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace markql::helper {

enum class HelperMode {
  Start,
  Repair,
  Explain,
};

enum class ArtifactKind {
  None,
  CompactFamilies,
  Families,
  Skeleton,
  TargetedSubtree,
  FullHtml,
};

enum class ControllerState {
  Start,
  InspectCompact,
  ChoosePath,
  RetrievePack,
  ModelSuggest,
  LintQuery,
  ExecuteQuery,
  AnalyzeResult,
  ModelRepair,
  EscalateArtifact,
  Done,
  Blocked,
};

enum class Diagnosis {
  None,
  RowScope,
  FieldScope,
  Grammar,
  Semantic,
  ArtifactTooLossy,
  Mixed,
  Unknown,
};

enum class AnalysisCategory {
  GrammarFailure,
  SemanticFailure,
  EmptyOutput,
  WrongRowCount,
  RightRowsNullFields,
  MixedInstability,
  LikelySuccess,
  ArtifactTooLossy,
};

enum class RetrievalTopic {
  RowSelection,
  Exploration,
  Stabilization,
  StableExtraction,
  Repair,
  Grammar,
  NullAndScope,
};

enum class ModelDecisionStatus {
  None,
  QueryReady,
  NeedMoreArtifact,
  Blocked,
};

enum class ModelTask {
  None,
  InterpretAndSuggest,
  RepairFromSummary,
};

enum class StepActionKind {
  InspectArtifact,
  BuildRetrievalPack,
  CallModel,
  LintQuery,
  ExecuteQuery,
  AnalyzeResult,
  EscalateArtifact,
  Done,
  Blocked,
};

struct HelperRequest {
  HelperMode mode = HelperMode::Start;
  std::string input_path;
  std::string goal_text;
  std::string query;
  std::vector<std::string> constraints;
  std::vector<std::string> expected_fields;
};

struct Intent {
  HelperMode mode = HelperMode::Start;
  std::vector<std::string> fields;
  std::vector<std::string> constraints;
  std::string goal_text;
};

struct ArtifactSummary {
  ArtifactKind kind = ArtifactKind::None;
  std::string content;
  std::string selector_or_scope;
  std::string family_hint;
  bool lossy = false;
  std::string source;
};

struct RetrievalPack {
  RetrievalTopic topic = RetrievalTopic::RowSelection;
  std::string summary;
  std::vector<std::string> facts;
  std::vector<std::string> examples;
  std::vector<std::string> doc_refs;
};

struct LintSummary {
  bool ok = true;
  Diagnosis category = Diagnosis::None;
  int error_count = 0;
  int warning_count = 0;
  std::string headline;
  std::string details;
};

struct ExecutionSummary {
  bool ok = true;
  std::size_t row_count = 0;
  std::size_t null_field_count = 0;
  std::size_t blank_field_count = 0;
  bool has_rows = false;
  std::optional<std::size_t> expected_row_count;
  std::string headline;
  std::string details;
  std::vector<std::unordered_map<std::string, std::string>> sample_rows;
};

struct ResultAnalysis {
  AnalysisCategory category = AnalysisCategory::LikelySuccess;
  Diagnosis diagnosis = Diagnosis::None;
  std::string reason;
  bool should_repair = false;
  bool should_escalate = false;
  bool should_execute = false;
  bool done = false;
};

struct ModelDecision {
  ModelDecisionStatus status = ModelDecisionStatus::None;
  Diagnosis diagnosis = Diagnosis::None;
  std::string reason;
  std::string chosen_family;
  ArtifactKind requested_artifact = ArtifactKind::None;
  std::string query;
  std::string next_action;
};

struct FinalSuggestion {
  std::string status;
  HelperMode mode = HelperMode::Start;
  std::string query;
  Diagnosis diagnosis = Diagnosis::None;
  std::string reason;
  ArtifactKind artifact_used = ArtifactKind::None;
  RetrievalTopic retrieval_topic = RetrievalTopic::RowSelection;
  LintSummary lint_summary;
  ExecutionSummary execution_summary;
};

struct ControllerSnapshot {
  HelperRequest request;
  ControllerState state = ControllerState::Start;
  ArtifactSummary artifact;
  bool has_artifact = false;
  RetrievalPack retrieval_pack;
  bool has_retrieval_pack = false;
  std::string current_query;
  bool has_lint_summary = false;
  LintSummary lint_summary;
  bool has_execution_summary = false;
  ExecutionSummary execution_summary;
  bool has_result_analysis = false;
  ResultAnalysis result_analysis;
  bool has_model_decision = false;
  ModelDecision model_decision;
  int repair_loops = 0;
};

struct ControllerStep {
  ControllerState state = ControllerState::Start;
  StepActionKind action = StepActionKind::Blocked;
  ArtifactKind requested_artifact = ArtifactKind::None;
  RetrievalTopic requested_pack = RetrievalTopic::RowSelection;
  ModelTask model_task = ModelTask::None;
  Diagnosis diagnosis = Diagnosis::None;
  std::string reason;
  std::string query;
  FinalSuggestion final_suggestion;
};

}  // namespace markql::helper
