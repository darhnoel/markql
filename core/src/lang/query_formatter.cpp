#include "markql/query_formatter.h"

#include <cctype>
#include <sstream>
#include <variant>

#include "markql_parser.h"

namespace markql {
namespace {

std::string upper(std::string value) {
  for (char& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return value;
}

std::string source_text(const Source& source) {
  switch (source.kind) {
    case Source::Kind::Document: return "doc";
    case Source::Kind::Path: return "'" + source.value + "'";
    case Source::Kind::Url: return "'" + source.value + "'";
    default: return {};
  }
}

std::string operand_text(const Operand& operand) {
  std::string prefix;
  switch (operand.axis) {
    case Operand::Axis::Self: break;
    case Operand::Axis::Parent: prefix = "parent."; break;
    case Operand::Axis::Child: prefix = "child."; break;
    case Operand::Axis::Ancestor: prefix = "ancestor."; break;
    case Operand::Axis::Descendant: prefix = "descendant."; break;
  }
  if (operand.qualifier) prefix += *operand.qualifier + ".";
  switch (operand.field_kind) {
    case Operand::FieldKind::Attribute: return prefix + "attributes." + operand.attribute;
    case Operand::FieldKind::AttributesMap: return prefix + "attributes";
    case Operand::FieldKind::Tag: return prefix + "tag";
    case Operand::FieldKind::Text: return prefix + "text";
    case Operand::FieldKind::NodeId: return prefix + "node_id";
    case Operand::FieldKind::ParentId: return prefix + "parent_id";
    case Operand::FieldKind::SiblingPos: return prefix + "sibling_pos";
    case Operand::FieldKind::MaxDepth: return prefix + "max_depth";
    case Operand::FieldKind::DocOrder: return prefix + "doc_order";
  }
  return {};
}

std::string quote(const std::string& value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\\' || c == '\'') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('\'');
  return out;
}

std::optional<std::string> expr_text(const Expr& expr) {
  return std::visit([](const auto& node) -> std::optional<std::string> {
    using T = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<T, CompareExpr>) {
      std::string op;
      switch (node.op) {
        case CompareExpr::Op::Eq: op = " = "; break;
        case CompareExpr::Op::NotEq: op = " <> "; break;
        case CompareExpr::Op::Lt: op = " < "; break;
        case CompareExpr::Op::Lte: op = " <= "; break;
        case CompareExpr::Op::Gt: op = " > "; break;
        case CompareExpr::Op::Gte: op = " >= "; break;
        case CompareExpr::Op::Contains: op = " CONTAINS "; break;
        case CompareExpr::Op::Like: op = " LIKE "; break;
        case CompareExpr::Op::IsNull: return operand_text(node.lhs) + " IS NULL";
        case CompareExpr::Op::IsNotNull: return operand_text(node.lhs) + " IS NOT NULL";
        default: return std::nullopt;
      }
      if (node.rhs.values.size() != 1) return std::nullopt;
      return operand_text(node.lhs) + op + quote(node.rhs.values.front());
    } else if constexpr (std::is_same_v<T, std::shared_ptr<BinaryExpr>>) {
      if (!node) return std::nullopt;
      const auto left = expr_text(node->left);
      const auto right = expr_text(node->right);
      if (!left || !right) return std::nullopt;
      return "(" + *left + (node->op == BinaryExpr::Op::And ? " AND " : " OR ") + *right + ")";
    } else {
      return std::nullopt;
    }
  }, expr);
}

std::optional<std::string> project_expr_text(const Query::SelectItem::FlattenExtractExpr& expr) {
  using Kind = Query::SelectItem::FlattenExtractExpr::Kind;
  if (expr.kind == Kind::Text || expr.kind == Kind::Attr) {
    std::string out = (expr.kind == Kind::Text ? "TEXT(" : "ATTR(") + expr.tag;
    if (expr.kind == Kind::Attr) out += ", " + *expr.attribute;
    if (expr.where) {
      const auto predicate = expr_text(*expr.where);
      if (!predicate) return std::nullopt;
      out += " WHERE " + *predicate;
    }
    if (expr.selector_index) out += ", " + std::to_string(*expr.selector_index);
    else if (expr.selector_last) out += ", LAST";
    return out + ")";
  }
  if (expr.kind == Kind::StringLiteral) return quote(expr.string_value);
  if (expr.kind == Kind::NumberLiteral) return std::to_string(expr.number_value);
  if (expr.kind == Kind::NullLiteral) return "NULL";
  return std::nullopt;
}

}  // namespace

FormatResult format_query_text(const std::string& input) {
  const ParseResult parsed = parse_query(input);
  if (!parsed.query) {
    return {std::nullopt, parsed.error ? parsed.error->message : "query could not be parsed"};
  }
  const Query& query = *parsed.query;
  if (query.kind != Query::Kind::Select || query.with || !query.joins.empty() ||
      query.exclude_fields.size() > 0) {
    return {std::nullopt, "formatter does not yet support this query form"};
  }
  if (query.select_items.empty()) return {std::nullopt, "query has no select items"};
  for (const auto& item : query.select_items) {
    if (item.flatten_text || item.expr_projection || item.project_expr ||
        item.expr || item.aggregate != Query::SelectItem::Aggregate::None) {
      return {std::nullopt, "formatter does not yet support this select expression"};
    }
  }
  const std::string source = source_text(query.source);
  if (source.empty()) return {std::nullopt, "formatter does not support this source"};

  std::ostringstream out;
  out << "SELECT ";
  for (size_t i = 0; i < query.select_items.size(); ++i) {
    if (i) out << ", ";
    const auto& item = query.select_items[i];
    if (item.flatten_extract) {
      out << "PROJECT(" << item.tag << ") AS (";
      const bool multiline = item.flatten_extract_exprs.size() > 1;
      for (size_t j = 0; j < item.flatten_extract_exprs.size(); ++j) {
        if (multiline) out << (j == 0 ? "\n  " : ",\n  ");
        else if (j) out << ", ";
        const auto value = project_expr_text(item.flatten_extract_exprs[j]);
        if (!value) return {std::nullopt, "formatter does not support this PROJECT expression"};
        out << item.flatten_extract_aliases[j] << ": " << *value;
      }
      out << (multiline ? "\n)" : ")");
    } else if (item.self_node_projection) out << "self";
    else if (item.field) out << *item.field;
    else if (!item.tag.empty()) out << item.tag;
    else return {std::nullopt, "formatter does not support this select item"};
  }
  out << "\nFROM " << source;
  if (query.where) {
    const auto predicate = expr_text(*query.where);
    if (!predicate) return {std::nullopt, "formatter does not support this WHERE expression"};
    out << "\nWHERE " << *predicate;
  }
  if (!query.order_by.empty()) {
    out << "\nORDER BY ";
    for (size_t i = 0; i < query.order_by.size(); ++i) {
      if (i) out << ", ";
      out << query.order_by[i].field << (query.order_by[i].descending ? " DESC" : " ASC");
    }
  }
  if (query.limit) out << "\nLIMIT " << *query.limit;
  if (query.to_list) out << "\nTO LIST";
  else if (query.to_table) out << "\nTO TABLE";
  else if (query.export_sink) {
    switch (query.export_sink->kind) {
      case Query::ExportSink::Kind::Json: out << "\nTO JSON()"; break;
      case Query::ExportSink::Kind::Ndjson: out << "\nTO NDJSON()"; break;
      case Query::ExportSink::Kind::Csv: out << "\nTO CSV()"; break;
      case Query::ExportSink::Kind::Parquet: out << "\nTO PARQUET()"; break;
      default: break;
    }
  }
  out << ";\n";
  return {out.str(), {}};
}

}  // namespace markql
