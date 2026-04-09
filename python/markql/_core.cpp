#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "helper/helper_controller.h"
#include "helper/helper_policy.h"
#include "helper/helper_result_analysis.h"
#include "markql/markql.h"

namespace py = pybind11;

namespace {

py::dict attributes_to_dict(const std::unordered_map<std::string, std::string>& attrs) {
  py::dict out;
  for (const auto& kv : attrs) {
    out[py::str(kv.first)] = py::str(kv.second);
  }
  return out;
}

py::object field_value(const markql::QueryResultRow& row, const std::string& field) {
  if (field == "node_id") return py::int_(row.node_id);
  if (field == "count") return py::int_(row.node_id);
  if (field == "tag") return py::str(row.tag);
  if (field == "text") return py::str(row.text);
  if (field == "inner_html") return py::str(row.inner_html);
  if (field == "max_depth") return py::int_(row.max_depth);
  if (field == "doc_order") return py::int_(row.doc_order);
  if (field == "terms_score") {
    py::dict out;
    for (const auto& kv : row.term_scores) {
      out[py::str(kv.first)] = py::float_(kv.second);
    }
    return out;
  }
  if (field == "parent_id") {
    if (row.parent_id.has_value()) return py::int_(*row.parent_id);
    return py::none();
  }
  if (field == "sibling_pos") return py::int_(row.sibling_pos);
  if (field == "source_uri") return py::str(row.source_uri);
  if (field == "attributes") return attributes_to_dict(row.attributes);
  auto computed = row.computed_fields.find(field);
  if (computed != row.computed_fields.end()) return py::str(computed->second);
  auto it = row.attributes.find(field);
  if (it == row.attributes.end()) return py::none();
  return py::str(it->second);
}

py::dict row_to_dict(const markql::QueryResultRow& row, const std::vector<std::string>& columns) {
  py::dict out;
  for (const auto& col : columns) {
    out[py::str(col)] = field_value(row, col);
  }
  return out;
}

py::dict diagnostic_span_to_dict(const markql::DiagnosticSpan& span) {
  py::dict out;
  out["start_line"] = span.start_line;
  out["start_col"] = span.start_col;
  out["end_line"] = span.end_line;
  out["end_col"] = span.end_col;
  out["byte_start"] = span.byte_start;
  out["byte_end"] = span.byte_end;
  return out;
}

std::string severity_to_string(markql::DiagnosticSeverity severity) {
  switch (severity) {
    case markql::DiagnosticSeverity::Error:
      return "ERROR";
    case markql::DiagnosticSeverity::Warning:
      return "WARNING";
    case markql::DiagnosticSeverity::Note:
      return "NOTE";
  }
  return "ERROR";
}

std::string coverage_to_string(markql::LintCoverageLevel coverage) {
  switch (coverage) {
    case markql::LintCoverageLevel::ParseOnly:
      return "parse_only";
    case markql::LintCoverageLevel::Full:
      return "full";
    case markql::LintCoverageLevel::Reduced:
      return "reduced";
    case markql::LintCoverageLevel::Mixed:
      return "mixed";
  }
  return "parse_only";
}

py::dict diagnostic_to_dict(const markql::Diagnostic& diagnostic) {
  py::dict out;
  out["severity"] = severity_to_string(diagnostic.severity);
  out["code"] = diagnostic.code;
  out["message"] = diagnostic.message;
  out["help"] = diagnostic.help;
  out["doc_ref"] = diagnostic.doc_ref;
  out["span"] = diagnostic_span_to_dict(diagnostic.span);
  out["snippet"] = diagnostic.snippet;
  py::list related;
  for (const auto& item : diagnostic.related) {
    py::dict rel;
    rel["message"] = item.message;
    rel["span"] = diagnostic_span_to_dict(item.span);
    related.append(rel);
  }
  out["related"] = related;
  out["category"] = diagnostic.category;
  out["why"] = diagnostic.why;
  out["example"] = diagnostic.example;
  out["expected"] = diagnostic.expected;
  out["encountered"] = diagnostic.encountered;
  return out;
}

py::dict lint_summary_to_dict(const markql::LintSummary& summary) {
  py::dict out;
  out["parse_succeeded"] = summary.parse_succeeded;
  out["coverage"] = coverage_to_string(summary.coverage);
  out["relation_style_query"] = summary.relation_style_query;
  out["used_reduced_validation"] = summary.used_reduced_validation;
  out["error_count"] = summary.error_count;
  out["warning_count"] = summary.warning_count;
  out["note_count"] = summary.note_count;
  return out;
}

std::vector<std::string> py_string_list(const py::handle& obj) {
  if (obj.is_none()) return {};
  return py::cast<std::vector<std::string>>(obj);
}

markql::helper::HelperMode helper_mode_from_string(const std::string& value) {
  if (value == "repair") return markql::helper::HelperMode::Repair;
  if (value == "explain") return markql::helper::HelperMode::Explain;
  return markql::helper::HelperMode::Start;
}

markql::helper::ArtifactKind artifact_kind_from_string(const std::string& value) {
  if (value == "compact_families") return markql::helper::ArtifactKind::CompactFamilies;
  if (value == "families") return markql::helper::ArtifactKind::Families;
  if (value == "skeleton") return markql::helper::ArtifactKind::Skeleton;
  if (value == "targeted_subtree") return markql::helper::ArtifactKind::TargetedSubtree;
  if (value == "full_html") return markql::helper::ArtifactKind::FullHtml;
  return markql::helper::ArtifactKind::None;
}

markql::helper::ControllerState controller_state_from_string(const std::string& value) {
  using markql::helper::ControllerState;
  if (value == "INSPECT_COMPACT") return ControllerState::InspectCompact;
  if (value == "CHOOSE_PATH") return ControllerState::ChoosePath;
  if (value == "RETRIEVE_PACK") return ControllerState::RetrievePack;
  if (value == "MODEL_SUGGEST") return ControllerState::ModelSuggest;
  if (value == "LINT_QUERY") return ControllerState::LintQuery;
  if (value == "EXECUTE_QUERY") return ControllerState::ExecuteQuery;
  if (value == "ANALYZE_RESULT") return ControllerState::AnalyzeResult;
  if (value == "MODEL_REPAIR") return ControllerState::ModelRepair;
  if (value == "ESCALATE_ARTIFACT") return ControllerState::EscalateArtifact;
  if (value == "DONE") return ControllerState::Done;
  if (value == "BLOCKED") return ControllerState::Blocked;
  return ControllerState::Start;
}

markql::helper::Diagnosis diagnosis_from_string(const std::string& value) {
  using markql::helper::Diagnosis;
  if (value == "row_scope") return Diagnosis::RowScope;
  if (value == "field_scope") return Diagnosis::FieldScope;
  if (value == "grammar") return Diagnosis::Grammar;
  if (value == "semantic") return Diagnosis::Semantic;
  if (value == "artifact_too_lossy") return Diagnosis::ArtifactTooLossy;
  if (value == "mixed") return Diagnosis::Mixed;
  if (value == "unknown") return Diagnosis::Unknown;
  return Diagnosis::None;
}

markql::helper::RetrievalTopic retrieval_topic_from_string(const std::string& value) {
  using markql::helper::RetrievalTopic;
  if (value == "exploration") return RetrievalTopic::Exploration;
  if (value == "stabilization") return RetrievalTopic::Stabilization;
  if (value == "stable_extraction") return RetrievalTopic::StableExtraction;
  if (value == "repair") return RetrievalTopic::Repair;
  if (value == "grammar") return RetrievalTopic::Grammar;
  if (value == "null_and_scope") return RetrievalTopic::NullAndScope;
  return RetrievalTopic::RowSelection;
}

markql::helper::ModelDecisionStatus model_status_from_string(const std::string& value) {
  using markql::helper::ModelDecisionStatus;
  if (value == "query_ready") return ModelDecisionStatus::QueryReady;
  if (value == "need_more_artifact") return ModelDecisionStatus::NeedMoreArtifact;
  if (value == "blocked") return ModelDecisionStatus::Blocked;
  return ModelDecisionStatus::None;
}

markql::helper::HelperRequest helper_request_from_dict(const py::dict& obj) {
  markql::helper::HelperRequest request;
  if (obj.contains("mode")) request.mode = helper_mode_from_string(py::cast<std::string>(obj["mode"]));
  if (obj.contains("input_path")) request.input_path = py::cast<std::string>(obj["input_path"]);
  if (obj.contains("goal_text")) request.goal_text = py::cast<std::string>(obj["goal_text"]);
  if (obj.contains("query")) request.query = py::cast<std::string>(obj["query"]);
  if (obj.contains("constraints")) request.constraints = py_string_list(obj["constraints"]);
  if (obj.contains("expected_fields")) request.expected_fields = py_string_list(obj["expected_fields"]);
  return request;
}

markql::helper::ArtifactSummary artifact_summary_from_dict(const py::dict& obj) {
  markql::helper::ArtifactSummary artifact;
  if (obj.contains("kind")) artifact.kind = artifact_kind_from_string(py::cast<std::string>(obj["kind"]));
  if (obj.contains("content")) artifact.content = py::cast<std::string>(obj["content"]);
  if (obj.contains("selector_or_scope")) artifact.selector_or_scope = py::cast<std::string>(obj["selector_or_scope"]);
  if (obj.contains("family_hint")) artifact.family_hint = py::cast<std::string>(obj["family_hint"]);
  if (obj.contains("lossy")) artifact.lossy = py::cast<bool>(obj["lossy"]);
  if (obj.contains("source")) artifact.source = py::cast<std::string>(obj["source"]);
  return artifact;
}

markql::helper::LintSummary helper_lint_summary_from_dict(const py::dict& obj) {
  markql::helper::LintSummary summary;
  if (obj.contains("ok")) summary.ok = py::cast<bool>(obj["ok"]);
  if (obj.contains("category")) summary.category = diagnosis_from_string(py::cast<std::string>(obj["category"]));
  if (obj.contains("error_count")) summary.error_count = py::cast<int>(obj["error_count"]);
  if (obj.contains("warning_count")) summary.warning_count = py::cast<int>(obj["warning_count"]);
  if (obj.contains("headline")) summary.headline = py::cast<std::string>(obj["headline"]);
  if (obj.contains("details")) summary.details = py::cast<std::string>(obj["details"]);
  return summary;
}

markql::helper::ExecutionSummary execution_summary_from_dict(const py::dict& obj) {
  markql::helper::ExecutionSummary summary;
  if (obj.contains("ok")) summary.ok = py::cast<bool>(obj["ok"]);
  if (obj.contains("row_count")) summary.row_count = py::cast<std::size_t>(obj["row_count"]);
  if (obj.contains("null_field_count")) summary.null_field_count = py::cast<std::size_t>(obj["null_field_count"]);
  if (obj.contains("blank_field_count")) summary.blank_field_count = py::cast<std::size_t>(obj["blank_field_count"]);
  if (obj.contains("has_rows")) summary.has_rows = py::cast<bool>(obj["has_rows"]);
  if (obj.contains("expected_row_count") && !obj["expected_row_count"].is_none()) {
    summary.expected_row_count = py::cast<std::size_t>(obj["expected_row_count"]);
  }
  if (obj.contains("headline")) summary.headline = py::cast<std::string>(obj["headline"]);
  if (obj.contains("details")) summary.details = py::cast<std::string>(obj["details"]);
  if (obj.contains("sample_rows")) {
    summary.sample_rows =
        py::cast<std::vector<std::unordered_map<std::string, std::string>>>(obj["sample_rows"]);
  }
  return summary;
}

markql::helper::ResultAnalysis result_analysis_from_dict(const py::dict& obj) {
  markql::helper::ResultAnalysis analysis;
  if (obj.contains("diagnosis")) analysis.diagnosis = diagnosis_from_string(py::cast<std::string>(obj["diagnosis"]));
  if (obj.contains("reason")) analysis.reason = py::cast<std::string>(obj["reason"]);
  if (obj.contains("should_repair")) analysis.should_repair = py::cast<bool>(obj["should_repair"]);
  if (obj.contains("should_escalate")) analysis.should_escalate = py::cast<bool>(obj["should_escalate"]);
  if (obj.contains("should_execute")) analysis.should_execute = py::cast<bool>(obj["should_execute"]);
  if (obj.contains("done")) analysis.done = py::cast<bool>(obj["done"]);
  if (obj.contains("category")) {
    const std::string category = py::cast<std::string>(obj["category"]);
    using markql::helper::AnalysisCategory;
    if (category == "grammar_failure") analysis.category = AnalysisCategory::GrammarFailure;
    else if (category == "semantic_failure") analysis.category = AnalysisCategory::SemanticFailure;
    else if (category == "empty_output") analysis.category = AnalysisCategory::EmptyOutput;
    else if (category == "wrong_row_count") analysis.category = AnalysisCategory::WrongRowCount;
    else if (category == "right_rows_null_fields") analysis.category = AnalysisCategory::RightRowsNullFields;
    else if (category == "mixed_instability") analysis.category = AnalysisCategory::MixedInstability;
    else if (category == "artifact_too_lossy") analysis.category = AnalysisCategory::ArtifactTooLossy;
    else analysis.category = AnalysisCategory::LikelySuccess;
  }
  return analysis;
}

markql::helper::ModelDecision model_decision_from_dict(const py::dict& obj) {
  markql::helper::ModelDecision decision;
  if (obj.contains("status")) decision.status = model_status_from_string(py::cast<std::string>(obj["status"]));
  if (obj.contains("diagnosis")) decision.diagnosis = diagnosis_from_string(py::cast<std::string>(obj["diagnosis"]));
  if (obj.contains("reason")) decision.reason = py::cast<std::string>(obj["reason"]);
  if (obj.contains("chosen_family")) decision.chosen_family = py::cast<std::string>(obj["chosen_family"]);
  if (obj.contains("requested_artifact")) {
    decision.requested_artifact = artifact_kind_from_string(py::cast<std::string>(obj["requested_artifact"]));
  }
  if (obj.contains("query")) decision.query = py::cast<std::string>(obj["query"]);
  if (obj.contains("next_action")) decision.next_action = py::cast<std::string>(obj["next_action"]);
  return decision;
}

markql::helper::ControllerSnapshot controller_snapshot_from_dict(const py::dict& obj) {
  markql::helper::ControllerSnapshot snapshot;
  if (obj.contains("request")) snapshot.request = helper_request_from_dict(py::cast<py::dict>(obj["request"]));
  if (obj.contains("state")) snapshot.state = controller_state_from_string(py::cast<std::string>(obj["state"]));
  if (obj.contains("artifact") && !obj["artifact"].is_none()) {
    snapshot.artifact = artifact_summary_from_dict(py::cast<py::dict>(obj["artifact"]));
    snapshot.has_artifact = true;
  }
  if (obj.contains("retrieval_pack") && !obj["retrieval_pack"].is_none()) {
    const py::dict pack = py::cast<py::dict>(obj["retrieval_pack"]);
    if (pack.contains("topic")) {
      snapshot.retrieval_pack.topic = retrieval_topic_from_string(py::cast<std::string>(pack["topic"]));
    }
    if (pack.contains("summary")) snapshot.retrieval_pack.summary = py::cast<std::string>(pack["summary"]);
    if (pack.contains("facts")) snapshot.retrieval_pack.facts = py_string_list(pack["facts"]);
    if (pack.contains("examples")) snapshot.retrieval_pack.examples = py_string_list(pack["examples"]);
    if (pack.contains("doc_refs")) snapshot.retrieval_pack.doc_refs = py_string_list(pack["doc_refs"]);
    snapshot.has_retrieval_pack = true;
  }
  if (obj.contains("current_query")) snapshot.current_query = py::cast<std::string>(obj["current_query"]);
  if (obj.contains("lint_summary") && !obj["lint_summary"].is_none()) {
    snapshot.lint_summary = helper_lint_summary_from_dict(py::cast<py::dict>(obj["lint_summary"]));
    snapshot.has_lint_summary = true;
  }
  if (obj.contains("execution_summary") && !obj["execution_summary"].is_none()) {
    snapshot.execution_summary = execution_summary_from_dict(py::cast<py::dict>(obj["execution_summary"]));
    snapshot.has_execution_summary = true;
  }
  if (obj.contains("result_analysis") && !obj["result_analysis"].is_none()) {
    snapshot.result_analysis = result_analysis_from_dict(py::cast<py::dict>(obj["result_analysis"]));
    snapshot.has_result_analysis = true;
  }
  if (obj.contains("model_decision") && !obj["model_decision"].is_none()) {
    snapshot.model_decision = model_decision_from_dict(py::cast<py::dict>(obj["model_decision"]));
    snapshot.has_model_decision = true;
  }
  if (obj.contains("repair_loops")) snapshot.repair_loops = py::cast<int>(obj["repair_loops"]);
  return snapshot;
}

py::dict retrieval_pack_to_dict(const markql::helper::RetrievalPack& pack) {
  py::dict out;
  out["topic"] = markql::helper::to_string(pack.topic);
  out["summary"] = pack.summary;
  out["facts"] = pack.facts;
  out["examples"] = pack.examples;
  out["doc_refs"] = pack.doc_refs;
  return out;
}

py::dict result_analysis_to_dict(const markql::helper::ResultAnalysis& analysis) {
  py::dict out;
  out["category"] = markql::helper::to_string(analysis.category);
  out["diagnosis"] = markql::helper::to_string(analysis.diagnosis);
  out["reason"] = analysis.reason;
  out["should_repair"] = analysis.should_repair;
  out["should_escalate"] = analysis.should_escalate;
  out["should_execute"] = analysis.should_execute;
  out["done"] = analysis.done;
  return out;
}

py::dict lint_summary_helper_to_dict(const markql::helper::LintSummary& summary) {
  py::dict out;
  out["ok"] = summary.ok;
  out["category"] = markql::helper::to_string(summary.category);
  out["error_count"] = summary.error_count;
  out["warning_count"] = summary.warning_count;
  out["headline"] = summary.headline;
  out["details"] = summary.details;
  return out;
}

py::dict execution_summary_to_dict(const markql::helper::ExecutionSummary& summary) {
  py::dict out;
  out["ok"] = summary.ok;
  out["row_count"] = summary.row_count;
  out["null_field_count"] = summary.null_field_count;
  out["blank_field_count"] = summary.blank_field_count;
  out["has_rows"] = summary.has_rows;
  out["expected_row_count"] =
      summary.expected_row_count.has_value() ? py::object(py::int_(*summary.expected_row_count))
                                             : py::none();
  out["headline"] = summary.headline;
  out["details"] = summary.details;
  out["sample_rows"] = summary.sample_rows;
  return out;
}

py::dict final_suggestion_to_dict(const markql::helper::FinalSuggestion& suggestion) {
  py::dict out;
  out["status"] = suggestion.status;
  out["mode"] = markql::helper::to_string(suggestion.mode);
  out["query"] = suggestion.query;
  out["diagnosis"] = markql::helper::to_string(suggestion.diagnosis);
  out["reason"] = suggestion.reason;
  out["artifact_used"] = markql::helper::to_string(suggestion.artifact_used);
  out["retrieval_topic"] = markql::helper::to_string(suggestion.retrieval_topic);
  out["lint_summary"] = lint_summary_helper_to_dict(suggestion.lint_summary);
  out["execution_summary"] = execution_summary_to_dict(suggestion.execution_summary);
  return out;
}

py::dict controller_step_to_dict(const markql::helper::ControllerStep& step) {
  py::dict out;
  out["state"] = markql::helper::to_string(step.state);
  out["action"] = markql::helper::to_string(step.action);
  out["requested_artifact"] = markql::helper::to_string(step.requested_artifact);
  out["requested_pack"] = markql::helper::to_string(step.requested_pack);
  out["model_task"] = markql::helper::to_string(step.model_task);
  out["diagnosis"] = markql::helper::to_string(step.diagnosis);
  out["reason"] = step.reason;
  out["query"] = step.query;
  out["final_suggestion"] = final_suggestion_to_dict(step.final_suggestion);
  return out;
}

}  // namespace

PYBIND11_MODULE(_core, m) {
  m.doc() = "Native bindings for MARKQL query execution.";

  m.def(
      "execute_from_document",
      [](const std::string& html, const std::string& query) {
        markql::QueryResult result = markql::execute_query_from_document(html, query);
        py::dict out;
        out["columns"] = result.columns;
        out["warnings"] = result.warnings;
        py::list rows;
        for (const auto& row : result.rows) {
          rows.append(row_to_dict(row, result.columns));
        }
        out["rows"] = rows;
        py::list tables;
        for (const auto& table : result.tables) {
          py::dict table_obj;
          table_obj["node_id"] = table.node_id;
          table_obj["rows"] = table.rows;
          tables.append(table_obj);
        }
        out["tables"] = tables;
        out["to_list"] = result.to_list;
        out["to_table"] = result.to_table;
        out["table_has_header"] = result.table_has_header;
        py::dict export_sink;
        switch (result.export_sink.kind) {
          case markql::QueryResult::ExportSink::Kind::Csv:
            export_sink["kind"] = "csv";
            break;
          case markql::QueryResult::ExportSink::Kind::Parquet:
            export_sink["kind"] = "parquet";
            break;
          default:
            export_sink["kind"] = "none";
            break;
        }
        export_sink["path"] = result.export_sink.path;
        out["export_sink"] = export_sink;
        return out;
      },
      py::arg("html"), py::arg("query"));

  m.def(
      "lint_query",
      [](const std::string& query) {
        std::vector<markql::Diagnostic> diagnostics = markql::lint_query(query);
        py::list out;
        for (const auto& diagnostic : diagnostics) {
          out.append(diagnostic_to_dict(diagnostic));
        }
        return out;
      },
      py::arg("query"));

  m.def(
      "lint_query_detailed",
      [](const std::string& query) {
        markql::LintResult result = markql::lint_query_detailed(query);
        py::dict out;
        out["summary"] = lint_summary_to_dict(result.summary);
        py::list diagnostics;
        for (const auto& diagnostic : result.diagnostics) {
          diagnostics.append(diagnostic_to_dict(diagnostic));
        }
        out["diagnostics"] = diagnostics;
        return out;
      },
      py::arg("query"));

  m.def("core_version", []() { return markql::version_string(); });

  m.def("core_version_info", []() {
    markql::VersionInfo info = markql::get_version_info();
    py::dict out;
    out["version"] = info.version;
    out["git_commit"] = info.git_commit;
    out["git_dirty"] = info.git_dirty;
    out["provenance"] = markql::version_string();
    return out;
  });

  m.def(
      "helper_retrieval_pack",
      [](const std::string& topic) {
        return retrieval_pack_to_dict(markql::helper::build_retrieval_pack(
            retrieval_topic_from_string(topic)));
      },
      py::arg("topic"));

  m.def(
      "helper_analyze_result",
      [](const py::dict& request_obj, const py::dict& artifact_obj, py::object lint_obj,
         py::object execution_obj) {
        markql::helper::HelperRequest request = helper_request_from_dict(request_obj);
        markql::helper::ArtifactSummary artifact = artifact_summary_from_dict(artifact_obj);
        std::optional<markql::helper::LintSummary> lint = std::nullopt;
        std::optional<markql::helper::ExecutionSummary> execution = std::nullopt;
        if (!lint_obj.is_none()) lint = helper_lint_summary_from_dict(py::cast<py::dict>(lint_obj));
        if (!execution_obj.is_none()) {
          execution = execution_summary_from_dict(py::cast<py::dict>(execution_obj));
        }
        return result_analysis_to_dict(markql::helper::analyze_result(
            request, artifact, lint ? &*lint : nullptr, execution ? &*execution : nullptr));
      },
      py::arg("request"), py::arg("artifact"), py::arg("lint_summary") = py::none(),
      py::arg("execution_summary") = py::none());

  m.def(
      "helper_plan_next",
      [](const py::dict& snapshot_obj) {
        return controller_step_to_dict(
            markql::helper::plan_next_step(controller_snapshot_from_dict(snapshot_obj)));
      },
      py::arg("snapshot"));
}
