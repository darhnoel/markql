#include "parser_internal.h"

#include <limits>

namespace markql {

bool Parser::parse_flatten_extract_expr(Query::SelectItem::FlattenExtractExpr& expr) {
  if (current_.type == TokenType::String) {
    expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::StringLiteral;
    expr.string_value = current_.text;
    expr.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (current_.type == TokenType::Number) {
    expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::NumberLiteral;
    try {
      expr.number_value = std::stoll(current_.text);
    } catch (...) {
      return set_error("Invalid numeric literal");
    }
    expr.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (current_.type == TokenType::KeywordNull) {
    expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::NullLiteral;
    expr.span = Span{current_.pos, current_.pos + 4};
    advance();
    return true;
  }
  if (current_.type == TokenType::KeywordCase) {
    size_t start = current_.pos;
    advance();
    expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::CaseWhen;
    while (current_.type == TokenType::KeywordWhen) {
      advance();
      Expr when_expr;
      if (!parse_expr(when_expr)) return false;
      if (!consume(TokenType::KeywordThen, "Expected THEN in CASE expression")) return false;
      Query::SelectItem::FlattenExtractExpr then_expr;
      if (!parse_flatten_extract_expr(then_expr)) return false;
      expr.case_when_conditions.push_back(std::move(when_expr));
      expr.case_when_values.push_back(std::move(then_expr));
    }
    if (expr.case_when_values.empty()) {
      return set_error("CASE expression requires at least one WHEN ... THEN pair");
    }
    if (current_.type == TokenType::KeywordElse) {
      advance();
      auto else_expr = std::make_shared<Query::SelectItem::FlattenExtractExpr>();
      if (!parse_flatten_extract_expr(*else_expr)) return false;
      expr.case_else = std::move(else_expr);
    }
    if (!consume(TokenType::KeywordEnd, "Expected END to close CASE expression")) return false;
    expr.span = Span{start, current_.pos};
    return true;
  }
  if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable &&
      current_.type != TokenType::KeywordSelf) {
    return set_error("Expected expression inside PROJECT/FLATTEN_EXTRACT");
  }
  auto parse_selector_position = [&](Query::SelectItem::FlattenExtractExpr& target,
                                     const char* func_name) -> bool {
    if (current_.type != TokenType::Comma) return true;
    advance();
    if (current_.type == TokenType::Number) {
      try {
        int64_t index = std::stoll(current_.text);
        if (index < 1) return set_error(std::string(func_name) + " index must be >= 1");
        target.selector_index = index;
      } catch (...) {
        return set_error(std::string("Invalid ") + func_name + " index");
      }
      advance();
      return true;
    }
    if (current_.type == TokenType::Identifier) {
      std::string pos = to_upper(current_.text);
      if (pos == "FIRST") {
        target.selector_index = 1;
        advance();
        return true;
      }
      if (pos == "LAST") {
        target.selector_last = true;
        advance();
        return true;
      }
    }
    return set_error(std::string("Expected numeric index, FIRST, or LAST in ") + func_name);
  };

  size_t start = current_.pos;
  std::string ident = current_.text;
  std::string fn = to_upper(current_.text);
  if (peek().type != TokenType::LParen) {
    bool operand_like = peek().type == TokenType::Dot || fn == "ATTRIBUTES" || fn == "TAG" ||
                        fn == "TEXT" || fn == "NODE_ID" || fn == "PARENT_ID" ||
                        fn == "SIBLING_POS" || fn == "MAX_DEPTH" || fn == "DOC_ORDER" ||
                        fn == "SELF" || fn == "PARENT" || fn == "CHILD" || fn == "ANCESTOR" ||
                        fn == "DESCENDANT";
    if (operand_like) {
      Operand operand;
      if (!parse_operand(operand)) return false;
      expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::OperandRef;
      expr.operand = std::move(operand);
      expr.span = expr.operand.span;
      return true;
    }
    expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::AliasRef;
    expr.alias_ref = ident;
    expr.span = Span{start, current_.pos + current_.text.size()};
    advance();
    return true;
  }

  if (fn == "TEXT" || fn == "DIRECT_TEXT" || fn == "FIRST_TEXT" || fn == "LAST_TEXT") {
    advance();
    if (!consume(TokenType::LParen, "Expected ( after TEXT function")) return false;
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected tag identifier inside TEXT()");
    }
    std::string tag = to_lower(current_.text);
    advance();
    if (fn == "DIRECT_TEXT") {
      expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::FunctionCall;
      expr.function_name = "DIRECT_TEXT";
      Query::SelectItem::FlattenExtractExpr tag_expr;
      tag_expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::StringLiteral;
      tag_expr.string_value = tag;
      expr.args.push_back(std::move(tag_expr));
    } else {
      expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::Text;
      expr.tag = tag;
    }
    if (current_.type == TokenType::KeywordWhere) {
      advance();
      Expr where_expr;
      if (!parse_expr(where_expr)) return false;
      expr.where = std::move(where_expr);
    }
    if (!parse_selector_position(expr, "TEXT()")) return false;
    if (fn == "FIRST_TEXT") {
      expr.selector_index = 1;
      expr.selector_last = false;
    } else if (fn == "LAST_TEXT") {
      expr.selector_index.reset();
      expr.selector_last = true;
    }
    if (!consume(TokenType::RParen, "Expected ) after TEXT expression")) return false;
    expr.span = Span{start, current_.pos};
    return true;
  }

  if (fn == "ATTR" || fn == "FIRST_ATTR" || fn == "LAST_ATTR") {
    advance();
    if (!consume(TokenType::LParen, "Expected ( after ATTR")) return false;
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected tag identifier inside ATTR()");
    }
    expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::Attr;
    expr.tag = to_lower(current_.text);
    advance();
    if (!consume(TokenType::Comma, "Expected , after tag in ATTR()")) return false;
    if (current_.type != TokenType::Identifier) {
      return set_error("Expected attribute identifier in ATTR()");
    }
    expr.attribute = to_lower(current_.text);
    advance();
    if (current_.type == TokenType::KeywordWhere) {
      advance();
      Expr where_expr;
      if (!parse_expr(where_expr)) return false;
      expr.where = std::move(where_expr);
    }
    if (!parse_selector_position(expr, "ATTR()")) return false;
    if (fn == "FIRST_ATTR") {
      expr.selector_index = 1;
      expr.selector_last = false;
    } else if (fn == "LAST_ATTR") {
      expr.selector_index.reset();
      expr.selector_last = true;
    }
    if (!consume(TokenType::RParen, "Expected ) after ATTR expression")) return false;
    expr.span = Span{start, current_.pos};
    return true;
  }

  advance();

  if (fn == "COALESCE") {
    if (!consume(TokenType::LParen, "Expected ( after COALESCE")) return false;
    expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::Coalesce;
    Query::SelectItem::FlattenExtractExpr arg;
    if (!parse_flatten_extract_expr(arg)) return false;
    expr.args.push_back(std::move(arg));
    while (current_.type == TokenType::Comma) {
      advance();
      Query::SelectItem::FlattenExtractExpr next_arg;
      if (!parse_flatten_extract_expr(next_arg)) return false;
      expr.args.push_back(std::move(next_arg));
    }
    if (expr.args.size() < 2) {
      return set_error("COALESCE() requires at least two expressions");
    }
    if (!consume(TokenType::RParen, "Expected ) after COALESCE expression")) return false;
    expr.span = Span{start, current_.pos};
    return true;
  }

  expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::FunctionCall;
  expr.function_name = fn;
  if (!consume(TokenType::LParen, "Expected ( after function name")) return false;
  if (fn == "POSITION") {
    Query::SelectItem::FlattenExtractExpr needle;
    if (!parse_flatten_extract_expr(needle)) return false;
    expr.args.push_back(std::move(needle));
    if (!consume(TokenType::KeywordIn, "Expected IN inside POSITION(substr IN str)")) return false;
    Query::SelectItem::FlattenExtractExpr haystack;
    if (!parse_flatten_extract_expr(haystack)) return false;
    expr.args.push_back(std::move(haystack));
  } else if (current_.type != TokenType::RParen) {
    Query::SelectItem::FlattenExtractExpr arg;
    if (!parse_flatten_extract_expr(arg)) return false;
    expr.args.push_back(std::move(arg));
    while (current_.type == TokenType::Comma) {
      advance();
      Query::SelectItem::FlattenExtractExpr next_arg;
      if (!parse_flatten_extract_expr(next_arg)) return false;
      expr.args.push_back(std::move(next_arg));
    }
  }
  if (!consume(TokenType::RParen, "Expected ) after function arguments")) return false;
  expr.span = Span{start, current_.pos};
  return true;
}

bool Parser::parse_flatten_extract_alias_expr_pairs(Query::SelectItem& item) {
  if (current_.type != TokenType::Identifier) {
    return set_error("Expected alias: expression inside PROJECT/FLATTEN_EXTRACT AS (...)");
  }
  while (true) {
    if (current_.type != TokenType::Identifier) {
      return set_error("Expected alias identifier in PROJECT/FLATTEN_EXTRACT AS (...)");
    }
    item.flatten_extract_aliases.push_back(current_.text);
    advance();
    if (!consume(TokenType::Colon, "Expected : after alias in PROJECT/FLATTEN_EXTRACT AS (...)")) {
      return false;
    }
    Query::SelectItem::FlattenExtractExpr expr;
    if (!parse_flatten_extract_expr(expr)) return false;
    while (current_.type == TokenType::Equal || current_.type == TokenType::NotEqual ||
           current_.type == TokenType::Less || current_.type == TokenType::LessEqual ||
           current_.type == TokenType::Greater || current_.type == TokenType::GreaterEqual ||
           current_.type == TokenType::KeywordLike) {
      Query::SelectItem::FlattenExtractExpr rhs;
      std::string op;
      if (current_.type == TokenType::Equal)
        op = "__CMP_EQ";
      else if (current_.type == TokenType::NotEqual)
        op = "__CMP_NE";
      else if (current_.type == TokenType::Less)
        op = "__CMP_LT";
      else if (current_.type == TokenType::LessEqual)
        op = "__CMP_LE";
      else if (current_.type == TokenType::Greater)
        op = "__CMP_GT";
      else if (current_.type == TokenType::GreaterEqual)
        op = "__CMP_GE";
      else
        op = "__CMP_LIKE";
      advance();
      if (!parse_flatten_extract_expr(rhs)) return false;
      Query::SelectItem::FlattenExtractExpr cmp_expr;
      cmp_expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::FunctionCall;
      cmp_expr.function_name = op;
      cmp_expr.args.push_back(std::move(expr));
      cmp_expr.args.push_back(std::move(rhs));
      expr = std::move(cmp_expr);
    }
    item.flatten_extract_exprs.push_back(std::move(expr));
    if (current_.type == TokenType::Comma) {
      advance();
      if (current_.type == TokenType::RParen) {
        advance();
        break;
      }
      continue;
    }
    if (current_.type == TokenType::RParen) {
      advance();
      break;
    }
    return set_error("Expected , or ) after PROJECT/FLATTEN_EXTRACT expression");
  }
  return true;
}

}  // namespace markql
