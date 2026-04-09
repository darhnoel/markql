#include "helper_policy.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace markql::helper {

namespace {

std::string ascii_lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

}  // namespace

std::string to_string(HelperMode value) {
  switch (value) {
    case HelperMode::Start:
      return "start";
    case HelperMode::Repair:
      return "repair";
    case HelperMode::Explain:
      return "explain";
  }
  return "start";
}

std::string to_string(ArtifactKind value) {
  switch (value) {
    case ArtifactKind::None:
      return "";
    case ArtifactKind::CompactFamilies:
      return "compact_families";
    case ArtifactKind::Families:
      return "families";
    case ArtifactKind::Skeleton:
      return "skeleton";
    case ArtifactKind::TargetedSubtree:
      return "targeted_subtree";
    case ArtifactKind::FullHtml:
      return "full_html";
  }
  return "";
}

std::string to_string(ControllerState value) {
  switch (value) {
    case ControllerState::Start:
      return "START";
    case ControllerState::InspectCompact:
      return "INSPECT_COMPACT";
    case ControllerState::ChoosePath:
      return "CHOOSE_PATH";
    case ControllerState::RetrievePack:
      return "RETRIEVE_PACK";
    case ControllerState::ModelSuggest:
      return "MODEL_SUGGEST";
    case ControllerState::LintQuery:
      return "LINT_QUERY";
    case ControllerState::ExecuteQuery:
      return "EXECUTE_QUERY";
    case ControllerState::AnalyzeResult:
      return "ANALYZE_RESULT";
    case ControllerState::ModelRepair:
      return "MODEL_REPAIR";
    case ControllerState::EscalateArtifact:
      return "ESCALATE_ARTIFACT";
    case ControllerState::Done:
      return "DONE";
    case ControllerState::Blocked:
      return "BLOCKED";
  }
  return "BLOCKED";
}

std::string to_string(Diagnosis value) {
  switch (value) {
    case Diagnosis::None:
      return "none";
    case Diagnosis::RowScope:
      return "row_scope";
    case Diagnosis::FieldScope:
      return "field_scope";
    case Diagnosis::Grammar:
      return "grammar";
    case Diagnosis::Semantic:
      return "semantic";
    case Diagnosis::ArtifactTooLossy:
      return "artifact_too_lossy";
    case Diagnosis::Mixed:
      return "mixed";
    case Diagnosis::Unknown:
      return "unknown";
  }
  return "unknown";
}

std::string to_string(AnalysisCategory value) {
  switch (value) {
    case AnalysisCategory::GrammarFailure:
      return "grammar_failure";
    case AnalysisCategory::SemanticFailure:
      return "semantic_failure";
    case AnalysisCategory::EmptyOutput:
      return "empty_output";
    case AnalysisCategory::WrongRowCount:
      return "wrong_row_count";
    case AnalysisCategory::RightRowsNullFields:
      return "right_rows_null_fields";
    case AnalysisCategory::MixedInstability:
      return "mixed_instability";
    case AnalysisCategory::LikelySuccess:
      return "likely_success";
    case AnalysisCategory::ArtifactTooLossy:
      return "artifact_too_lossy";
  }
  return "likely_success";
}

std::string to_string(RetrievalTopic value) {
  switch (value) {
    case RetrievalTopic::RowSelection:
      return "row_selection";
    case RetrievalTopic::Exploration:
      return "exploration";
    case RetrievalTopic::Stabilization:
      return "stabilization";
    case RetrievalTopic::StableExtraction:
      return "stable_extraction";
    case RetrievalTopic::Repair:
      return "repair";
    case RetrievalTopic::Grammar:
      return "grammar";
    case RetrievalTopic::NullAndScope:
      return "null_and_scope";
  }
  return "row_selection";
}

std::string to_string(ModelDecisionStatus value) {
  switch (value) {
    case ModelDecisionStatus::None:
      return "none";
    case ModelDecisionStatus::QueryReady:
      return "query_ready";
    case ModelDecisionStatus::NeedMoreArtifact:
      return "need_more_artifact";
    case ModelDecisionStatus::Blocked:
      return "blocked";
  }
  return "none";
}

std::string to_string(ModelTask value) {
  switch (value) {
    case ModelTask::None:
      return "none";
    case ModelTask::InterpretAndSuggest:
      return "interpret_and_suggest";
    case ModelTask::RepairFromSummary:
      return "repair_from_summary";
  }
  return "none";
}

std::string to_string(StepActionKind value) {
  switch (value) {
    case StepActionKind::InspectArtifact:
      return "inspect_artifact";
    case StepActionKind::BuildRetrievalPack:
      return "build_retrieval_pack";
    case StepActionKind::CallModel:
      return "call_model";
    case StepActionKind::LintQuery:
      return "lint_query";
    case StepActionKind::ExecuteQuery:
      return "execute_query";
    case StepActionKind::AnalyzeResult:
      return "analyze_result";
    case StepActionKind::EscalateArtifact:
      return "escalate_artifact";
    case StepActionKind::Done:
      return "done";
    case StepActionKind::Blocked:
      return "blocked";
  }
  return "blocked";
}

ArtifactKind next_artifact_kind(ArtifactKind current) {
  switch (current) {
    case ArtifactKind::None:
      return ArtifactKind::CompactFamilies;
    case ArtifactKind::CompactFamilies:
      return ArtifactKind::Families;
    case ArtifactKind::Families:
      return ArtifactKind::Skeleton;
    case ArtifactKind::Skeleton:
      return ArtifactKind::TargetedSubtree;
    case ArtifactKind::TargetedSubtree:
      return ArtifactKind::FullHtml;
    case ArtifactKind::FullHtml:
      return ArtifactKind::FullHtml;
  }
  return ArtifactKind::FullHtml;
}

bool can_escalate_artifact(ArtifactKind current) {
  return current != ArtifactKind::FullHtml;
}

bool constraint_contains(const HelperRequest& request, std::string_view needle) {
  const std::string lowered_needle = ascii_lower(std::string(needle));
  for (const std::string& constraint : request.constraints) {
    if (ascii_lower(constraint).find(lowered_needle) != std::string::npos) return true;
  }
  return false;
}

RetrievalTopic choose_retrieval_topic(const ControllerSnapshot& snapshot) {
  if (snapshot.has_lint_summary && !snapshot.lint_summary.ok) {
    if (snapshot.lint_summary.category == Diagnosis::Grammar) return RetrievalTopic::Grammar;
    return RetrievalTopic::Repair;
  }
  if (snapshot.has_result_analysis) {
    switch (snapshot.result_analysis.diagnosis) {
      case Diagnosis::Grammar:
        return RetrievalTopic::Grammar;
      case Diagnosis::Semantic:
        return RetrievalTopic::Repair;
      case Diagnosis::RowScope:
        return snapshot.current_query.empty() ? RetrievalTopic::RowSelection
                                              : RetrievalTopic::Stabilization;
      case Diagnosis::FieldScope:
        return constraint_contains(snapshot.request, "project")
                   ? RetrievalTopic::StableExtraction
                   : RetrievalTopic::NullAndScope;
      case Diagnosis::ArtifactTooLossy:
        return RetrievalTopic::Exploration;
      case Diagnosis::Mixed:
        return RetrievalTopic::Repair;
      case Diagnosis::None:
      case Diagnosis::Unknown:
        break;
    }
  }
  if (constraint_contains(snapshot.request, "no project")) return RetrievalTopic::Exploration;
  if (constraint_contains(snapshot.request, "use project")) return RetrievalTopic::StableExtraction;
  if (!snapshot.current_query.empty()) return RetrievalTopic::Repair;
  return RetrievalTopic::RowSelection;
}

RetrievalPack build_retrieval_pack(RetrievalTopic topic) {
  RetrievalPack pack;
  pack.topic = topic;
  switch (topic) {
    case RetrievalTopic::RowSelection:
      pack.summary = "Outer WHERE controls row survival; use EXISTS(...) when supplier existence should decide row inclusion.";
      pack.facts = {
          "MarkQL runs in two stages: outer WHERE keeps rows and field expressions choose values.",
          "Use EXISTS(child|descendant ...) in outer WHERE to gate rows before field extraction.",
      };
      pack.examples = {
          "SELECT section.node_id FROM doc WHERE tag = 'section' AND EXISTS(child WHERE tag = 'h3') ORDER BY node_id;",
      };
      pack.doc_refs = {
          "docs/book/ch02-mental-model.md",
          "docs/book/ch03-first-query-loop.md",
      };
      break;
    case RetrievalTopic::Exploration:
      pack.summary = "Use small exploratory queries first: inspect rows, then test one field.";
      pack.facts = {
          "Start with one family and one row-check query.",
          "FLATTEN/TEXT/ATTR are for inspection; do not jump to a large extraction first.",
      };
      pack.examples = {
          "SELECT a.href, a.tag FROM doc WHERE href IS NOT NULL TO LIST();",
      };
      pack.doc_refs = {
          "docs/book/ch03-first-query-loop.md",
          "tools/html_inspector/docs/ai_inspection_playbook.md",
      };
      break;
    case RetrievalTopic::Stabilization:
      pack.summary = "When the query stops being trivial, stabilize row scope with helper relations before scaling extraction.";
      pack.facts = {
          "Use WITH row-anchor relations for stable row ids.",
          "Keep helper-row CTEs separate from pure shaped PROJECT/FLATTEN CTEs.",
      };
      pack.examples = {
          "WITH r_rows AS (SELECT n.node_id AS row_id FROM doc AS n WHERE n.tag = 'tr') SELECT r_row.row_id FROM r_rows AS r_row;",
      };
      pack.doc_refs = {
          "docs/book/ch03-first-query-loop.md",
          "tools/html_inspector/docs/ai_markql_musts.md",
      };
      break;
    case RetrievalTopic::StableExtraction:
      pack.summary = "Once row scope is proven, use PROJECT, COALESCE, and positional extraction for stable fields.";
      pack.facts = {
          "PROJECT(base_tag) AS (...) evaluates fields per kept row.",
          "TEXT(..., n), FIRST_TEXT(...), and LAST_TEXT(...) are selector-position tools, not row selectors.",
      };
      pack.examples = {
          "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), second_span: TEXT(span, 2), last_span: LAST_TEXT(span)) FROM doc WHERE attributes.data-kind = 'flight' ORDER BY node_id;",
      };
      pack.doc_refs = {
          "docs/book/ch09-project.md",
          "docs/markql-cli-guide.md",
      };
      break;
    case RetrievalTopic::Repair:
      pack.summary = "Repair the smallest failing shape first and feed back only a short lint or execution summary.";
      pack.facts = {
          "Lint first, then execute only if the query shape is valid.",
          "Wrong rows means row-scope repair; null fields on right rows means supplier repair.",
      };
      pack.examples = {
          "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), stop_text: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%')) FROM doc WHERE tag='section' AND EXISTS(descendant WHERE tag='span' AND text LIKE '%stop%') ORDER BY node_id;",
      };
      pack.doc_refs = {
          "docs/book/ch12-troubleshooting.md",
          "tools/html_inspector/docs/ai_markql_musts.md",
      };
      break;
    case RetrievalTopic::Grammar:
      pack.summary = "Use only verified grammar and clause order from the current build.";
      pack.facts = {
          "Do not invent unsupported axes, operators, or top-level extraction forms.",
          "Clause order and PROJECT/FLATTEN shapes are fixed by the current grammar.",
      };
      pack.examples = {
          "SELECT li.node_id, PROJECT(li) AS (title: TEXT(h2)) FROM doc WHERE tag = 'li';",
      };
      pack.doc_refs = {
          "docs/book/appendix-grammar.md",
          "docs/book/appendix-function-reference.md",
      };
      break;
    case RetrievalTopic::NullAndScope:
      pack.summary = "NULL is a field-level signal unless stage 1 dropped the row.";
      pack.facts = {
          "Correct rows with NULL fields usually mean supplier logic is wrong.",
          "Field predicates pick suppliers; they do not remove rows that already survived outer WHERE.",
      };
      pack.examples = {
          "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), stop_text: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%')) FROM doc WHERE tag = 'section' ORDER BY node_id;",
      };
      pack.doc_refs = {
          "docs/book/ch02-mental-model.md",
          "docs/book/ch10-null-and-stability.md",
      };
      break;
  }
  return pack;
}

}  // namespace markql::helper
