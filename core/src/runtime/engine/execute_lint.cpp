#include "markql/markql.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "../../lang/markql_parser.h"
#include "../../util/string_util.h"
#include "markql_internal.h"

namespace markql {

namespace {

bool is_node_stream_source(const Source& source) {
  return source.kind == Source::Kind::Document || source.kind == Source::Kind::Path ||
         source.kind == Source::Kind::Url || source.kind == Source::Kind::RawHtml ||
         source.kind == Source::Kind::Fragments || source.kind == Source::Kind::Parse;
}

bool is_bare_identifier_node_projection(const Query::SelectItem& item) {
  return item.aggregate == Query::SelectItem::Aggregate::None && !item.field.has_value() &&
         !item.flatten_text && !item.flatten_extract && !item.self_node_projection &&
         !item.expr_projection && item.tag != "*";
}

bool is_relation_runtime_query(const Query& q) {
  return q.kind == Query::Kind::Select &&
         (q.with.has_value() || !q.joins.empty() || q.source.kind == Source::Kind::CteRef ||
          q.source.kind == Source::Kind::DerivedSubquery);
}

std::string operand_member_name(const Operand& operand) {
  switch (operand.field_kind) {
    case Operand::FieldKind::Attribute:
      return operand.attribute;
    case Operand::FieldKind::AttributesMap:
      return "attributes";
    case Operand::FieldKind::Tag:
      return "tag";
    case Operand::FieldKind::Text:
      return "text";
    case Operand::FieldKind::NodeId:
      return "node_id";
    case Operand::FieldKind::ParentId:
      return "parent_id";
    case Operand::FieldKind::SiblingPos:
      return "sibling_pos";
    case Operand::FieldKind::MaxDepth:
      return "max_depth";
    case Operand::FieldKind::DocOrder:
      return "doc_order";
  }
  return "";
}

DiagnosticSpan span_from_bytes_local(const std::string& query, size_t byte_start, size_t byte_end) {
  DiagnosticSpan span;
  const size_t size = query.size();
  span.byte_start = std::min(byte_start, size);
  span.byte_end = std::min(std::max(byte_end, span.byte_start + (size == 0 ? 0u : 1u)), size);
  if (size == 0) {
    span.start_line = 1;
    span.start_col = 1;
    span.end_line = 1;
    span.end_col = 1;
    return span;
  }
  if (span.byte_start >= size) span.byte_start = size - 1;
  if (span.byte_end <= span.byte_start) span.byte_end = std::min(size, span.byte_start + 1);

  size_t line = 1;
  size_t col = 1;
  for (size_t i = 0; i < span.byte_start && i < size; ++i) {
    if (query[i] == '\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
  }
  span.start_line = line;
  span.start_col = col;
  for (size_t i = span.byte_start; i < span.byte_end && i < size; ++i) {
    if (query[i] == '\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
  }
  span.end_line = line;
  span.end_col = col;
  return span;
}

std::string render_code_frame_local(const std::string& query, const DiagnosticSpan& span) {
  if (query.empty()) return "";
  size_t line_start = 0;
  size_t current_line = 1;
  while (current_line < span.start_line && line_start < query.size()) {
    size_t nl = query.find('\n', line_start);
    if (nl == std::string::npos) break;
    line_start = nl + 1;
    ++current_line;
  }
  size_t line_end = query.find('\n', line_start);
  if (line_end == std::string::npos) line_end = query.size();
  std::string line_text = query.substr(line_start, line_end - line_start);
  if (!line_text.empty() && line_text.back() == '\r') line_text.pop_back();

  const size_t caret_start = span.start_col > 0 ? span.start_col - 1 : 0;
  size_t caret_width = 1;
  if (span.start_line == span.end_line && span.end_col > span.start_col) {
    caret_width = span.end_col - span.start_col;
  }
  const size_t line_digits = std::to_string(span.start_line).size();
  std::ostringstream out;
  out << " --> line " << span.start_line << ", col " << span.start_col << "\n";
  out << std::string(line_digits, ' ') << " |\n";
  out << span.start_line << " | " << line_text << "\n";
  out << std::string(line_digits, ' ') << " | " << std::string(caret_start, ' ')
      << std::string(caret_width, '^');
  return out.str();
}

size_t bounded_edit_distance(std::string_view lhs, std::string_view rhs, size_t max_distance) {
  if (lhs == rhs) return 0;
  if (lhs.empty()) return rhs.size();
  if (rhs.empty()) return lhs.size();
  if (lhs.size() > rhs.size() + max_distance || rhs.size() > lhs.size() + max_distance) {
    return max_distance + 1;
  }

  std::vector<size_t> prev(rhs.size() + 1);
  std::vector<size_t> cur(rhs.size() + 1);
  for (size_t j = 0; j <= rhs.size(); ++j) prev[j] = j;
  for (size_t i = 1; i <= lhs.size(); ++i) {
    cur[0] = i;
    size_t row_min = cur[0];
    for (size_t j = 1; j <= rhs.size(); ++j) {
      const size_t cost = lhs[i - 1] == rhs[j - 1] ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
      row_min = std::min(row_min, cur[j]);
    }
    if (row_min > max_distance) return max_distance + 1;
    prev.swap(cur);
  }
  return prev[rhs.size()];
}

std::optional<std::string> suspicious_builtin_match(const std::string& name) {
  static const std::vector<std::string> kBuiltinFields = {"tag",       "text",        "node_id",
                                                          "parent_id", "sibling_pos", "max_depth",
                                                          "doc_order", "attributes",  "attr"};
  const std::string lowered = util::to_lower(name);
  size_t best_distance = 3;
  std::optional<std::string> best_match;
  for (const auto& builtin : kBuiltinFields) {
    if (lowered == builtin) return std::nullopt;
    const size_t distance = bounded_edit_distance(lowered, builtin, 2);
    if (distance < best_distance) {
      best_distance = distance;
      best_match = builtin;
    }
  }
  if (best_distance <= 2) return best_match;
  return std::nullopt;
}

std::vector<std::string> visible_bindings_for_query(const Query& query) {
  std::vector<std::string> bindings;
  auto push_binding = [&](const std::string& name) {
    if (name.empty()) return;
    const std::string lowered = util::to_lower(name);
    if (std::find(bindings.begin(), bindings.end(), lowered) == bindings.end()) {
      bindings.push_back(lowered);
    }
  };

  push_binding("self");
  if (query.source.alias.has_value()) {
    push_binding(*query.source.alias);
    if (query.source.kind == Source::Kind::Document &&
        util::to_lower(*query.source.alias) == "doc") {
      push_binding("document");
    }
  } else if (query.source.kind == Source::Kind::Document) {
    push_binding("doc");
    push_binding("document");
  } else if (query.source.kind == Source::Kind::CteRef) {
    push_binding(query.source.value);
  }

  for (const auto& join : query.joins) {
    if (join.right_source.alias.has_value()) {
      push_binding(*join.right_source.alias);
    } else if (join.right_source.kind == Source::Kind::CteRef) {
      push_binding(join.right_source.value);
    }
  }
  return bindings;
}

void collect_select_alias_ambiguity_warnings(const Query& q, const std::string& query_text,
                                             std::vector<Diagnostic>& diagnostics) {
  if (q.kind == Query::Kind::Select && q.select_items.size() == 1 && q.source.alias.has_value() &&
      is_node_stream_source(q.source)) {
    const Query::SelectItem& item = q.select_items.front();
    if (is_bare_identifier_node_projection(item) &&
        util::to_lower(item.tag) == util::to_lower(*q.source.alias)) {
      const size_t start = std::min(item.span.start, query_text.size());
      const size_t end = std::min(query_text.size(), start + item.tag.size());
      diagnostics.push_back(make_select_alias_ambiguity_warning(query_text, start, end));
    }
  }

  if (q.with.has_value()) {
    for (const auto& cte : q.with->ctes) {
      if (cte.query != nullptr) {
        collect_select_alias_ambiguity_warnings(*cte.query, query_text, diagnostics);
      }
    }
  }

  auto visit_source = [&](const Source& source) -> void {
    if (source.derived_query != nullptr) {
      collect_select_alias_ambiguity_warnings(*source.derived_query, query_text, diagnostics);
    }
    if (source.fragments_query != nullptr) {
      collect_select_alias_ambiguity_warnings(*source.fragments_query, query_text, diagnostics);
    }
    if (source.parse_query != nullptr) {
      collect_select_alias_ambiguity_warnings(*source.parse_query, query_text, diagnostics);
    }
  };

  visit_source(q.source);
  for (const auto& join : q.joins) {
    visit_source(join.right_source);
  }
}

void finalize_warning_snippet(const std::string& query, const Operand& operand,
                              Diagnostic& diagnostic, bool qualifier_only) {
  if (!operand.qualifier.has_value()) return;
  size_t start = operand.span.start;
  size_t end = operand.span.end;
  if (qualifier_only) {
    const std::string& qualifier = *operand.qualifier;
    if (operand.span.start >= qualifier.size() + 1) {
      const size_t candidate = operand.span.start - qualifier.size() - 1;
      if (candidate + qualifier.size() < query.size() &&
          util::to_lower(query.substr(candidate, qualifier.size())) == util::to_lower(qualifier) &&
          query[candidate + qualifier.size()] == '.') {
        start = candidate;
        end = candidate + qualifier.size();
      }
    }
  }
  diagnostic.span = span_from_bytes_local(query, start, end);
  diagnostic.snippet = render_code_frame_local(query, diagnostic.span);
}

void collect_relation_suspicion_warnings(const Query& q, const std::string& query_text,
                                         std::vector<Diagnostic>& diagnostics,
                                         std::unordered_set<std::string>& seen) {
  const bool relation_runtime = is_relation_runtime_query(q);
  const std::vector<std::string> visible_bindings = visible_bindings_for_query(q);

  auto maybe_add = [&](Diagnostic diagnostic, const Operand& operand, bool qualifier_only) {
    finalize_warning_snippet(query_text, operand, diagnostic, qualifier_only);
    const std::string key = diagnostic.code + ":" + std::to_string(diagnostic.span.byte_start) +
                            ":" + std::to_string(diagnostic.span.byte_end);
    if (seen.insert(key).second) diagnostics.push_back(std::move(diagnostic));
  };

  std::function<void(const Operand&)> visit_operand = [&](const Operand& operand) {
    if (operand.qualifier.has_value()) {
      if (operand.field_kind == Operand::FieldKind::Attribute) {
        if (auto suggestion = suspicious_builtin_match(operand.attribute); suggestion.has_value()) {
          Diagnostic d;
          d.severity = DiagnosticSeverity::Warning;
          d.code = "MQL-LINT-0002";
          d.category = "suspicion_warning";
          const std::string qualifier = *operand.qualifier;
          d.message = "Qualified member access looks like a misspelled built-in field";
          d.why =
              "This is not a parse error because MarkQL allows dynamic attribute-style access such "
              "as alias.attr_name. "
              "However, '" +
              qualifier + "." + operand.attribute + "' is very close to the built-in field '" +
              *suggestion + "'.";
          d.help = "If you meant the built-in field, use '" + qualifier + "." + *suggestion + "'.";
          d.example = qualifier + "." + *suggestion;
          d.expected = *suggestion;
          d.encountered = operand.attribute;
          d.doc_ref = "docs/book/appendix-grammar.md";
          maybe_add(std::move(d), operand, false);
        }
      }
      if (relation_runtime) {
        const std::string lowered = util::to_lower(*operand.qualifier);
        const bool visible = std::find(visible_bindings.begin(), visible_bindings.end(), lowered) !=
                             visible_bindings.end();
        if (!visible) {
          Diagnostic d;
          d.severity = DiagnosticSeverity::Warning;
          d.code = "MQL-LINT-0003";
          d.category = "binding_warning";
          d.message = "Qualifier may not be bound in this relation-style query";
          d.why =
              "This is not a parse error because MarkQL accepts qualified member access "
              "syntactically. "
              "This query shape currently uses reduced lint coverage, so alias binding is not "
              "fully proven here.";
          d.encountered = *operand.qualifier;
          d.doc_ref = "docs/book/appendix-grammar.md";
          if (!visible_bindings.empty()) {
            d.help = "Check the FROM/JOIN aliases in scope. Visible bindings here include: ";
            for (size_t i = 0; i < visible_bindings.size(); ++i) {
              if (i != 0) d.help += ", ";
              d.help += visible_bindings[i];
              if (i == 3 && i + 1 < visible_bindings.size()) {
                d.help += ", ...";
                break;
              }
            }
          } else {
            d.help = "Check the FROM/JOIN aliases in scope for this query.";
          }
          maybe_add(std::move(d), operand, true);
        }
      }
    }
  };

  std::function<void(const ScalarExpr&)> visit_scalar = [&](const ScalarExpr& expr) {
    if (expr.kind == ScalarExpr::Kind::Operand) {
      visit_operand(expr.operand);
      return;
    }
    if (expr.kind == ScalarExpr::Kind::FunctionCall) {
      for (const auto& arg : expr.args) visit_scalar(arg);
    }
  };

  std::function<void(const Expr&)> visit_expr = [&](const Expr& expr) {
    if (std::holds_alternative<CompareExpr>(expr)) {
      const auto& cmp = std::get<CompareExpr>(expr);
      if (cmp.lhs_expr.has_value()) {
        visit_scalar(*cmp.lhs_expr);
      } else {
        visit_operand(cmp.lhs);
      }
      if (cmp.rhs_expr.has_value()) visit_scalar(*cmp.rhs_expr);
      for (const auto& rhs_expr : cmp.rhs_expr_list) visit_scalar(rhs_expr);
      return;
    }
    if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
      const auto& exists = *std::get<std::shared_ptr<ExistsExpr>>(expr);
      if (exists.where.has_value()) visit_expr(*exists.where);
      return;
    }
    const auto& bin = *std::get<std::shared_ptr<BinaryExpr>>(expr);
    visit_expr(bin.left);
    visit_expr(bin.right);
  };

  std::function<void(const Query::SelectItem::FlattenExtractExpr&)> visit_extract =
      [&](const Query::SelectItem::FlattenExtractExpr& expr) {
        if (expr.kind == Query::SelectItem::FlattenExtractExpr::Kind::OperandRef) {
          visit_operand(expr.operand);
        }
        if (expr.where.has_value()) visit_expr(*expr.where);
        for (const auto& arg : expr.args) visit_extract(arg);
        for (const auto& when_expr : expr.case_when_conditions) visit_expr(when_expr);
        for (const auto& then_expr : expr.case_when_values) visit_extract(then_expr);
        if (expr.case_else != nullptr) visit_extract(*expr.case_else);
      };

  if (q.where.has_value()) visit_expr(*q.where);
  for (const auto& join : q.joins) {
    if (join.on.has_value()) visit_expr(*join.on);
  }
  for (const auto& item : q.select_items) {
    if (item.expr.has_value()) visit_scalar(*item.expr);
    if (item.project_expr.has_value()) visit_extract(*item.project_expr);
    for (const auto& expr : item.flatten_extract_exprs) visit_extract(expr);
  }

  auto visit_source = [&](const Source& source) -> void {
    if (source.parse_expr != nullptr) visit_scalar(*source.parse_expr);
    if (source.fragments_query != nullptr) {
      collect_relation_suspicion_warnings(*source.fragments_query, query_text, diagnostics, seen);
    }
    if (source.parse_query != nullptr) {
      collect_relation_suspicion_warnings(*source.parse_query, query_text, diagnostics, seen);
    }
    if (source.derived_query != nullptr) {
      collect_relation_suspicion_warnings(*source.derived_query, query_text, diagnostics, seen);
    }
  };

  visit_source(q.source);
  for (const auto& join : q.joins) visit_source(join.right_source);
  if (q.with.has_value()) {
    for (const auto& cte : q.with->ctes) {
      if (cte.query != nullptr) {
        collect_relation_suspicion_warnings(*cte.query, query_text, diagnostics, seen);
      }
    }
  }
}

}  // namespace

LintResult lint_query_detailed(const std::string& query) {
  LintResult result;
  auto parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    const size_t position = parsed.error.has_value() ? parsed.error->position : 0;
    const std::string message =
        parsed.error.has_value() ? parsed.error->message : "Query parse error";
    result.diagnostics.push_back(make_syntax_diagnostic(query, message, position));
    result.summary.parse_succeeded = false;
    result.summary.coverage = LintCoverageLevel::ParseOnly;
    result.summary.error_count = 1;
    return result;
  }

  const Query& ast = *parsed.query;
  result.summary.parse_succeeded = true;
  const bool relation_runtime = is_relation_runtime_query(ast);
  result.summary.relation_style_query = relation_runtime;
  result.summary.used_reduced_validation = relation_runtime;
  result.summary.coverage =
      ast.kind == Query::Kind::Select
          ? (relation_runtime ? LintCoverageLevel::Reduced : LintCoverageLevel::Full)
          : LintCoverageLevel::ParseOnly;
  try {
    if (ast.kind == Query::Kind::Select) {
      if (relation_runtime) {
        if (ast.to_table) {
          throw std::runtime_error("TO TABLE() is not supported with WITH/JOIN queries");
        }
        markql_internal::validate_limits(ast);
        markql_internal::validate_predicates(ast);
      } else {
        markql_internal::validate_projection(ast);
        markql_internal::validate_order_by(ast);
        markql_internal::validate_to_table(ast);
        markql_internal::validate_export_sink(ast);
        markql_internal::validate_qualifiers(ast);
        markql_internal::validate_predicates(ast);
        markql_internal::validate_limits(ast);
      }
    }
  } catch (const std::exception& ex) {
    result.diagnostics.push_back(make_semantic_diagnostic(query, ex.what()));
    result.summary.error_count = 1;
    return result;
  }
  collect_select_alias_ambiguity_warnings(ast, query, result.diagnostics);
  std::unordered_set<std::string> seen_warning_spans;
  collect_relation_suspicion_warnings(ast, query, result.diagnostics, seen_warning_spans);
  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.severity == DiagnosticSeverity::Error) {
      ++result.summary.error_count;
    } else if (diagnostic.severity == DiagnosticSeverity::Warning) {
      ++result.summary.warning_count;
    } else {
      ++result.summary.note_count;
    }
  }
  return result;
}

std::vector<Diagnostic> lint_query(const std::string& query) {
  return lint_query_detailed(query).diagnostics;
}

}  // namespace markql
