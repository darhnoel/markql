#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

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
}
