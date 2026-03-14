#pragma once

#include "markql/markql.h"

#include <cstdint>
#include <string>
#include <vector>

#include "../../lang/markql_parser.h"
#include "../../dom/html_parser.h"

namespace markql {

struct DescendantTagFilter {
  struct Predicate {
    Operand::FieldKind field_kind = Operand::FieldKind::Tag;
    std::string attribute;
    CompareExpr::Op op = CompareExpr::Op::Eq;
    std::vector<std::string> values;
  };
  std::vector<Predicate> predicates;
};

void collect_descendants_at_depth(const std::vector<std::vector<int64_t>>& children,
                                  int64_t node_id,
                                  size_t depth,
                                  std::vector<int64_t>& out);
void collect_descendants_any_depth(const std::vector<std::vector<int64_t>>& children,
                                   int64_t node_id,
                                   std::vector<int64_t>& out);
bool collect_descendant_tag_filter(const Expr& expr, DescendantTagFilter& filter);
bool match_descendant_predicate(const HtmlNode& node, const DescendantTagFilter::Predicate& pred);

}  // namespace markql
