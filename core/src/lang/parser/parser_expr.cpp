#include "parser_internal.h"

namespace markql {

/// Parses an expression with OR precedence.
/// MUST build BinaryExpr nodes in left-associative order.
/// Inputs are tokens; outputs are Expr or errors.
bool Parser::parse_expr(Expr& out) {
  Expr left;
  if (!parse_and_expr(left)) return false;
  while (current_.type == TokenType::KeywordOr) {
    Token op = current_;
    advance();
    Expr right;
    if (!parse_and_expr(right)) return false;
    auto node = std::make_shared<BinaryExpr>();
    node->op = BinaryExpr::Op::Or;
    node->left = left;
    node->right = right;
    node->span = Span{op.pos, current_.pos};
    left = node;
  }
  out = left;
  return true;
}

/// Parses an expression with AND precedence.
/// MUST build BinaryExpr nodes in left-associative order.
/// Inputs are tokens; outputs are Expr or errors.
bool Parser::parse_and_expr(Expr& out) {
  Expr left;
  if (!parse_cmp_expr(left)) return false;
  while (current_.type == TokenType::KeywordAnd) {
    Token op = current_;
    advance();
    Expr right;
    if (!parse_cmp_expr(right)) return false;
    auto node = std::make_shared<BinaryExpr>();
    node->op = BinaryExpr::Op::And;
    node->left = left;
    node->right = right;
    node->span = Span{op.pos, current_.pos};
    left = node;
  }
  out = left;
  return true;
}

/// Parses comparison predicates and NULL checks.
/// MUST validate operators and value list cardinality.
/// Inputs are tokens; outputs are CompareExpr or errors.
bool Parser::parse_cmp_expr(Expr& out) {
  if (current_.type == TokenType::LParen) {
    advance();
    Expr inner;
    if (!parse_expr(inner)) return false;
    if (!consume(TokenType::RParen, "Expected ) to close expression")) return false;
    out = inner;
    return true;
  }
  if (current_.type == TokenType::KeywordExists) {
    Token exists_token = current_;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after EXISTS")) return false;
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordSelf) {
      return set_error("Expected axis name after EXISTS(");
    }
    Operand::Axis axis = Operand::Axis::Self;
    std::string axis_name = to_upper(current_.text);
    if (axis_name == "SELF") {
      axis = Operand::Axis::Self;
    } else if (axis_name == "PARENT") {
      axis = Operand::Axis::Parent;
    } else if (axis_name == "CHILD") {
      axis = Operand::Axis::Child;
    } else if (axis_name == "ANCESTOR") {
      axis = Operand::Axis::Ancestor;
    } else if (axis_name == "DESCENDANT") {
      axis = Operand::Axis::Descendant;
    } else {
      return set_error("Expected axis name (self, parent, child, ancestor, descendant)");
    }
    advance();
    std::optional<Expr> filter;
    if (current_.type == TokenType::KeywordWhere) {
      advance();
      Expr inner;
      if (!parse_expr(inner)) return false;
      filter = inner;
    }
    if (!consume(TokenType::RParen, "Expected ) after EXISTS(...)")) return false;
    auto node = std::make_shared<ExistsExpr>();
    node->axis = axis;
    node->where = std::move(filter);
    node->span = Span{exists_token.pos, current_.pos};
    out = node;
    return true;
  }
  // WHY: keep the legacy shorthand while routing behavior through SQL-style LIKE semantics.
  if (current_.type == TokenType::Identifier && peek().type == TokenType::KeywordHasDirectText) {
    Token tag_token = current_;
    advance();
    advance();
    if (current_.type != TokenType::String) {
      return set_error("Expected string literal after HAS_DIRECT_TEXT");
    }
    std::string needle = current_.text;
    advance();

    CompareExpr tag_cmp;
    tag_cmp.op = CompareExpr::Op::Eq;
    tag_cmp.lhs.axis = Operand::Axis::Self;
    tag_cmp.lhs.field_kind = Operand::FieldKind::Tag;
    tag_cmp.lhs.attribute = "tag";
    tag_cmp.rhs.values = {to_lower(tag_token.text)};

    ScalarExpr direct_text_expr;
    direct_text_expr.kind = ScalarExpr::Kind::FunctionCall;
    direct_text_expr.function_name = "DIRECT_TEXT";
    ScalarExpr tag_arg;
    tag_arg.kind = ScalarExpr::Kind::StringLiteral;
    tag_arg.string_value = to_lower(tag_token.text);
    direct_text_expr.args.push_back(tag_arg);

    CompareExpr like_cmp;
    like_cmp.op = CompareExpr::Op::Like;
    like_cmp.lhs_expr = direct_text_expr;
    ScalarExpr pattern_expr;
    pattern_expr.kind = ScalarExpr::Kind::StringLiteral;
    pattern_expr.string_value = "%" + needle + "%";
    like_cmp.rhs_expr = pattern_expr;

    auto node = std::make_shared<BinaryExpr>();
    node->op = BinaryExpr::Op::And;
    node->left = tag_cmp;
    node->right = like_cmp;
    out = node;
    return true;
  }

  ScalarExpr lhs;
  if (!parse_scalar_expr(lhs)) return false;

  CompareExpr cmp;
  cmp.lhs_expr = lhs;
  if (lhs.kind == ScalarExpr::Kind::Operand) {
    cmp.lhs = lhs.operand;
  }
  if (current_.type == TokenType::KeywordContains) {
    cmp.op = CompareExpr::Op::Contains;
    advance();
    if (current_.type == TokenType::KeywordAll) {
      cmp.op = CompareExpr::Op::ContainsAll;
      advance();
    } else if (current_.type == TokenType::KeywordAny) {
      cmp.op = CompareExpr::Op::ContainsAny;
      advance();
    }
    ValueList values;
    if (!parse_string_list(values)) return false;
    if (cmp.op == CompareExpr::Op::Contains && values.values.size() != 1) {
      return set_error("CONTAINS with multiple values requires ALL or ANY");
    }
    cmp.rhs = values;
    cmp.rhs_expr_list.clear();
    for (const auto& value : values.values) {
      ScalarExpr lit;
      lit.kind = ScalarExpr::Kind::StringLiteral;
      lit.string_value = value;
      cmp.rhs_expr_list.push_back(std::move(lit));
    }
    out = cmp;
    return true;
  }
  if (current_.type == TokenType::KeywordHasDirectText) {
    cmp.op = CompareExpr::Op::HasDirectText;
    advance();
    if (!(lhs.kind == ScalarExpr::Kind::Operand &&
          lhs.operand.field_kind == Operand::FieldKind::Tag)) {
      return set_error("HAS_DIRECT_TEXT expects a tag identifier");
    }
    if (current_.type != TokenType::String) {
      return set_error("Expected string literal after HAS_DIRECT_TEXT");
    }
    ValueList values;
    values.values.push_back(current_.text);
    values.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    cmp.rhs = values;
    ScalarExpr rhs;
    rhs.kind = ScalarExpr::Kind::StringLiteral;
    rhs.string_value = values.values.front();
    cmp.rhs_expr = rhs;
    out = cmp;
    return true;
  }
  if (current_.type == TokenType::KeywordIn) {
    cmp.op = CompareExpr::Op::In;
    advance();
    if (current_.type == TokenType::LParen) {
      advance();
      ScalarExpr value;
      if (!parse_scalar_expr(value)) return false;
      cmp.rhs_expr_list.push_back(value);
      while (current_.type == TokenType::Comma) {
        advance();
        ScalarExpr next_value;
        if (!parse_scalar_expr(next_value)) return false;
        cmp.rhs_expr_list.push_back(next_value);
      }
      if (!consume(TokenType::RParen, "Expected )")) return false;
    } else {
      ScalarExpr value;
      if (!parse_scalar_expr(value)) return false;
      cmp.rhs_expr_list.push_back(value);
    }
    bool all_literals = true;
    for (const auto& value : cmp.rhs_expr_list) {
      if (value.kind == ScalarExpr::Kind::StringLiteral) {
        cmp.rhs.values.push_back(value.string_value);
      } else if (value.kind == ScalarExpr::Kind::NumberLiteral) {
        cmp.rhs.values.push_back(std::to_string(value.number_value));
      } else {
        all_literals = false;
        break;
      }
    }
    if (!all_literals) {
      cmp.rhs.values.clear();
    }
    out = cmp;
    return true;
  }
  if (current_.type == TokenType::KeywordIs) {
    advance();
    if (current_.type == TokenType::KeywordNot) {
      advance();
      if (!consume(TokenType::KeywordNull, "Expected NULL after IS NOT")) return false;
      cmp.op = CompareExpr::Op::IsNotNull;
      out = cmp;
      return true;
    }
    if (!consume(TokenType::KeywordNull, "Expected NULL after IS")) return false;
    cmp.op = CompareExpr::Op::IsNull;
    out = cmp;
    return true;
  }

  if (current_.type == TokenType::Equal) {
    cmp.op = CompareExpr::Op::Eq;
  } else if (current_.type == TokenType::NotEqual) {
    cmp.op = CompareExpr::Op::NotEq;
  } else if (current_.type == TokenType::Less) {
    cmp.op = CompareExpr::Op::Lt;
  } else if (current_.type == TokenType::LessEqual) {
    cmp.op = CompareExpr::Op::Lte;
  } else if (current_.type == TokenType::Greater) {
    cmp.op = CompareExpr::Op::Gt;
  } else if (current_.type == TokenType::GreaterEqual) {
    cmp.op = CompareExpr::Op::Gte;
  } else if (current_.type == TokenType::RegexMatch) {
    cmp.op = CompareExpr::Op::Regex;
  } else if (current_.type == TokenType::KeywordLike) {
    cmp.op = CompareExpr::Op::Like;
  } else {
    return set_error("Expected =, <>, <, <=, >, >=, ~, LIKE, IN, CONTAINS, HAS_DIRECT_TEXT, or IS");
  }
  advance();
  ScalarExpr rhs;
  if (!parse_scalar_expr(rhs)) return false;
  cmp.rhs_expr = rhs;
  if (rhs.kind == ScalarExpr::Kind::StringLiteral || rhs.kind == ScalarExpr::Kind::NumberLiteral) {
    cmp.rhs.values.push_back(rhs.kind == ScalarExpr::Kind::StringLiteral
                                 ? rhs.string_value
                                 : std::to_string(rhs.number_value));
  }
  out = cmp;
  return true;
}

}  // namespace markql
