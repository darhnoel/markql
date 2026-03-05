#include "xsql/xsql.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../lang/markql_parser.h"
#include "../../util/string_util.h"
#include "xsql_internal.h"

namespace xsql {

namespace {

bool is_node_stream_source(const Source& source) {
  return source.kind == Source::Kind::Document ||
         source.kind == Source::Kind::Path ||
         source.kind == Source::Kind::Url ||
         source.kind == Source::Kind::RawHtml ||
         source.kind == Source::Kind::Fragments ||
         source.kind == Source::Kind::Parse;
}

bool is_bare_identifier_node_projection(const Query::SelectItem& item) {
  return item.aggregate == Query::SelectItem::Aggregate::None &&
         !item.field.has_value() &&
         !item.flatten_text &&
         !item.flatten_extract &&
         !item.self_node_projection &&
         !item.expr_projection &&
         item.tag != "*";
}

void collect_select_alias_ambiguity_warnings(const Query& q,
                                             const std::string& query_text,
                                             std::vector<Diagnostic>& diagnostics) {
  if (q.kind == Query::Kind::Select &&
      q.select_items.size() == 1 &&
      q.source.alias.has_value() &&
      is_node_stream_source(q.source)) {
    const Query::SelectItem& item = q.select_items.front();
    if (is_bare_identifier_node_projection(item) &&
        util::to_lower(item.tag) == util::to_lower(*q.source.alias)) {
      const size_t start = std::min(item.span.start, query_text.size());
      const size_t end = std::min(query_text.size(), start + item.tag.size());
      diagnostics.push_back(
          make_select_alias_ambiguity_warning(query_text, start, end));
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

}  // namespace

std::vector<Diagnostic> lint_query(const std::string& query) {
  std::vector<Diagnostic> diagnostics;
  auto parsed = parse_query(query);
  if (!parsed.query.has_value()) {
    const size_t position = parsed.error.has_value() ? parsed.error->position : 0;
    const std::string message =
        parsed.error.has_value() ? parsed.error->message : "Query parse error";
    diagnostics.push_back(make_syntax_diagnostic(query, message, position));
    return diagnostics;
  }

  const Query& ast = *parsed.query;
  try {
    if (ast.kind == Query::Kind::Select) {
      const bool relation_runtime =
          ast.with.has_value() ||
          !ast.joins.empty() ||
          ast.source.kind == Source::Kind::CteRef ||
          ast.source.kind == Source::Kind::DerivedSubquery;
      if (relation_runtime) {
        if (ast.to_table) {
          throw std::runtime_error("TO TABLE() is not supported with WITH/JOIN queries");
        }
        xsql_internal::validate_limits(ast);
        xsql_internal::validate_predicates(ast);
      } else {
        xsql_internal::validate_projection(ast);
        xsql_internal::validate_order_by(ast);
        xsql_internal::validate_to_table(ast);
        xsql_internal::validate_export_sink(ast);
        xsql_internal::validate_qualifiers(ast);
        xsql_internal::validate_predicates(ast);
        xsql_internal::validate_limits(ast);
      }
    }
  } catch (const std::exception& ex) {
    diagnostics.push_back(make_semantic_diagnostic(query, ex.what()));
    return diagnostics;
  }
  collect_select_alias_ambiguity_warnings(ast, query, diagnostics);
  return diagnostics;
}

}  // namespace xsql
