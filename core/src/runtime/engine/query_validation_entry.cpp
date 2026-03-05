#include "xsql/xsql.h"

#include <stdexcept>

#include "../../lang/markql_parser.h"
#include "engine_execution_internal.h"
#include "xsql_internal.h"

namespace xsql {

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
    xsql_internal::validate_limits(query);
    xsql_internal::validate_predicates(query);
    return;
  }
  xsql_internal::validate_projection(query);
  xsql_internal::validate_order_by(query);
  xsql_internal::validate_to_table(query);
  xsql_internal::validate_export_sink(query);
  xsql_internal::validate_qualifiers(query);
  xsql_internal::validate_predicates(query);
  xsql_internal::validate_limits(query);
}

}  // namespace xsql
