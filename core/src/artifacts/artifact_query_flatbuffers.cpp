#include "artifact_query_flatbuffers.h"

#include "artifact_internal.h"

#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include "prepared_query_payload_generated.h"

namespace markql::artifacts::detail {

namespace qastfb = markql::artifacts::qastfb;

namespace {

uint8_t to_u8(Query::Kind value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Source::Kind value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Operand::Axis value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Operand::FieldKind value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(ScalarExpr::Kind value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(CompareExpr::Op value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(BinaryExpr::Op value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Query::JoinItem::Type value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Query::ExportSink::Kind value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Query::TableOptions::TrimEmptyCols value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Query::TableOptions::EmptyIs value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Query::TableOptions::Format value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Query::TableOptions::SparseShape value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Query::SelectItem::Aggregate value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Query::SelectItem::TfidfStopwords value) {
  return static_cast<uint8_t>(value);
}
uint8_t to_u8(Query::SelectItem::FlattenExtractExpr::Kind value) {
  return static_cast<uint8_t>(value);
}

std::string read_fb_string(const flatbuffers::String* value,
                           const std::string& field_name = "artifact string") {
  const std::string text = value != nullptr ? value->str() : "";
  validate_utf8_text(text, field_name);
  return text;
}

template <typename T>
size_t bounded_vector_size(const T* vector, size_t max_count, const std::string& message) {
  if (vector == nullptr) return 0;
  ensure(vector->size() <= max_count, message);
  return static_cast<size_t>(vector->size());
}

size_t read_fb_size(uint64_t value, const std::string& message) {
  ensure(value <= static_cast<uint64_t>(std::numeric_limits<size_t>::max()), message);
  return static_cast<size_t>(value);
}

flatbuffers::Offset<qastfb::Operand> build_operand_fb(flatbuffers::FlatBufferBuilder& builder,
                                                      const Operand& operand) {
  return qastfb::CreateOperand(
      builder, static_cast<qastfb::Axis>(to_u8(operand.axis)),
      static_cast<qastfb::OperandFieldKind>(to_u8(operand.field_kind)),
      builder.CreateString(operand.attribute),
      operand.qualifier.has_value() ? builder.CreateString(*operand.qualifier) : 0);
}

flatbuffers::Offset<qastfb::ScalarExpr> build_scalar_expr_fb(
    flatbuffers::FlatBufferBuilder& builder, const ScalarExpr& expr);
flatbuffers::Offset<qastfb::Expr> build_expr_fb(flatbuffers::FlatBufferBuilder& builder,
                                                const Expr& expr);
flatbuffers::Offset<qastfb::Query> build_query_fb(flatbuffers::FlatBufferBuilder& builder,
                                                  const Query& query);

flatbuffers::Offset<qastfb::FlattenExtractExpr> build_flatten_extract_expr_fb(
    flatbuffers::FlatBufferBuilder& builder, const Query::SelectItem::FlattenExtractExpr& expr) {
  std::vector<flatbuffers::Offset<qastfb::FlattenExtractExpr>> args;
  args.reserve(expr.args.size());
  for (const auto& arg : expr.args) args.push_back(build_flatten_extract_expr_fb(builder, arg));

  std::vector<flatbuffers::Offset<qastfb::Expr>> conditions;
  conditions.reserve(expr.case_when_conditions.size());
  for (const auto& condition : expr.case_when_conditions)
    conditions.push_back(build_expr_fb(builder, condition));

  std::vector<flatbuffers::Offset<qastfb::FlattenExtractExpr>> values;
  values.reserve(expr.case_when_values.size());
  for (const auto& value : expr.case_when_values)
    values.push_back(build_flatten_extract_expr_fb(builder, value));

  return qastfb::CreateFlattenExtractExpr(
      builder, static_cast<qastfb::FlattenExtractKind>(to_u8(expr.kind)),
      builder.CreateString(expr.tag),
      expr.attribute.has_value() ? builder.CreateString(*expr.attribute) : 0,
      expr.where.has_value() ? build_expr_fb(builder, *expr.where) : 0,
      expr.selector_index.value_or(0), expr.selector_index.has_value(), expr.selector_last,
      args.empty() ? 0 : builder.CreateVector(args), builder.CreateString(expr.function_name),
      builder.CreateString(expr.string_value), expr.number_value,
      builder.CreateString(expr.alias_ref), build_operand_fb(builder, expr.operand),
      conditions.empty() ? 0 : builder.CreateVector(conditions),
      values.empty() ? 0 : builder.CreateVector(values),
      expr.case_else != nullptr ? build_flatten_extract_expr_fb(builder, *expr.case_else) : 0);
}

flatbuffers::Offset<qastfb::ScalarExpr> build_scalar_expr_fb(
    flatbuffers::FlatBufferBuilder& builder, const ScalarExpr& expr) {
  std::vector<flatbuffers::Offset<qastfb::ScalarExpr>> args;
  args.reserve(expr.args.size());
  for (const auto& arg : expr.args) args.push_back(build_scalar_expr_fb(builder, arg));
  return qastfb::CreateScalarExpr(builder, static_cast<qastfb::ScalarExprKind>(to_u8(expr.kind)),
                                  build_operand_fb(builder, expr.operand),
                                  builder.CreateString(expr.string_value), expr.number_value,
                                  builder.CreateString(expr.function_name),
                                  args.empty() ? 0 : builder.CreateVector(args));
}

flatbuffers::Offset<qastfb::CompareExpr> build_compare_expr_fb(
    flatbuffers::FlatBufferBuilder& builder, const CompareExpr& expr) {
  std::vector<flatbuffers::Offset<flatbuffers::String>> rhs_values;
  rhs_values.reserve(expr.rhs.values.size());
  for (const auto& value : expr.rhs.values) rhs_values.push_back(builder.CreateString(value));

  std::vector<flatbuffers::Offset<qastfb::ScalarExpr>> rhs_expr_list;
  rhs_expr_list.reserve(expr.rhs_expr_list.size());
  for (const auto& rhs : expr.rhs_expr_list)
    rhs_expr_list.push_back(build_scalar_expr_fb(builder, rhs));

  return qastfb::CreateCompareExpr(
      builder, static_cast<qastfb::CompareOp>(to_u8(expr.op)), build_operand_fb(builder, expr.lhs),
      rhs_values.empty() ? 0 : builder.CreateVector(rhs_values),
      expr.lhs_expr.has_value() ? build_scalar_expr_fb(builder, *expr.lhs_expr) : 0,
      expr.rhs_expr.has_value() ? build_scalar_expr_fb(builder, *expr.rhs_expr) : 0,
      rhs_expr_list.empty() ? 0 : builder.CreateVector(rhs_expr_list));
}

flatbuffers::Offset<qastfb::Expr> build_expr_fb(flatbuffers::FlatBufferBuilder& builder,
                                                const Expr& expr) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    return qastfb::CreateExpr(builder, qastfb::ExprVariant_Compare,
                              build_compare_expr_fb(builder, std::get<CompareExpr>(expr)), 0, 0);
  }
  if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
    const auto& exists = *std::get<std::shared_ptr<ExistsExpr>>(expr);
    return qastfb::CreateExpr(
        builder, qastfb::ExprVariant_Exists, 0,
        qastfb::CreateExistsExpr(
            builder, static_cast<qastfb::Axis>(to_u8(exists.axis)),
            exists.where.has_value() ? build_expr_fb(builder, *exists.where) : 0),
        0);
  }
  const auto& binary = *std::get<std::shared_ptr<BinaryExpr>>(expr);
  return qastfb::CreateExpr(
      builder, qastfb::ExprVariant_Binary, 0, 0,
      qastfb::CreateBinaryExpr(builder, static_cast<qastfb::BinaryOp>(to_u8(binary.op)),
                               build_expr_fb(builder, binary.left),
                               build_expr_fb(builder, binary.right)));
}

flatbuffers::Offset<qastfb::Source> build_source_fb(flatbuffers::FlatBufferBuilder& builder,
                                                    const Source& source) {
  return qastfb::CreateSource(
      builder, static_cast<qastfb::SourceKind>(to_u8(source.kind)),
      builder.CreateString(source.value),
      source.alias.has_value() ? builder.CreateString(*source.alias) : 0,
      source.fragments_query != nullptr ? build_query_fb(builder, *source.fragments_query) : 0,
      source.fragments_raw.has_value() ? builder.CreateString(*source.fragments_raw) : 0,
      source.parse_query != nullptr ? build_query_fb(builder, *source.parse_query) : 0,
      source.parse_expr != nullptr ? build_scalar_expr_fb(builder, *source.parse_expr) : 0,
      source.derived_query != nullptr ? build_query_fb(builder, *source.derived_query) : 0);
}

flatbuffers::Offset<qastfb::SelectItem> build_select_item_fb(
    flatbuffers::FlatBufferBuilder& builder, const Query::SelectItem& item) {
  std::vector<flatbuffers::Offset<flatbuffers::String>> tfidf_tags;
  tfidf_tags.reserve(item.tfidf_tags.size());
  for (const auto& tag : item.tfidf_tags) tfidf_tags.push_back(builder.CreateString(tag));

  std::vector<flatbuffers::Offset<flatbuffers::String>> flatten_aliases;
  flatten_aliases.reserve(item.flatten_aliases.size());
  for (const auto& alias : item.flatten_aliases)
    flatten_aliases.push_back(builder.CreateString(alias));

  std::vector<flatbuffers::Offset<flatbuffers::String>> flatten_extract_aliases;
  flatten_extract_aliases.reserve(item.flatten_extract_aliases.size());
  for (const auto& alias : item.flatten_extract_aliases)
    flatten_extract_aliases.push_back(builder.CreateString(alias));

  std::vector<flatbuffers::Offset<qastfb::FlattenExtractExpr>> flatten_exprs;
  flatten_exprs.reserve(item.flatten_extract_exprs.size());
  for (const auto& expr : item.flatten_extract_exprs)
    flatten_exprs.push_back(build_flatten_extract_expr_fb(builder, expr));

  return qastfb::CreateSelectItem(
      builder, static_cast<qastfb::Aggregate>(to_u8(item.aggregate)),
      static_cast<qastfb::TfidfStopwords>(to_u8(item.tfidf_stopwords)),
      builder.CreateString(item.tag), tfidf_tags.empty() ? 0 : builder.CreateVector(tfidf_tags),
      item.field.has_value() ? builder.CreateString(*item.field) : 0, item.tfidf_all_tags,
      item.tfidf_top_terms, item.tfidf_min_df, item.tfidf_max_df, item.inner_html_depth.value_or(0),
      item.inner_html_depth.has_value(), item.inner_html_auto_depth, item.inner_html_function,
      item.raw_inner_html_function, item.text_function, item.trim, item.flatten_text,
      item.flatten_extract, item.self_node_projection, item.expr_projection,
      item.flatten_depth.value_or(0), item.flatten_depth.has_value(),
      flatten_aliases.empty() ? 0 : builder.CreateVector(flatten_aliases),
      flatten_extract_aliases.empty() ? 0 : builder.CreateVector(flatten_extract_aliases),
      flatten_exprs.empty() ? 0 : builder.CreateVector(flatten_exprs),
      item.expr.has_value() ? build_scalar_expr_fb(builder, *item.expr) : 0,
      item.project_expr.has_value() ? build_flatten_extract_expr_fb(builder, *item.project_expr)
                                    : 0);
}

flatbuffers::Offset<qastfb::Query> build_query_fb(flatbuffers::FlatBufferBuilder& builder,
                                                  const Query& query) {
  flatbuffers::Offset<qastfb::WithClause> with_clause = 0;
  if (query.with.has_value()) {
    std::vector<flatbuffers::Offset<qastfb::WithCte>> ctes;
    ctes.reserve(query.with->ctes.size());
    for (const auto& cte : query.with->ctes) {
      ensure(cte.query != nullptr, "Prepared query artifact cannot store null CTE query");
      ctes.push_back(qastfb::CreateWithCte(builder, builder.CreateString(cte.name),
                                           build_query_fb(builder, *cte.query)));
    }
    with_clause = qastfb::CreateWithClause(builder, ctes.empty() ? 0 : builder.CreateVector(ctes));
  }

  std::vector<flatbuffers::Offset<qastfb::SelectItem>> select_items;
  select_items.reserve(query.select_items.size());
  for (const auto& item : query.select_items)
    select_items.push_back(build_select_item_fb(builder, item));

  std::vector<flatbuffers::Offset<qastfb::JoinItem>> joins;
  joins.reserve(query.joins.size());
  for (const auto& join : query.joins) {
    joins.push_back(qastfb::CreateJoinItem(
        builder, static_cast<qastfb::JoinType>(to_u8(join.type)),
        build_source_fb(builder, join.right_source),
        join.on.has_value() ? build_expr_fb(builder, *join.on) : 0, join.lateral));
  }

  std::vector<flatbuffers::Offset<qastfb::OrderBy>> order_by;
  order_by.reserve(query.order_by.size());
  for (const auto& order : query.order_by) {
    order_by.push_back(
        qastfb::CreateOrderBy(builder, builder.CreateString(order.field), order.descending));
  }

  std::vector<flatbuffers::Offset<flatbuffers::String>> exclude_fields;
  exclude_fields.reserve(query.exclude_fields.size());
  for (const auto& field : query.exclude_fields)
    exclude_fields.push_back(builder.CreateString(field));

  flatbuffers::Offset<qastfb::ExportSink> export_sink = 0;
  if (query.export_sink.has_value()) {
    export_sink = qastfb::CreateExportSink(
        builder, static_cast<qastfb::ExportSinkKind>(to_u8(query.export_sink->kind)),
        builder.CreateString(query.export_sink->path));
  }

  const auto table_options = qastfb::CreateTableOptions(
      builder, static_cast<qastfb::TrimEmptyCols>(to_u8(query.table_options.trim_empty_cols)),
      static_cast<qastfb::EmptyIs>(to_u8(query.table_options.empty_is)),
      static_cast<qastfb::TableFormat>(to_u8(query.table_options.format)),
      static_cast<qastfb::SparseShape>(to_u8(query.table_options.sparse_shape)),
      query.table_options.trim_empty_rows, query.table_options.stop_after_empty_rows,
      query.table_options.header_normalize, query.table_options.header_normalize_explicit);

  return qastfb::CreateQuery(
      builder, static_cast<qastfb::QueryKind>(to_u8(query.kind)), with_clause,
      select_items.empty() ? 0 : builder.CreateVector(select_items),
      build_source_fb(builder, query.source), joins.empty() ? 0 : builder.CreateVector(joins),
      query.where.has_value() ? build_expr_fb(builder, *query.where) : 0,
      order_by.empty() ? 0 : builder.CreateVector(order_by),
      exclude_fields.empty() ? 0 : builder.CreateVector(exclude_fields), query.limit.value_or(0),
      query.limit.has_value(), query.to_list, query.to_table, query.table_has_header, table_options,
      export_sink);
}

Operand parse_operand_fb(const qastfb::Operand* operand_fb) {
  ensure(operand_fb != nullptr, "Corrupted artifact: missing operand");
  Operand operand;
  operand.axis = enum_from_u8(static_cast<uint8_t>(operand_fb->axis()), Operand::Axis::Descendant,
                              "Corrupted artifact: invalid operand axis");
  operand.field_kind =
      enum_from_u8(static_cast<uint8_t>(operand_fb->field_kind()), Operand::FieldKind::DocOrder,
                   "Corrupted artifact: invalid operand field kind");
  operand.attribute = read_fb_string(operand_fb->attribute());
  if (operand_fb->qualifier() != nullptr)
    operand.qualifier = read_fb_string(operand_fb->qualifier());
  return operand;
}

ScalarExpr parse_scalar_expr_fb(const qastfb::ScalarExpr* expr_fb, size_t depth = 0);
Expr parse_expr_fb(const qastfb::Expr* expr_fb, size_t depth = 0);
Query parse_query_fb(const qastfb::Query* query_fb, size_t depth = 0);

Query::SelectItem::FlattenExtractExpr parse_flatten_extract_expr_fb(
    const qastfb::FlattenExtractExpr* expr_fb, size_t depth = 0) {
  (void)checked_next_depth(depth, kMaxFlattenExprDepth,
                           "Artifact limit exceeded: flatten expression nesting is too deep");
  ensure(expr_fb != nullptr, "Corrupted artifact: missing flatten extract expression");
  Query::SelectItem::FlattenExtractExpr expr;
  expr.kind = enum_from_u8(static_cast<uint8_t>(expr_fb->kind()),
                           Query::SelectItem::FlattenExtractExpr::Kind::CaseWhen,
                           "Corrupted artifact: invalid flatten extract kind");
  expr.tag = read_fb_string(expr_fb->tag());
  if (expr_fb->attribute() != nullptr) expr.attribute = read_fb_string(expr_fb->attribute());
  if (expr_fb->where_expr() != nullptr) expr.where = parse_expr_fb(expr_fb->where_expr(), depth);
  if (expr_fb->has_selector_index()) expr.selector_index = expr_fb->selector_index();
  expr.selector_last = expr_fb->selector_last();
  if (const auto* args_fb = expr_fb->args()) {
    const size_t count = bounded_vector_size(args_fb, kMaxCollectionCount,
                                             "Artifact limit exceeded: flatten args are too large");
    expr.args.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      expr.args.push_back(parse_flatten_extract_expr_fb(
          args_fb->Get(static_cast<flatbuffers::uoffset_t>(i)),
          checked_next_depth(depth, kMaxFlattenExprDepth,
                             "Artifact limit exceeded: flatten expression nesting is too deep")));
    }
  }
  expr.function_name = read_fb_string(expr_fb->function_name());
  expr.string_value = read_fb_string(expr_fb->string_value());
  expr.number_value = expr_fb->number_value();
  expr.alias_ref = read_fb_string(expr_fb->alias_ref());
  expr.operand = parse_operand_fb(expr_fb->operand());
  if (const auto* conditions_fb = expr_fb->case_when_conditions()) {
    const size_t count =
        bounded_vector_size(conditions_fb, kMaxCollectionCount,
                            "Artifact limit exceeded: flatten CASE conditions are too large");
    expr.case_when_conditions.reserve(count);
    for (size_t i = 0; i < count; ++i)
      expr.case_when_conditions.push_back(parse_expr_fb(conditions_fb->Get(i), depth));
  }
  if (const auto* values_fb = expr_fb->case_when_values()) {
    const size_t count =
        bounded_vector_size(values_fb, kMaxCollectionCount,
                            "Artifact limit exceeded: flatten CASE values are too large");
    expr.case_when_values.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      expr.case_when_values.push_back(parse_flatten_extract_expr_fb(
          values_fb->Get(i),
          checked_next_depth(depth, kMaxFlattenExprDepth,
                             "Artifact limit exceeded: flatten expression nesting is too deep")));
    }
  }
  if (expr_fb->case_else() != nullptr) {
    expr.case_else =
        std::make_shared<Query::SelectItem::FlattenExtractExpr>(parse_flatten_extract_expr_fb(
            expr_fb->case_else(),
            checked_next_depth(depth, kMaxFlattenExprDepth,
                               "Artifact limit exceeded: flatten expression nesting is too deep")));
  }
  return expr;
}

ScalarExpr parse_scalar_expr_fb(const qastfb::ScalarExpr* expr_fb, size_t depth) {
  const size_t next_depth = checked_next_depth(
      depth, kMaxScalarExprDepth, "Artifact limit exceeded: scalar expression nesting is too deep");
  ensure(expr_fb != nullptr, "Corrupted artifact: missing scalar expression");
  ScalarExpr expr;
  expr.kind = enum_from_u8(static_cast<uint8_t>(expr_fb->kind()), ScalarExpr::Kind::FunctionCall,
                           "Corrupted artifact: invalid scalar expression kind");
  expr.operand = parse_operand_fb(expr_fb->operand());
  expr.string_value = read_fb_string(expr_fb->string_value());
  expr.number_value = expr_fb->number_value();
  expr.function_name = read_fb_string(expr_fb->function_name());
  if (const auto* args_fb = expr_fb->args()) {
    const size_t count = bounded_vector_size(args_fb, kMaxCollectionCount,
                                             "Artifact limit exceeded: scalar args are too large");
    expr.args.reserve(count);
    for (size_t i = 0; i < count; ++i)
      expr.args.push_back(parse_scalar_expr_fb(args_fb->Get(i), next_depth));
  }
  return expr;
}

Expr parse_expr_fb(const qastfb::Expr* expr_fb, size_t depth) {
  const size_t next_depth = checked_next_depth(
      depth, kMaxExprDepth, "Artifact limit exceeded: expression nesting is too deep");
  ensure(expr_fb != nullptr, "Corrupted artifact: missing expression");
  switch (expr_fb->variant()) {
    case qastfb::ExprVariant_Compare: {
      ensure(expr_fb->compare() != nullptr, "Corrupted artifact: missing compare expression");
      CompareExpr expr;
      expr.op = enum_from_u8(static_cast<uint8_t>(expr_fb->compare()->op()),
                             CompareExpr::Op::HasDirectText,
                             "Corrupted artifact: invalid compare operator");
      expr.lhs = parse_operand_fb(expr_fb->compare()->lhs());
      if (const auto* values_fb = expr_fb->compare()->rhs_values()) {
        const size_t count = bounded_vector_size(
            values_fb, kMaxCollectionCount, "Artifact limit exceeded: string list is too large");
        expr.rhs.values.reserve(count);
        for (size_t i = 0; i < count; ++i)
          expr.rhs.values.push_back(read_fb_string(values_fb->Get(i)));
      }
      if (expr_fb->compare()->lhs_expr() != nullptr)
        expr.lhs_expr = parse_scalar_expr_fb(expr_fb->compare()->lhs_expr());
      if (expr_fb->compare()->rhs_expr() != nullptr)
        expr.rhs_expr = parse_scalar_expr_fb(expr_fb->compare()->rhs_expr());
      if (const auto* rhs_fb = expr_fb->compare()->rhs_expr_list()) {
        const size_t count = bounded_vector_size(
            rhs_fb, kMaxCollectionCount, "Artifact limit exceeded: expression list is too large");
        expr.rhs_expr_list.reserve(count);
        for (size_t i = 0; i < count; ++i)
          expr.rhs_expr_list.push_back(parse_scalar_expr_fb(rhs_fb->Get(i)));
      }
      return expr;
    }
    case qastfb::ExprVariant_Exists: {
      ensure(expr_fb->exists() != nullptr, "Corrupted artifact: missing exists expression");
      ExistsExpr expr;
      expr.axis =
          enum_from_u8(static_cast<uint8_t>(expr_fb->exists()->axis()), Operand::Axis::Descendant,
                       "Corrupted artifact: invalid exists axis");
      if (expr_fb->exists()->where_expr() != nullptr)
        expr.where = parse_expr_fb(expr_fb->exists()->where_expr(), next_depth);
      return std::make_shared<ExistsExpr>(std::move(expr));
    }
    case qastfb::ExprVariant_Binary: {
      ensure(expr_fb->binary() != nullptr, "Corrupted artifact: missing binary expression");
      BinaryExpr expr;
      expr.op = enum_from_u8(static_cast<uint8_t>(expr_fb->binary()->op()), BinaryExpr::Op::Or,
                             "Corrupted artifact: invalid binary operator");
      expr.left = parse_expr_fb(expr_fb->binary()->left(), next_depth);
      expr.right = parse_expr_fb(expr_fb->binary()->right(), next_depth);
      return std::make_shared<BinaryExpr>(std::move(expr));
    }
    default:
      throw std::runtime_error("Corrupted artifact: invalid expression variant");
  }
}

Source parse_source_fb(const qastfb::Source* source_fb, size_t query_depth) {
  ensure(source_fb != nullptr, "Corrupted artifact: missing source");
  Source source;
  source.kind = enum_from_u8(static_cast<uint8_t>(source_fb->kind()), Source::Kind::DerivedSubquery,
                             "Corrupted artifact: invalid source kind");
  source.value = read_fb_string(source_fb->value());
  if (source_fb->alias() != nullptr) source.alias = read_fb_string(source_fb->alias());
  if (source_fb->fragments_query() != nullptr) {
    source.fragments_query =
        std::make_shared<Query>(parse_query_fb(source_fb->fragments_query(), query_depth));
  }
  if (source_fb->fragments_raw() != nullptr)
    source.fragments_raw = read_fb_string(source_fb->fragments_raw());
  if (source_fb->parse_query() != nullptr) {
    source.parse_query =
        std::make_shared<Query>(parse_query_fb(source_fb->parse_query(), query_depth));
  }
  if (source_fb->parse_expr() != nullptr)
    source.parse_expr = std::make_shared<ScalarExpr>(parse_scalar_expr_fb(source_fb->parse_expr()));
  if (source_fb->derived_query() != nullptr) {
    source.derived_query =
        std::make_shared<Query>(parse_query_fb(source_fb->derived_query(), query_depth));
  }
  return source;
}

Query::SelectItem parse_select_item_fb(const qastfb::SelectItem* item_fb) {
  ensure(item_fb != nullptr, "Corrupted artifact: missing select item");
  Query::SelectItem item;
  item.aggregate =
      enum_from_u8(static_cast<uint8_t>(item_fb->aggregate()), Query::SelectItem::Aggregate::Tfidf,
                   "Corrupted artifact: invalid aggregate kind");
  item.tfidf_stopwords = enum_from_u8(static_cast<uint8_t>(item_fb->tfidf_stopwords()),
                                      Query::SelectItem::TfidfStopwords::None,
                                      "Corrupted artifact: invalid tfidf stopwords value");
  item.tag = read_fb_string(item_fb->tag());
  if (const auto* tags_fb = item_fb->tfidf_tags()) {
    const size_t count = bounded_vector_size(tags_fb, kMaxCollectionCount,
                                             "Artifact limit exceeded: string list is too large");
    item.tfidf_tags.reserve(count);
    for (size_t i = 0; i < count; ++i) item.tfidf_tags.push_back(read_fb_string(tags_fb->Get(i)));
  }
  if (item_fb->field() != nullptr) item.field = read_fb_string(item_fb->field());
  item.tfidf_all_tags = item_fb->tfidf_all_tags();
  item.tfidf_top_terms =
      read_fb_size(item_fb->tfidf_top_terms(), "Corrupted artifact: size value overflow");
  item.tfidf_min_df =
      read_fb_size(item_fb->tfidf_min_df(), "Corrupted artifact: size value overflow");
  item.tfidf_max_df =
      read_fb_size(item_fb->tfidf_max_df(), "Corrupted artifact: size value overflow");
  if (item_fb->has_inner_html_depth()) {
    item.inner_html_depth =
        read_fb_size(item_fb->inner_html_depth(), "Corrupted artifact: size value overflow");
  }
  item.inner_html_auto_depth = item_fb->inner_html_auto_depth();
  item.inner_html_function = item_fb->inner_html_function();
  item.raw_inner_html_function = item_fb->raw_inner_html_function();
  item.text_function = item_fb->text_function();
  item.trim = item_fb->trim();
  item.flatten_text = item_fb->flatten_text();
  item.flatten_extract = item_fb->flatten_extract();
  item.self_node_projection = item_fb->self_node_projection();
  item.expr_projection = item_fb->expr_projection();
  if (item_fb->has_flatten_depth()) {
    item.flatten_depth =
        read_fb_size(item_fb->flatten_depth(), "Corrupted artifact: size value overflow");
  }
  if (const auto* aliases_fb = item_fb->flatten_aliases()) {
    const size_t count = bounded_vector_size(aliases_fb, kMaxCollectionCount,
                                             "Artifact limit exceeded: string list is too large");
    item.flatten_aliases.reserve(count);
    for (size_t i = 0; i < count; ++i)
      item.flatten_aliases.push_back(read_fb_string(aliases_fb->Get(i)));
  }
  if (const auto* aliases_fb = item_fb->flatten_extract_aliases()) {
    const size_t count = bounded_vector_size(aliases_fb, kMaxCollectionCount,
                                             "Artifact limit exceeded: string list is too large");
    item.flatten_extract_aliases.reserve(count);
    for (size_t i = 0; i < count; ++i)
      item.flatten_extract_aliases.push_back(read_fb_string(aliases_fb->Get(i)));
  }
  if (const auto* exprs_fb = item_fb->flatten_extract_exprs()) {
    const size_t count =
        bounded_vector_size(exprs_fb, kMaxCollectionCount,
                            "Artifact limit exceeded: flatten extract expressions are too large");
    item.flatten_extract_exprs.reserve(count);
    for (size_t i = 0; i < count; ++i)
      item.flatten_extract_exprs.push_back(parse_flatten_extract_expr_fb(exprs_fb->Get(i)));
  }
  if (item_fb->expr() != nullptr) item.expr = parse_scalar_expr_fb(item_fb->expr());
  if (item_fb->project_expr() != nullptr)
    item.project_expr = parse_flatten_extract_expr_fb(item_fb->project_expr());
  return item;
}

Query parse_query_fb(const qastfb::Query* query_fb, size_t depth) {
  const size_t next_depth = checked_next_depth(
      depth, kMaxQueryDepth, "Artifact limit exceeded: query nesting is too deep");
  ensure(query_fb != nullptr, "Corrupted artifact: missing query");
  Query query;
  query.kind = enum_from_u8(static_cast<uint8_t>(query_fb->kind()), Query::Kind::DescribeLanguage,
                            "Corrupted artifact: invalid query kind");
  if (query_fb->with_clause() != nullptr) {
    query.with = Query::WithClause{};
    const auto* ctes_fb = query_fb->with_clause()->ctes();
    const size_t cte_count = bounded_vector_size(ctes_fb, kMaxCollectionCount,
                                                 "Artifact limit exceeded: CTE list is too large");
    query.with->ctes.reserve(cte_count);
    for (size_t i = 0; i < cte_count; ++i) {
      const auto* cte_fb = ctes_fb->Get(i);
      ensure(cte_fb != nullptr, "Corrupted artifact: missing CTE entry");
      Query::WithClause::CteDef cte;
      cte.name = read_fb_string(cte_fb->name());
      cte.query = std::make_shared<Query>(parse_query_fb(cte_fb->query(), next_depth));
      query.with->ctes.push_back(std::move(cte));
    }
  }
  if (const auto* select_fb = query_fb->select_items()) {
    const size_t count = bounded_vector_size(select_fb, kMaxCollectionCount,
                                             "Artifact limit exceeded: select list is too large");
    query.select_items.reserve(count);
    for (size_t i = 0; i < count; ++i)
      query.select_items.push_back(parse_select_item_fb(select_fb->Get(i)));
  }
  query.source = parse_source_fb(query_fb->source(), next_depth);
  if (const auto* joins_fb = query_fb->joins()) {
    const size_t count = bounded_vector_size(joins_fb, kMaxCollectionCount,
                                             "Artifact limit exceeded: join list is too large");
    query.joins.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      const auto* join_fb = joins_fb->Get(i);
      ensure(join_fb != nullptr, "Corrupted artifact: missing join");
      Query::JoinItem join;
      join.type = enum_from_u8(static_cast<uint8_t>(join_fb->type()), Query::JoinItem::Type::Cross,
                               "Corrupted artifact: invalid join type");
      join.right_source = parse_source_fb(join_fb->right_source(), next_depth);
      if (join_fb->on_expr() != nullptr) join.on = parse_expr_fb(join_fb->on_expr(), next_depth);
      join.lateral = join_fb->lateral();
      query.joins.push_back(std::move(join));
    }
  }
  if (query_fb->where_expr() != nullptr)
    query.where = parse_expr_fb(query_fb->where_expr(), next_depth);
  if (const auto* order_fb = query_fb->order_by()) {
    const size_t count = bounded_vector_size(order_fb, kMaxCollectionCount,
                                             "Artifact limit exceeded: order-by list is too large");
    query.order_by.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      const auto* order_item = order_fb->Get(i);
      ensure(order_item != nullptr, "Corrupted artifact: missing order-by entry");
      Query::OrderBy order;
      order.field = read_fb_string(order_item->field());
      order.descending = order_item->descending();
      query.order_by.push_back(std::move(order));
    }
  }
  if (const auto* exclude_fb = query_fb->exclude_fields()) {
    const size_t count = bounded_vector_size(exclude_fb, kMaxCollectionCount,
                                             "Artifact limit exceeded: string list is too large");
    query.exclude_fields.reserve(count);
    for (size_t i = 0; i < count; ++i)
      query.exclude_fields.push_back(read_fb_string(exclude_fb->Get(i)));
  }
  if (query_fb->has_limit())
    query.limit = read_fb_size(query_fb->limit(), "Corrupted artifact: size value overflow");
  query.to_list = query_fb->to_list();
  query.to_table = query_fb->to_table();
  query.table_has_header = query_fb->table_has_header();
  ensure(query_fb->table_options() != nullptr, "Corrupted artifact: missing table options");
  query.table_options.trim_empty_cols =
      enum_from_u8(static_cast<uint8_t>(query_fb->table_options()->trim_empty_cols()),
                   Query::TableOptions::TrimEmptyCols::All,
                   "Corrupted artifact: invalid table trim-empty-cols value");
  query.table_options.empty_is = enum_from_u8(
      static_cast<uint8_t>(query_fb->table_options()->empty_is()),
      Query::TableOptions::EmptyIs::BlankOnly, "Corrupted artifact: invalid table empty-is value");
  query.table_options.format = enum_from_u8(
      static_cast<uint8_t>(query_fb->table_options()->format()),
      Query::TableOptions::Format::Sparse, "Corrupted artifact: invalid table format value");
  query.table_options.sparse_shape = enum_from_u8(
      static_cast<uint8_t>(query_fb->table_options()->sparse_shape()),
      Query::TableOptions::SparseShape::Wide, "Corrupted artifact: invalid sparse-shape value");
  query.table_options.trim_empty_rows = query_fb->table_options()->trim_empty_rows();
  query.table_options.stop_after_empty_rows =
      read_fb_size(query_fb->table_options()->stop_after_empty_rows(),
                   "Corrupted artifact: size value overflow");
  query.table_options.header_normalize = query_fb->table_options()->header_normalize();
  query.table_options.header_normalize_explicit =
      query_fb->table_options()->header_normalize_explicit();
  if (query_fb->export_sink() != nullptr) {
    Query::ExportSink sink;
    sink.kind = enum_from_u8(static_cast<uint8_t>(query_fb->export_sink()->kind()),
                             Query::ExportSink::Kind::Ndjson,
                             "Corrupted artifact: invalid export sink kind");
    sink.path = read_fb_string(query_fb->export_sink()->path());
    query.export_sink = sink;
  }
  return query;
}

}  // namespace

std::string build_prepared_query_payload_flatbuffers(const Query& query) {
  flatbuffers::FlatBufferBuilder builder;
  auto query_root = build_query_fb(builder, query);
  qastfb::FinishPreparedQueryPayloadBuffer(builder,
                                           qastfb::CreatePreparedQueryPayload(builder, query_root));
  return std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize());
}

Query parse_prepared_query_payload_flatbuffers(const std::string& payload) {
  flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
  ensure(qastfb::VerifyPreparedQueryPayloadBuffer(verifier),
         "Corrupted artifact: QAST FlatBuffer verification failed");
  ensure(qastfb::PreparedQueryPayloadBufferHasIdentifier(payload.data()),
         "Corrupted artifact: invalid QAST FlatBuffer identifier");
  const qastfb::PreparedQueryPayload* root = qastfb::GetPreparedQueryPayload(payload.data());
  ensure(root != nullptr, "Corrupted artifact: QAST FlatBuffer root missing");
  ensure(root->query() != nullptr, "Corrupted artifact: QAST query missing");
  return parse_query_fb(root->query());
}

}  // namespace markql::artifacts::detail
