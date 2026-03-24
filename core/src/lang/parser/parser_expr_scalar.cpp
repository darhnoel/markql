#include "parser_internal.h"

namespace markql {

namespace {

bool is_tag_identifier_token(TokenType type) {
  return type == TokenType::Identifier || type == TokenType::KeywordTable;
}

}  // namespace

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
  if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable &&
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

bool Parser::parse_scalar_function(const std::string& function_name, size_t start_pos,
                                   ScalarExpr& out) {
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

  if (function_name == "TEXT" || function_name == "DIRECT_TEXT" || function_name == "INNER_HTML" ||
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
      } else if (current_.type == TokenType::Identifier && to_upper(current_.text) == "MAX_DEPTH") {
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
    if (current_.type != TokenType::Identifier)
      return set_error("Expected attribute identifier inside ATTR()");
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
  auto is_attr_keyword = [&](const std::string& upper) {
    return upper == "ATTRIBUTES" || upper == "ATTR";
  };
  if (current_.type == TokenType::KeywordSelf) {
    size_t self_pos = current_.pos;
    advance();
    if (current_.type != TokenType::Dot) {
      return set_error(
          "`self` refers to the current row node from FROM. Example: SELECT self.node_id, self.tag "
          "FROM doc");
    }
    advance();
    if (current_.type != TokenType::Identifier) {
      return set_error(
          "Expected self.<field> where field is node_id, tag, parent_id, doc_order, sibling_pos, "
          "max_depth, attributes/attr, or text");
    }
    std::string field = to_upper(current_.text);
    operand.axis = Operand::Axis::Self;
    operand.qualifier = "self";
    if (is_attr_keyword(field)) {
      size_t attributes_pos = current_.pos;
      advance();
      if (current_.type == TokenType::Dot) {
        advance();
        if (current_.type != TokenType::Identifier)
          return set_error("Expected attribute name after self.attributes/self.attr.");
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
      return set_error(
          "Expected self.<field> where field is node_id, tag, parent_id, doc_order, sibling_pos, "
          "max_depth, attributes/attr, or text");
    }
    operand.span = Span{self_pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
    return set_error("Expected identifier");
  }
  if (is_attr_keyword(to_upper(current_.text))) {
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
    if (current_.type != TokenType::Identifier)
      return set_error("Expected attributes, tag, text, or parent_id after parent");
    std::string next = to_upper(current_.text);
    if (is_attr_keyword(next)) {
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
    if (current_.type != TokenType::Identifier)
      return set_error("Expected attributes, tag, text, node_id, or parent_id after child");
    std::string next = to_upper(current_.text);
    if (is_attr_keyword(next)) {
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
    if (current_.type != TokenType::Identifier)
      return set_error("Expected attributes, tag, text, node_id, or parent_id after ancestor");
    std::string next = to_upper(current_.text);
    if (is_attr_keyword(next)) {
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
    if (current_.type != TokenType::Identifier)
      return set_error("Expected attributes, tag, text, node_id, or parent_id after descendant");
    std::string next = to_upper(current_.text);
    if (is_attr_keyword(next)) {
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
  if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable &&
      current_.type != TokenType::KeywordSelf) {
    return set_error("Expected attributes, parent, or attribute name after qualifier");
  }
  if (is_attr_keyword(to_upper(current_.text))) {
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
  if (to_upper(current_.text) == "TAG") {
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::Tag;
    operand.qualifier = qualifier;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (to_upper(current_.text) == "TEXT") {
    operand.axis = Operand::Axis::Self;
    operand.field_kind = Operand::FieldKind::Text;
    operand.qualifier = qualifier;
    operand.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  if (to_upper(current_.text) == "PARENT") {
    advance();
    if (!consume(TokenType::Dot, "Expected . after parent")) return false;
    if (current_.type != TokenType::Identifier)
      return set_error("Expected attributes, tag, text, node_id, or parent_id after parent");
    std::string next = to_upper(current_.text);
    if (is_attr_keyword(next)) {
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
    if (current_.type != TokenType::Identifier)
      return set_error("Expected attributes, tag, text, or parent_id after child");
    std::string next = to_upper(current_.text);
    if (is_attr_keyword(next)) {
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
    if (current_.type != TokenType::Identifier)
      return set_error("Expected attributes, tag, text, or parent_id after ancestor");
    std::string next = to_upper(current_.text);
    if (is_attr_keyword(next)) {
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
    if (current_.type != TokenType::Identifier)
      return set_error("Expected attributes, tag, text, or parent_id after descendant");
    std::string next = to_upper(current_.text);
    if (is_attr_keyword(next)) {
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

}  // namespace markql
