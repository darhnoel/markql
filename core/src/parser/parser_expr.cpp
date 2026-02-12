#include "parser_internal.h"

namespace xsql {

namespace {

bool is_tag_identifier_token(TokenType type) {
  return type == TokenType::Identifier || type == TokenType::KeywordTable;
}

}  // namespace

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

bool Parser::parse_scalar_expr(ScalarExpr& out) {
  if (current_.type == TokenType::String) {
    out.kind = ScalarExpr::Kind::StringLiteral;
    out.string_value = current_.text;
    out.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (current_.type == TokenType::Number) {
    out.kind = ScalarExpr::Kind::NumberLiteral;
    try {
      out.number_value = std::stoll(current_.text);
    } catch (...) {
      return set_error("Invalid numeric literal");
    }
    out.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (current_.type == TokenType::KeywordNull) {
    out.kind = ScalarExpr::Kind::NullLiteral;
    out.span = Span{current_.pos, current_.pos + 4};
    advance();
    return true;
  }
  if (current_.type == TokenType::KeywordSelf && peek().type != TokenType::Dot) {
    out.kind = ScalarExpr::Kind::SelfRef;
    out.self_ref.span = Span{current_.pos, current_.pos + current_.text.size()};
    out.span = out.self_ref.span;
    advance();
    return true;
  }
  if (current_.type != TokenType::Identifier &&
      current_.type != TokenType::KeywordTable &&
      current_.type != TokenType::KeywordSelf) {
    return set_error("Expected scalar expression");
  }
  Token ident = current_;
  std::string fn_name = to_upper(current_.text);
  if (peek().type == TokenType::LParen) {
    advance();
    return parse_scalar_function(fn_name, ident.pos, out);
  }
  Operand operand;
  if (!parse_operand(operand)) return false;
  out.kind = ScalarExpr::Kind::Operand;
  out.operand = std::move(operand);
  out.span = out.operand.span;
  return true;
}

bool Parser::parse_scalar_function(const std::string& function_name, size_t start_pos, ScalarExpr& out) {
  if (!consume(TokenType::LParen, "Expected ( after function name")) return false;
  out.kind = ScalarExpr::Kind::FunctionCall;
  out.function_name = function_name;

  if (function_name == "POSITION") {
    ScalarExpr needle;
    if (!parse_scalar_expr(needle)) return false;
    out.args.push_back(std::move(needle));
    if (!consume(TokenType::KeywordIn, "Expected IN inside POSITION(substr IN str)")) return false;
    ScalarExpr haystack;
    if (!parse_scalar_expr(haystack)) return false;
    out.args.push_back(std::move(haystack));
    if (!consume(TokenType::RParen, "Expected ) after POSITION arguments")) return false;
    out.span = Span{start_pos, current_.pos};
    return true;
  }

  if (function_name == "TEXT" ||
      function_name == "DIRECT_TEXT" ||
      function_name == "INNER_HTML" ||
      function_name == "RAW_INNER_HTML") {
    ScalarExpr node_or_tag_arg;
    if (current_.type == TokenType::KeywordSelf) {
      node_or_tag_arg.kind = ScalarExpr::Kind::SelfRef;
      node_or_tag_arg.self_ref.span = Span{current_.pos, current_.pos + current_.text.size()};
      node_or_tag_arg.span = node_or_tag_arg.self_ref.span;
      advance();
    } else if (is_tag_identifier_token(current_.type)) {
      node_or_tag_arg.kind = ScalarExpr::Kind::StringLiteral;
      node_or_tag_arg.string_value = to_lower(current_.text);
      node_or_tag_arg.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
    } else {
      return set_error("Expected tag identifier or self inside extraction function");
    }
    out.args.push_back(std::move(node_or_tag_arg));
    if ((function_name == "INNER_HTML" || function_name == "RAW_INNER_HTML") &&
        current_.type == TokenType::Comma) {
      advance();
      ScalarExpr depth_arg;
      if (current_.type == TokenType::Number) {
        depth_arg.kind = ScalarExpr::Kind::NumberLiteral;
        try {
          depth_arg.number_value = std::stoll(current_.text);
        } catch (...) {
          return set_error("Invalid numeric literal");
        }
        depth_arg.span = Span{current_.pos, current_.pos + current_.text.size()};
        advance();
      } else if (current_.type == TokenType::Identifier &&
                 to_upper(current_.text) == "MAX_DEPTH") {
        depth_arg.kind = ScalarExpr::Kind::Operand;
        depth_arg.operand.axis = Operand::Axis::Self;
        depth_arg.operand.field_kind = Operand::FieldKind::MaxDepth;
        depth_arg.operand.span = Span{current_.pos, current_.pos + current_.text.size()};
        depth_arg.span = depth_arg.operand.span;
        advance();
      } else {
        return set_error("Expected numeric depth or MAX_DEPTH in inner_html()/raw_inner_html()");
      }
      out.args.push_back(std::move(depth_arg));
    }
    if (!consume(TokenType::RParen, "Expected ) after extraction function arguments")) return false;
    out.span = Span{start_pos, current_.pos};
    return true;
  }

  if (function_name == "ATTR") {
    ScalarExpr node_or_tag_arg;
    if (current_.type == TokenType::KeywordSelf) {
      node_or_tag_arg.kind = ScalarExpr::Kind::SelfRef;
      node_or_tag_arg.self_ref.span = Span{current_.pos, current_.pos + current_.text.size()};
      node_or_tag_arg.span = node_or_tag_arg.self_ref.span;
      advance();
    } else if (is_tag_identifier_token(current_.type)) {
      node_or_tag_arg.kind = ScalarExpr::Kind::StringLiteral;
      node_or_tag_arg.string_value = to_lower(current_.text);
      node_or_tag_arg.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
    } else {
      return set_error("Expected tag identifier or self inside ATTR()");
    }
    out.args.push_back(std::move(node_or_tag_arg));
    if (!consume(TokenType::Comma, "Expected , after ATTR tag/node")) return false;
    if (current_.type != TokenType::Identifier) return set_error("Expected attribute identifier inside ATTR()");
    ScalarExpr attr_arg;
    attr_arg.kind = ScalarExpr::Kind::StringLiteral;
    attr_arg.string_value = to_lower(current_.text);
    attr_arg.span = Span{current_.pos, current_.pos + current_.text.size()};
    out.args.push_back(std::move(attr_arg));
    advance();
    if (!consume(TokenType::RParen, "Expected ) after ATTR arguments")) return false;
    out.span = Span{start_pos, current_.pos};
    return true;
  }

  if (current_.type != TokenType::RParen) {
    ScalarExpr arg;
    if (!parse_scalar_expr(arg)) return false;
    out.args.push_back(std::move(arg));
    while (current_.type == TokenType::Comma) {
      advance();
      ScalarExpr next_arg;
      if (!parse_scalar_expr(next_arg)) return false;
      out.args.push_back(std::move(next_arg));
    }
  }
  if (!consume(TokenType::RParen, "Expected ) after function arguments")) return false;
  out.span = Span{start_pos, current_.pos};
  return true;
}

/// Parses an operand with axes, fields, and qualifiers.
/// MUST set axis/field_kind consistently with grammar.
/// Inputs are tokens; outputs are Operand or errors.
bool Parser::parse_operand(Operand& operand) {
  if (current_.type == TokenType::KeywordSelf) {
    size_t self_pos = current_.pos;
    advance();
    if (current_.type != TokenType::Dot) {
      return set_error(
          "`self` refers to the current row node from FROM. Example: SELECT self.node_id, self.tag FROM doc");
    }
    advance();
    if (current_.type != TokenType::Identifier) {
      return set_error("Expected self.<field> where field is node_id, tag, parent_id, doc_order, sibling_pos, max_depth, attributes, or text");
    }
    std::string field = to_upper(current_.text);
    operand.axis = Operand::Axis::Self;
    operand.qualifier = "self";
    if (field == "ATTRIBUTES") {
      size_t attributes_pos = current_.pos;
      advance();
      if (current_.type == TokenType::Dot) {
        advance();
        if (current_.type != TokenType::Identifier) return set_error("Expected attribute name after self.attributes.");
        operand.field_kind = Operand::FieldKind::Attribute;
        operand.attribute = current_.text;
        operand.span = Span{self_pos, current_.pos + current_.text.size()};
        advance();
        return true;
      }
      operand.field_kind = Operand::FieldKind::AttributesMap;
      operand.span = Span{self_pos, attributes_pos + std::string("attributes").size()};
      return true;
    }
    if (field == "TAG") {
      operand.field_kind = Operand::FieldKind::Tag;
    } else if (field == "TEXT") {
      operand.field_kind = Operand::FieldKind::Text;
    } else if (field == "NODE_ID") {
      operand.field_kind = Operand::FieldKind::NodeId;
    } else if (field == "PARENT_ID") {
      operand.field_kind = Operand::FieldKind::ParentId;
    } else if (field == "SIBLING_POS") {
      operand.field_kind = Operand::FieldKind::SiblingPos;
    } else if (field == "MAX_DEPTH") {
      operand.field_kind = Operand::FieldKind::MaxDepth;
    } else if (field == "DOC_ORDER") {
      operand.field_kind = Operand::FieldKind::DocOrder;
    } else {
      return set_error("Expected self.<field> where field is node_id, tag, parent_id, doc_order, sibling_pos, max_depth, attributes, or text");
    }
    operand.span = Span{self_pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
    return set_error("Expected identifier");
  }
  if (to_upper(current_.text) == "ATTRIBUTES") {
    advance();
    if (current_.type == TokenType::Dot) {
      advance();
      if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
      operand.attribute = current_.text;
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::Attribute;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::AttributesMap;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    return true;
  }
  if (to_upper(current_.text) == "TEXT") {
    advance();
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::Text;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    return true;
  }
  if (to_upper(current_.text) == "TAG") {
    advance();
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::Tag;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    return true;
  }
  if (to_upper(current_.text) == "NODE_ID") {
    advance();
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::NodeId;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    return true;
  }
  if (to_upper(current_.text) == "PARENT_ID") {
    advance();
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::ParentId;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    return true;
  }
  if (to_upper(current_.text) == "SIBLING_POS") {
    advance();
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::SiblingPos;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    return true;
  }
  if (to_upper(current_.text) == "MAX_DEPTH") {
    advance();
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::MaxDepth;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    return true;
  }
  if (to_upper(current_.text) == "DOC_ORDER") {
    advance();
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::DocOrder;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    return true;
  }
  if (to_upper(current_.text) == "PARENT") {
    advance();
    if (!consume(TokenType::Dot, "Expected . after parent")) return false;
    if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, or parent_id after parent");
    std::string next = to_upper(current_.text);
    if (next == "ATTRIBUTES") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
      operand.attribute = current_.text;
      operand.axis = Operand::Axis::Parent;
      operand.field_kind = Operand::FieldKind::Attribute;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Parent;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
      } else {
        operand.field_kind = Operand::FieldKind::ParentId;
      }
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    return set_error("Expected attributes, tag, text, or parent_id after parent");
  }
  if (to_upper(current_.text) == "CHILD") {
    advance();
    if (!consume(TokenType::Dot, "Expected . after child")) return false;
    if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, node_id, or parent_id after child");
    std::string next = to_upper(current_.text);
    if (next == "ATTRIBUTES") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
      operand.attribute = current_.text;
      operand.axis = Operand::Axis::Child;
      operand.field_kind = Operand::FieldKind::Attribute;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Child;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
      } else {
        operand.field_kind = Operand::FieldKind::ParentId;
      }
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    return set_error("Expected attributes, tag, text, or parent_id after child");
  }
  if (to_upper(current_.text) == "ANCESTOR") {
    advance();
    if (!consume(TokenType::Dot, "Expected . after ancestor")) return false;
    if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, node_id, or parent_id after ancestor");
    std::string next = to_upper(current_.text);
    if (next == "ATTRIBUTES") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
      operand.attribute = current_.text;
      operand.axis = Operand::Axis::Ancestor;
      operand.field_kind = Operand::FieldKind::Attribute;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Ancestor;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
      } else {
        operand.field_kind = Operand::FieldKind::ParentId;
      }
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    return set_error("Expected attributes, tag, text, or parent_id after ancestor");
  }
  if (to_upper(current_.text) == "DESCENDANT") {
    advance();
    if (!consume(TokenType::Dot, "Expected . after descendant")) return false;
    if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, node_id, or parent_id after descendant");
    std::string next = to_upper(current_.text);
    if (next == "ATTRIBUTES") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
      operand.attribute = current_.text;
      operand.axis = Operand::Axis::Descendant;
      operand.field_kind = Operand::FieldKind::Attribute;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Descendant;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
      } else {
        operand.field_kind = Operand::FieldKind::ParentId;
      }
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    return set_error("Expected attributes, tag, text, or parent_id after descendant");
  }
  std::string qualifier = current_.text;
  advance();
  if (current_.type != TokenType::Dot) {
    if (to_upper(qualifier) == "TEXT") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::Text;
      operand.span = Span{current_.pos, current_.pos};
      return true;
    }
    if (to_upper(qualifier) == "TAG") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::Tag;
      operand.span = Span{current_.pos, current_.pos};
      return true;
    }
    if (to_upper(qualifier) == "NODE_ID") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::NodeId;
      operand.span = Span{current_.pos, current_.pos};
      return true;
    }
    if (to_upper(qualifier) == "PARENT_ID") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::ParentId;
      operand.span = Span{current_.pos, current_.pos};
      return true;
    }
    if (to_upper(qualifier) == "SIBLING_POS") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::SiblingPos;
      operand.span = Span{current_.pos, current_.pos};
      return true;
    }
    if (to_upper(qualifier) == "MAX_DEPTH") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::MaxDepth;
      operand.span = Span{current_.pos, current_.pos};
      return true;
    }
    if (to_upper(qualifier) == "DOC_ORDER") {
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::DocOrder;
      operand.span = Span{current_.pos, current_.pos};
      return true;
    }
    operand.attribute = qualifier;
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::Attribute;
    operand.span = Span{current_.pos, current_.pos};
    return true;
  }
  advance();
  if (current_.type != TokenType::Identifier) {
    return set_error("Expected attributes, parent, or attribute name after qualifier");
  }
  if (to_upper(current_.text) == "ATTRIBUTES") {
    advance();
    if (current_.type == TokenType::Dot) {
      advance();
      if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
      operand.attribute = current_.text;
      operand.axis = Operand::Axis::Self;
      operand.field_kind = Operand::FieldKind::Attribute;
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::AttributesMap;
    operand.qualifier = qualifier;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    return true;
  }
  if (to_upper(current_.text) == "PARENT_ID") {
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::ParentId;
    operand.qualifier = qualifier;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (to_upper(current_.text) == "SIBLING_POS") {
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::SiblingPos;
    operand.qualifier = qualifier;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (to_upper(current_.text) == "NODE_ID") {
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::NodeId;
    operand.qualifier = qualifier;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (to_upper(current_.text) == "MAX_DEPTH") {
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::MaxDepth;
    operand.qualifier = qualifier;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (to_upper(current_.text) == "DOC_ORDER") {
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::DocOrder;
    operand.qualifier = qualifier;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (to_upper(current_.text) == "PARENT") {
    advance();
    if (!consume(TokenType::Dot, "Expected . after parent")) return false;
    if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, node_id, or parent_id after parent");
    std::string next = to_upper(current_.text);
    if (next == "ATTRIBUTES") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
      operand.attribute = current_.text;
      operand.axis = Operand::Axis::Parent;
      operand.field_kind = Operand::FieldKind::Attribute;
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Parent;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
      } else {
        operand.field_kind = Operand::FieldKind::ParentId;
      }
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    return set_error("Expected attributes, tag, text, or parent_id after parent");
  }
  if (to_upper(current_.text) == "CHILD") {
    advance();
    if (!consume(TokenType::Dot, "Expected . after child")) return false;
    if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, or parent_id after child");
    std::string next = to_upper(current_.text);
    if (next == "ATTRIBUTES") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
      operand.attribute = current_.text;
      operand.axis = Operand::Axis::Child;
      operand.field_kind = Operand::FieldKind::Attribute;
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Child;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
      } else {
        operand.field_kind = Operand::FieldKind::ParentId;
      }
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    return set_error("Expected attributes, tag, text, or parent_id after child");
  }
  if (to_upper(current_.text) == "ANCESTOR") {
    advance();
    if (!consume(TokenType::Dot, "Expected . after ancestor")) return false;
    if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, or parent_id after ancestor");
    std::string next = to_upper(current_.text);
    if (next == "ATTRIBUTES") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
      operand.attribute = current_.text;
      operand.axis = Operand::Axis::Ancestor;
      operand.field_kind = Operand::FieldKind::Attribute;
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Ancestor;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
      } else {
        operand.field_kind = Operand::FieldKind::ParentId;
      }
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    return set_error("Expected attributes, tag, text, or parent_id after ancestor");
  }
  if (to_upper(current_.text) == "DESCENDANT") {
    advance();
    if (!consume(TokenType::Dot, "Expected . after descendant")) return false;
    if (current_.type != TokenType::Identifier) return set_error("Expected attributes, tag, text, or parent_id after descendant");
    std::string next = to_upper(current_.text);
    if (next == "ATTRIBUTES") {
      advance();
      if (!consume(TokenType::Dot, "Expected . after attributes")) return false;
      if (current_.type != TokenType::Identifier) return set_error("Expected attribute name");
      operand.attribute = current_.text;
      operand.axis = Operand::Axis::Descendant;
      operand.field_kind = Operand::FieldKind::Attribute;
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    if (next == "TAG" || next == "TEXT" || next == "NODE_ID" || next == "PARENT_ID" ||
        next == "SIBLING_POS" || next == "MAX_DEPTH" || next == "DOC_ORDER") {
      operand.axis = Operand::Axis::Descendant;
      if (next == "TAG") {
        operand.field_kind = Operand::FieldKind::Tag;
      } else if (next == "TEXT") {
        operand.field_kind = Operand::FieldKind::Text;
      } else if (next == "NODE_ID") {
        operand.field_kind = Operand::FieldKind::NodeId;
      } else if (next == "SIBLING_POS") {
        operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (next == "MAX_DEPTH") {
        operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (next == "DOC_ORDER") {
        operand.field_kind = Operand::FieldKind::DocOrder;
      } else {
        operand.field_kind = Operand::FieldKind::ParentId;
      }
      operand.qualifier = qualifier;
      operand.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      return true;
    }
    return set_error("Expected attributes, tag, text, or parent_id after descendant");
  }
  operand.attribute = current_.text;
  operand.axis = Operand::Axis::Self;
  operand.field_kind = Operand::FieldKind::Attribute;
  operand.qualifier = qualifier;
  operand.span = Span{current_.pos, current_.pos + current_.text.size()};
  advance();
  return true;
}

/// Parses a literal or list of literals for comparisons.
/// MUST accept strings/numbers and enforce list delimiters.
/// Inputs are tokens; outputs are ValueList or errors.
bool Parser::parse_value_list(ValueList& values) {
  if (current_.type == TokenType::String) {
    values.values.push_back(current_.text);
    values.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (current_.type == TokenType::Number) {
    values.values.push_back(current_.text);
    values.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (current_.type == TokenType::LParen) {
    size_t start = current_.pos;
    advance();
    if (current_.type != TokenType::String && current_.type != TokenType::Number) {
      return set_error("Expected string literal or number");
    }
    values.values.push_back(current_.text);
    advance();
    while (current_.type == TokenType::Comma) {
      advance();
      if (current_.type != TokenType::String && current_.type != TokenType::Number) {
        return set_error("Expected string literal or number");
      }
      values.values.push_back(current_.text);
      advance();
    }
    if (!consume(TokenType::RParen, "Expected )")) return false;
    values.span = Span{start, current_.pos};
    return true;
  }
  return set_error("Expected literal or list");
}

/// Parses a string literal or list of string literals.
/// MUST reject non-string values and enforce list delimiters.
/// Inputs are tokens; outputs are ValueList or errors.
bool Parser::parse_string_list(ValueList& values) {
  if (current_.type == TokenType::String) {
    values.values.push_back(current_.text);
    values.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (current_.type == TokenType::LParen) {
    size_t start = current_.pos;
    advance();
    if (current_.type != TokenType::String) {
      return set_error("Expected string literal");
    }
    values.values.push_back(current_.text);
    advance();
    while (current_.type == TokenType::Comma) {
      advance();
      if (current_.type != TokenType::String) {
        return set_error("Expected string literal");
      }
      values.values.push_back(current_.text);
      advance();
    }
    if (!consume(TokenType::RParen, "Expected )")) return false;
    values.span = Span{start, current_.pos};
    return true;
  }
  return set_error("Expected string literal or list");
}

}  // namespace xsql
