#include "artifact_query.h"

#include "artifact_internal.h"
#include "artifact_query_flatbuffers.h"

#include <memory>
#include <vector>

namespace markql::artifacts::detail {

namespace {

uint8_t to_u8(Query::Kind value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Source::Kind value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Operand::Axis value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Operand::FieldKind value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(ScalarExpr::Kind value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(CompareExpr::Op value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(BinaryExpr::Op value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Query::JoinItem::Type value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Query::ExportSink::Kind value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Query::TableOptions::TrimEmptyCols value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Query::TableOptions::EmptyIs value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Query::TableOptions::Format value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Query::TableOptions::SparseShape value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Query::SelectItem::Aggregate value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Query::SelectItem::TfidfStopwords value) { return static_cast<uint8_t>(value); }
uint8_t to_u8(Query::SelectItem::FlattenExtractExpr::Kind value) {
  return static_cast<uint8_t>(value);
}

void write_string_list(BinaryWriter& writer, const std::vector<std::string>& values) {
  ensure(values.size() <= kMaxCollectionCount,
         "Artifact limit exceeded: string list is too large");
  writer.write_u64(static_cast<uint64_t>(values.size()));
  for (const auto& value : values) writer.write_string(value);
}

std::vector<std::string> read_string_list(BinaryReader& reader) {
  const size_t count = read_bounded_count(
      reader, kMaxCollectionCount, "Artifact limit exceeded: string list is too large");
  std::vector<std::string> values;
  values.reserve(count);
  for (size_t i = 0; i < count; ++i) values.push_back(reader.read_string());
  return values;
}

void write_operand(BinaryWriter& writer, const Operand& operand) {
  writer.write_u8(to_u8(operand.axis));
  writer.write_u8(to_u8(operand.field_kind));
  writer.write_string(operand.attribute);
  writer.write_optional_string(operand.qualifier);
}

Operand read_operand(BinaryReader& reader) {
  Operand operand;
  operand.axis = enum_from_u8(reader.read_u8(),
                              Operand::Axis::Descendant,
                              "Corrupted artifact: invalid operand axis");
  operand.field_kind = enum_from_u8(reader.read_u8(),
                                    Operand::FieldKind::DocOrder,
                                    "Corrupted artifact: invalid operand field kind");
  operand.attribute = reader.read_string();
  operand.qualifier = reader.read_optional_string();
  return operand;
}

void write_scalar_expr(BinaryWriter& writer, const ScalarExpr& expr);
ScalarExpr read_scalar_expr(BinaryReader& reader, size_t depth = 0);
void write_expr(BinaryWriter& writer, const Expr& expr);
Expr read_expr(BinaryReader& reader, size_t depth = 0);
void write_query_legacy(BinaryWriter& writer, const Query& query);
Query read_query_legacy(BinaryReader& reader, size_t depth = 0);

void write_flatten_extract_expr(BinaryWriter& writer,
                                const Query::SelectItem::FlattenExtractExpr& expr) {
  writer.write_u8(to_u8(expr.kind));
  writer.write_string(expr.tag);
  writer.write_optional_string(expr.attribute);
  writer.write_bool(expr.where.has_value());
  if (expr.where.has_value()) write_expr(writer, *expr.where);
  writer.write_optional_i64(expr.selector_index);
  writer.write_bool(expr.selector_last);
  writer.write_u64(static_cast<uint64_t>(expr.args.size()));
  for (const auto& arg : expr.args) write_flatten_extract_expr(writer, arg);
  writer.write_string(expr.function_name);
  writer.write_string(expr.string_value);
  writer.write_i64(expr.number_value);
  writer.write_string(expr.alias_ref);
  write_operand(writer, expr.operand);
  writer.write_u64(static_cast<uint64_t>(expr.case_when_conditions.size()));
  for (const auto& condition : expr.case_when_conditions) write_expr(writer, condition);
  writer.write_u64(static_cast<uint64_t>(expr.case_when_values.size()));
  for (const auto& value : expr.case_when_values) write_flatten_extract_expr(writer, value);
  writer.write_bool(expr.case_else != nullptr);
  if (expr.case_else != nullptr) write_flatten_extract_expr(writer, *expr.case_else);
}

Query::SelectItem::FlattenExtractExpr read_flatten_extract_expr(BinaryReader& reader,
                                                                size_t depth = 0) {
  (void)checked_next_depth(depth,
                           kMaxFlattenExprDepth,
                           "Artifact limit exceeded: flatten expression nesting is too deep");
  Query::SelectItem::FlattenExtractExpr expr;
  expr.kind = enum_from_u8(
      reader.read_u8(),
      Query::SelectItem::FlattenExtractExpr::Kind::CaseWhen,
      "Corrupted artifact: invalid flatten extract kind");
  expr.tag = reader.read_string();
  expr.attribute = reader.read_optional_string();
  if (reader.read_bool()) expr.where = read_expr(reader, depth);
  expr.selector_index = reader.read_optional_i64();
  expr.selector_last = reader.read_bool();
  const size_t arg_count = read_bounded_count(
      reader, kMaxCollectionCount, "Artifact limit exceeded: flatten args are too large");
  expr.args.reserve(arg_count);
  for (size_t i = 0; i < arg_count; ++i) {
    expr.args.push_back(read_flatten_extract_expr(
        reader,
        checked_next_depth(depth,
                           kMaxFlattenExprDepth,
                           "Artifact limit exceeded: flatten expression nesting is too deep")));
  }
  expr.function_name = reader.read_string();
  expr.string_value = reader.read_string();
  expr.number_value = reader.read_i64();
  expr.alias_ref = reader.read_string();
  expr.operand = read_operand(reader);
  const size_t condition_count = read_bounded_count(
      reader,
      kMaxCollectionCount,
      "Artifact limit exceeded: flatten CASE conditions are too large");
  expr.case_when_conditions.reserve(condition_count);
  for (size_t i = 0; i < condition_count; ++i) expr.case_when_conditions.push_back(read_expr(reader));
  const size_t value_count = read_bounded_count(
      reader,
      kMaxCollectionCount,
      "Artifact limit exceeded: flatten CASE values are too large");
  expr.case_when_values.reserve(value_count);
  for (size_t i = 0; i < value_count; ++i) {
    expr.case_when_values.push_back(read_flatten_extract_expr(
        reader,
        checked_next_depth(depth,
                           kMaxFlattenExprDepth,
                           "Artifact limit exceeded: flatten expression nesting is too deep")));
  }
  if (reader.read_bool()) {
    expr.case_else = std::make_shared<Query::SelectItem::FlattenExtractExpr>(
        read_flatten_extract_expr(
            reader,
            checked_next_depth(depth,
                               kMaxFlattenExprDepth,
                               "Artifact limit exceeded: flatten expression nesting is too deep")));
  }
  return expr;
}

void write_scalar_expr(BinaryWriter& writer, const ScalarExpr& expr) {
  writer.write_u8(to_u8(expr.kind));
  write_operand(writer, expr.operand);
  writer.write_string(expr.string_value);
  writer.write_i64(expr.number_value);
  writer.write_string(expr.function_name);
  writer.write_u64(static_cast<uint64_t>(expr.args.size()));
  for (const auto& arg : expr.args) write_scalar_expr(writer, arg);
}

ScalarExpr read_scalar_expr(BinaryReader& reader, size_t depth) {
  const size_t next_depth = checked_next_depth(
      depth, kMaxScalarExprDepth, "Artifact limit exceeded: scalar expression nesting is too deep");
  ScalarExpr expr;
  expr.kind = enum_from_u8(reader.read_u8(),
                           ScalarExpr::Kind::FunctionCall,
                           "Corrupted artifact: invalid scalar expression kind");
  expr.operand = read_operand(reader);
  expr.string_value = reader.read_string();
  expr.number_value = reader.read_i64();
  expr.function_name = reader.read_string();
  const size_t arg_count = read_bounded_count(
      reader, kMaxCollectionCount, "Artifact limit exceeded: scalar args are too large");
  expr.args.reserve(arg_count);
  for (size_t i = 0; i < arg_count; ++i) expr.args.push_back(read_scalar_expr(reader, next_depth));
  return expr;
}

void write_compare_expr(BinaryWriter& writer, const CompareExpr& expr) {
  writer.write_u8(to_u8(expr.op));
  write_operand(writer, expr.lhs);
  write_string_list(writer, expr.rhs.values);
  writer.write_bool(expr.lhs_expr.has_value());
  if (expr.lhs_expr.has_value()) write_scalar_expr(writer, *expr.lhs_expr);
  writer.write_bool(expr.rhs_expr.has_value());
  if (expr.rhs_expr.has_value()) write_scalar_expr(writer, *expr.rhs_expr);
  writer.write_u64(static_cast<uint64_t>(expr.rhs_expr_list.size()));
  for (const auto& rhs : expr.rhs_expr_list) write_scalar_expr(writer, rhs);
}

CompareExpr read_compare_expr(BinaryReader& reader) {
  CompareExpr expr;
  expr.op = enum_from_u8(reader.read_u8(),
                         CompareExpr::Op::HasDirectText,
                         "Corrupted artifact: invalid compare operator");
  expr.lhs = read_operand(reader);
  expr.rhs.values = read_string_list(reader);
  if (reader.read_bool()) expr.lhs_expr = read_scalar_expr(reader);
  if (reader.read_bool()) expr.rhs_expr = read_scalar_expr(reader);
  const size_t rhs_expr_count = read_bounded_count(
      reader, kMaxCollectionCount, "Artifact limit exceeded: expression list is too large");
  expr.rhs_expr_list.reserve(rhs_expr_count);
  for (size_t i = 0; i < rhs_expr_count; ++i) expr.rhs_expr_list.push_back(read_scalar_expr(reader));
  return expr;
}

void write_exists_expr(BinaryWriter& writer, const ExistsExpr& expr) {
  writer.write_u8(to_u8(expr.axis));
  writer.write_bool(expr.where.has_value());
  if (expr.where.has_value()) write_expr(writer, *expr.where);
}

ExistsExpr read_exists_expr(BinaryReader& reader, size_t depth) {
  ExistsExpr expr;
  expr.axis = enum_from_u8(reader.read_u8(),
                           Operand::Axis::Descendant,
                           "Corrupted artifact: invalid exists axis");
  if (reader.read_bool()) expr.where = read_expr(reader, depth);
  return expr;
}

void write_binary_expr(BinaryWriter& writer, const BinaryExpr& expr) {
  writer.write_u8(to_u8(expr.op));
  write_expr(writer, expr.left);
  write_expr(writer, expr.right);
}

BinaryExpr read_binary_expr(BinaryReader& reader, size_t depth) {
  BinaryExpr expr;
  expr.op = enum_from_u8(reader.read_u8(),
                         BinaryExpr::Op::Or,
                         "Corrupted artifact: invalid binary operator");
  expr.left = read_expr(reader, depth);
  expr.right = read_expr(reader, depth);
  return expr;
}

void write_expr(BinaryWriter& writer, const Expr& expr) {
  if (std::holds_alternative<CompareExpr>(expr)) {
    writer.write_u8(0);
    write_compare_expr(writer, std::get<CompareExpr>(expr));
    return;
  }
  if (std::holds_alternative<std::shared_ptr<ExistsExpr>>(expr)) {
    writer.write_u8(1);
    write_exists_expr(writer, *std::get<std::shared_ptr<ExistsExpr>>(expr));
    return;
  }
  writer.write_u8(2);
  write_binary_expr(writer, *std::get<std::shared_ptr<BinaryExpr>>(expr));
}

Expr read_expr(BinaryReader& reader, size_t depth) {
  const size_t next_depth = checked_next_depth(
      depth, kMaxExprDepth, "Artifact limit exceeded: expression nesting is too deep");
  switch (reader.read_u8()) {
    case 0:
      return read_compare_expr(reader);
    case 1:
      return std::make_shared<ExistsExpr>(read_exists_expr(reader, next_depth));
    case 2:
      return std::make_shared<BinaryExpr>(read_binary_expr(reader, next_depth));
    default:
      throw std::runtime_error("Corrupted artifact: invalid expression variant");
  }
}

void write_source(BinaryWriter& writer, const Source& source) {
  writer.write_u8(to_u8(source.kind));
  writer.write_string(source.value);
  writer.write_optional_string(source.alias);
  writer.write_bool(source.fragments_query != nullptr);
  if (source.fragments_query != nullptr) write_query_legacy(writer, *source.fragments_query);
  writer.write_optional_string(source.fragments_raw);
  writer.write_bool(source.parse_query != nullptr);
  if (source.parse_query != nullptr) write_query_legacy(writer, *source.parse_query);
  writer.write_bool(source.parse_expr != nullptr);
  if (source.parse_expr != nullptr) write_scalar_expr(writer, *source.parse_expr);
  writer.write_bool(source.derived_query != nullptr);
  if (source.derived_query != nullptr) write_query_legacy(writer, *source.derived_query);
}

Source read_source(BinaryReader& reader, size_t query_depth) {
  Source source;
  source.kind = enum_from_u8(reader.read_u8(),
                             Source::Kind::DerivedSubquery,
                             "Corrupted artifact: invalid source kind");
  source.value = reader.read_string();
  source.alias = reader.read_optional_string();
  if (reader.read_bool()) {
    source.fragments_query = std::make_shared<Query>(read_query_legacy(reader, query_depth));
  }
  source.fragments_raw = reader.read_optional_string();
  if (reader.read_bool()) source.parse_query = std::make_shared<Query>(read_query_legacy(reader, query_depth));
  if (reader.read_bool()) source.parse_expr = std::make_shared<ScalarExpr>(read_scalar_expr(reader));
  if (reader.read_bool()) source.derived_query = std::make_shared<Query>(read_query_legacy(reader, query_depth));
  return source;
}

void write_select_item(BinaryWriter& writer, const Query::SelectItem& item) {
  writer.write_u8(to_u8(item.aggregate));
  writer.write_u8(to_u8(item.tfidf_stopwords));
  writer.write_string(item.tag);
  write_string_list(writer, item.tfidf_tags);
  writer.write_optional_string(item.field);
  writer.write_bool(item.tfidf_all_tags);
  writer.write_u64(static_cast<uint64_t>(item.tfidf_top_terms));
  writer.write_u64(static_cast<uint64_t>(item.tfidf_min_df));
  writer.write_u64(static_cast<uint64_t>(item.tfidf_max_df));
  writer.write_optional_u64(item.inner_html_depth);
  writer.write_bool(item.inner_html_auto_depth);
  writer.write_bool(item.inner_html_function);
  writer.write_bool(item.raw_inner_html_function);
  writer.write_bool(item.text_function);
  writer.write_bool(item.trim);
  writer.write_bool(item.flatten_text);
  writer.write_bool(item.flatten_extract);
  writer.write_bool(item.self_node_projection);
  writer.write_bool(item.expr_projection);
  writer.write_optional_u64(item.flatten_depth);
  write_string_list(writer, item.flatten_aliases);
  write_string_list(writer, item.flatten_extract_aliases);
  writer.write_u64(static_cast<uint64_t>(item.flatten_extract_exprs.size()));
  for (const auto& expr : item.flatten_extract_exprs) write_flatten_extract_expr(writer, expr);
  writer.write_bool(item.expr.has_value());
  if (item.expr.has_value()) write_scalar_expr(writer, *item.expr);
  writer.write_bool(item.project_expr.has_value());
  if (item.project_expr.has_value()) write_flatten_extract_expr(writer, *item.project_expr);
}

Query::SelectItem read_select_item(BinaryReader& reader) {
  Query::SelectItem item;
  item.aggregate = enum_from_u8(reader.read_u8(),
                                Query::SelectItem::Aggregate::Tfidf,
                                "Corrupted artifact: invalid aggregate kind");
  item.tfidf_stopwords = enum_from_u8(reader.read_u8(),
                                      Query::SelectItem::TfidfStopwords::None,
                                      "Corrupted artifact: invalid tfidf stopwords value");
  item.tag = reader.read_string();
  item.tfidf_tags = read_string_list(reader);
  item.field = reader.read_optional_string();
  item.tfidf_all_tags = reader.read_bool();
  item.tfidf_top_terms = static_cast<size_t>(reader.read_u64());
  item.tfidf_min_df = static_cast<size_t>(reader.read_u64());
  item.tfidf_max_df = static_cast<size_t>(reader.read_u64());
  item.inner_html_depth = reader.read_optional_u64();
  item.inner_html_auto_depth = reader.read_bool();
  item.inner_html_function = reader.read_bool();
  item.raw_inner_html_function = reader.read_bool();
  item.text_function = reader.read_bool();
  item.trim = reader.read_bool();
  item.flatten_text = reader.read_bool();
  item.flatten_extract = reader.read_bool();
  item.self_node_projection = reader.read_bool();
  item.expr_projection = reader.read_bool();
  item.flatten_depth = reader.read_optional_u64();
  item.flatten_aliases = read_string_list(reader);
  item.flatten_extract_aliases = read_string_list(reader);
  const size_t flatten_expr_count = read_bounded_count(
      reader,
      kMaxCollectionCount,
      "Artifact limit exceeded: flatten extract expressions are too large");
  item.flatten_extract_exprs.reserve(flatten_expr_count);
  for (size_t i = 0; i < flatten_expr_count; ++i) item.flatten_extract_exprs.push_back(read_flatten_extract_expr(reader));
  if (reader.read_bool()) item.expr = read_scalar_expr(reader);
  if (reader.read_bool()) item.project_expr = read_flatten_extract_expr(reader);
  return item;
}

void write_query_legacy(BinaryWriter& writer, const Query& query) {
  writer.write_u8(to_u8(query.kind));
  writer.write_bool(query.with.has_value());
  if (query.with.has_value()) {
    writer.write_u64(static_cast<uint64_t>(query.with->ctes.size()));
    for (const auto& cte : query.with->ctes) {
      writer.write_string(cte.name);
      ensure(cte.query != nullptr, "Prepared query artifact cannot store null CTE query");
      write_query_legacy(writer, *cte.query);
    }
  }
  writer.write_u64(static_cast<uint64_t>(query.select_items.size()));
  for (const auto& item : query.select_items) write_select_item(writer, item);
  write_source(writer, query.source);
  writer.write_u64(static_cast<uint64_t>(query.joins.size()));
  for (const auto& join : query.joins) {
    writer.write_u8(to_u8(join.type));
    write_source(writer, join.right_source);
    writer.write_bool(join.on.has_value());
    if (join.on.has_value()) write_expr(writer, *join.on);
    writer.write_bool(join.lateral);
  }
  writer.write_bool(query.where.has_value());
  if (query.where.has_value()) write_expr(writer, *query.where);
  writer.write_u64(static_cast<uint64_t>(query.order_by.size()));
  for (const auto& order : query.order_by) {
    writer.write_string(order.field);
    writer.write_bool(order.descending);
  }
  write_string_list(writer, query.exclude_fields);
  writer.write_optional_u64(query.limit);
  writer.write_bool(query.to_list);
  writer.write_bool(query.to_table);
  writer.write_bool(query.table_has_header);
  writer.write_u8(to_u8(query.table_options.trim_empty_cols));
  writer.write_u8(to_u8(query.table_options.empty_is));
  writer.write_u8(to_u8(query.table_options.format));
  writer.write_u8(to_u8(query.table_options.sparse_shape));
  writer.write_bool(query.table_options.trim_empty_rows);
  writer.write_u64(static_cast<uint64_t>(query.table_options.stop_after_empty_rows));
  writer.write_bool(query.table_options.header_normalize);
  writer.write_bool(query.table_options.header_normalize_explicit);
  writer.write_bool(query.export_sink.has_value());
  if (query.export_sink.has_value()) {
    writer.write_u8(to_u8(query.export_sink->kind));
    writer.write_string(query.export_sink->path);
  }
}

Query read_query_legacy(BinaryReader& reader, size_t depth) {
  const size_t next_depth = checked_next_depth(
      depth, kMaxQueryDepth, "Artifact limit exceeded: query nesting is too deep");
  Query query;
  query.kind = enum_from_u8(reader.read_u8(),
                            Query::Kind::DescribeLanguage,
                            "Corrupted artifact: invalid query kind");
  if (reader.read_bool()) {
    query.with = Query::WithClause{};
    const size_t cte_count = read_bounded_count(
        reader, kMaxCollectionCount, "Artifact limit exceeded: CTE list is too large");
    query.with->ctes.reserve(cte_count);
    for (size_t i = 0; i < cte_count; ++i) {
      Query::WithClause::CteDef cte;
      cte.name = reader.read_string();
      cte.query = std::make_shared<Query>(read_query_legacy(reader, next_depth));
      query.with->ctes.push_back(std::move(cte));
    }
  }
  const size_t select_count = read_bounded_count(
      reader, kMaxCollectionCount, "Artifact limit exceeded: select list is too large");
  query.select_items.reserve(select_count);
  for (size_t i = 0; i < select_count; ++i) query.select_items.push_back(read_select_item(reader));
  query.source = read_source(reader, next_depth);
  const size_t join_count = read_bounded_count(
      reader, kMaxCollectionCount, "Artifact limit exceeded: join list is too large");
  query.joins.reserve(join_count);
  for (size_t i = 0; i < join_count; ++i) {
    Query::JoinItem join;
    join.type = enum_from_u8(reader.read_u8(),
                             Query::JoinItem::Type::Cross,
                             "Corrupted artifact: invalid join type");
    join.right_source = read_source(reader, next_depth);
    if (reader.read_bool()) join.on = read_expr(reader, next_depth);
    join.lateral = reader.read_bool();
    query.joins.push_back(std::move(join));
  }
  if (reader.read_bool()) query.where = read_expr(reader, next_depth);
  const size_t order_count = read_bounded_count(
      reader, kMaxCollectionCount, "Artifact limit exceeded: order-by list is too large");
  query.order_by.reserve(order_count);
  for (size_t i = 0; i < order_count; ++i) {
    Query::OrderBy order;
    order.field = reader.read_string();
    order.descending = reader.read_bool();
    query.order_by.push_back(std::move(order));
  }
  query.exclude_fields = read_string_list(reader);
  query.limit = reader.read_optional_u64();
  query.to_list = reader.read_bool();
  query.to_table = reader.read_bool();
  query.table_has_header = reader.read_bool();
  query.table_options.trim_empty_cols = enum_from_u8(
      reader.read_u8(),
      Query::TableOptions::TrimEmptyCols::All,
      "Corrupted artifact: invalid table trim-empty-cols value");
  query.table_options.empty_is = enum_from_u8(
      reader.read_u8(),
      Query::TableOptions::EmptyIs::BlankOnly,
      "Corrupted artifact: invalid table empty-is value");
  query.table_options.format = enum_from_u8(
      reader.read_u8(),
      Query::TableOptions::Format::Sparse,
      "Corrupted artifact: invalid table format value");
  query.table_options.sparse_shape = enum_from_u8(
      reader.read_u8(),
      Query::TableOptions::SparseShape::Wide,
      "Corrupted artifact: invalid sparse-shape value");
  query.table_options.trim_empty_rows = reader.read_bool();
  query.table_options.stop_after_empty_rows = static_cast<size_t>(reader.read_u64());
  query.table_options.header_normalize = reader.read_bool();
  query.table_options.header_normalize_explicit = reader.read_bool();
  if (reader.read_bool()) {
    Query::ExportSink sink;
    sink.kind = enum_from_u8(reader.read_u8(),
                             Query::ExportSink::Kind::Ndjson,
                             "Corrupted artifact: invalid export sink kind");
    sink.path = reader.read_string();
    query.export_sink = sink;
  }
  return query;
}

std::string build_prepared_query_payload_legacy(const Query& query) {
  BinaryWriter writer;
  write_query_legacy(writer, query);
  return writer.data();
}

Query parse_prepared_query_payload_legacy(const std::string& payload) {
  BinaryReader reader(payload);
  Query query = read_query_legacy(reader);
  ensure(reader.done(), "Corrupted artifact: trailing prepared query payload");
  return query;
}

}  // namespace

uint64_t prepared_query_required_features() { return kRequiredFeatureQastFlatbuffers; }

bool prepared_query_uses_flatbuffers(const ArtifactHeader& header) {
  return (header.required_features & kRequiredFeatureQastFlatbuffers) != 0;
}

std::string build_prepared_meta_payload(const Query& query, const std::string& original_query) {
  BinaryWriter writer;
  writer.write_string(markql::get_version_info().version, "producer version");
  writer.write_string(markql::get_version_info().version, "language version");
  writer.write_string(original_query, "original query");
  writer.write_u8(to_u8(query.kind));
  writer.write_u8(to_u8(query.source.kind));
  return writer.data();
}

void parse_prepared_meta(const std::string& payload, ArtifactInfo& info) {
  BinaryReader reader(payload);
  info.producer_version = reader.read_string("producer version");
  info.language_version = reader.read_string("language version");
  info.original_query = reader.read_string("original query");
  info.query_kind = enum_from_u8(reader.read_u8(),
                                 Query::Kind::DescribeLanguage,
                                 "Corrupted artifact: invalid prepared query kind");
  info.source_kind = enum_from_u8(reader.read_u8(),
                                  Source::Kind::DerivedSubquery,
                                  "Corrupted artifact: invalid prepared source kind");
  info.metadata_available = true;
  ensure(reader.done(), "Corrupted artifact: trailing prepared-query metadata");
}

std::string build_prepared_query_payload(const Query& query) {
  return build_prepared_query_payload_flatbuffers(query);
}

Query parse_prepared_query_payload(const ArtifactHeader& header, const std::string& payload) {
  return prepared_query_uses_flatbuffers(header) ? parse_prepared_query_payload_flatbuffers(payload)
                                                 : parse_prepared_query_payload_legacy(payload);
}

}  // namespace markql::artifacts::detail
