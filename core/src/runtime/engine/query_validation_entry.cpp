#include "markql/markql.h"

#include <stdexcept>

#include "../../lang/markql_parser.h"
#include "engine_execution_internal.h"
#include "markql_internal.h"

namespace markql {

void validate_query_for_execution(const Query& query) {
  if (query.kind != Query::Kind::Select) {
    return;
  }
  const bool relation_runtime =
      query.with.has_value() ||
      !query.joins.empty() ||
      query.source.kind == Source::Kind::CteRef ||
      query.source.kind == Source::Kind::DerivedSubquery;
  if (relation_runtime) {
    if (query.to_table) {
      throw std::runtime_error("TO TABLE() is not supported with WITH/JOIN queries");
    }
    markql_internal::validate_limits(query);
    markql_internal::validate_predicates(query);
    return;
  }
  markql_internal::validate_projection(query);
  markql_internal::validate_order_by(query);
  markql_internal::validate_to_table(query);
  markql_internal::validate_export_sink(query);
  markql_internal::validate_qualifiers(query);
  markql_internal::validate_predicates(query);
  markql_internal::validate_limits(query);
}

}  // namespace markql
