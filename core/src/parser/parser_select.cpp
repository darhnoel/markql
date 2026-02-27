#include "parser_internal.h"

#include <limits>

namespace xsql {

/// Parses the SELECT projection list, enforcing projection rules.
/// MUST reject mixing tag-only and projected fields.
/// Inputs are token stream; outputs are select items or errors.
bool Parser::parse_select_list(std::vector<Query::SelectItem>& items) {
  bool saw_field = false;
  bool saw_tag_only = false;
  if (!parse_select_item(items, saw_field, saw_tag_only)) return false;
  while (current_.type == TokenType::Comma) {
    advance();
    if (!parse_select_item(items, saw_field, saw_tag_only)) return false;
  }
  // WHY: mixing tag-only and projected fields breaks output schema invariants.
  if (saw_field && saw_tag_only) {
    return set_error("Cannot mix tag-only and projected fields in SELECT");
  }
  return true;
}

/// Parses EXCLUDE fields for SELECT * projections.
/// MUST accept a single field or a parenthesized list.
/// Inputs are tokens; outputs are field names or errors.
bool Parser::parse_exclude_list(std::vector<std::string>& fields) {
  if (current_.type == TokenType::Identifier) {
    fields.push_back(to_lower(current_.text));
    advance();
    return true;
  }
  if (current_.type == TokenType::LParen) {
    advance();
    if (current_.type != TokenType::Identifier) {
      return set_error("Expected field name in EXCLUDE list");
    }
    while (true) {
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected field name in EXCLUDE list");
      }
      fields.push_back(to_lower(current_.text));
      advance();
      if (current_.type == TokenType::Comma) {
        advance();
        continue;
      }
      if (current_.type == TokenType::RParen) {
        advance();
        break;
      }
      return set_error("Expected , or ) after EXCLUDE field");
    }
    return true;
  }
  return set_error("Expected field name or list after EXCLUDE");
}

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
  if (current_.type != TokenType::Identifier &&
      current_.type != TokenType::KeywordTable &&
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
    bool operand_like =
        peek().type == TokenType::Dot ||
        fn == "ATTRIBUTES" ||
        fn == "TAG" ||
        fn == "TEXT" ||
        fn == "NODE_ID" ||
        fn == "PARENT_ID" ||
        fn == "SIBLING_POS" ||
        fn == "MAX_DEPTH" ||
        fn == "DOC_ORDER" ||
        fn == "SELF" ||
        fn == "PARENT" ||
        fn == "CHILD" ||
        fn == "ANCESTOR" ||
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
    while (current_.type == TokenType::Equal ||
           current_.type == TokenType::NotEqual ||
           current_.type == TokenType::Less ||
           current_.type == TokenType::LessEqual ||
           current_.type == TokenType::Greater ||
           current_.type == TokenType::GreaterEqual ||
           current_.type == TokenType::KeywordLike) {
      Query::SelectItem::FlattenExtractExpr rhs;
      std::string op;
      if (current_.type == TokenType::Equal) op = "__CMP_EQ";
      else if (current_.type == TokenType::NotEqual) op = "__CMP_NE";
      else if (current_.type == TokenType::Less) op = "__CMP_LT";
      else if (current_.type == TokenType::LessEqual) op = "__CMP_LE";
      else if (current_.type == TokenType::Greater) op = "__CMP_GT";
      else if (current_.type == TokenType::GreaterEqual) op = "__CMP_GE";
      else op = "__CMP_LIKE";
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

/// Parses a single select item including functions and aggregates.
/// MUST set saw_field/saw_tag_only consistently for validation.
/// Inputs are tokens; outputs are select items or errors.
bool Parser::parse_select_item(std::vector<Query::SelectItem>& items, bool& saw_field, bool& saw_tag_only) {
  if (current_.type == TokenType::KeywordProject ||
      (current_.type == TokenType::Identifier &&
       to_upper(current_.text) == "FLATTEN_EXTRACT")) {
      Query::SelectItem item;
      item.flatten_extract = true;
      size_t start = current_.pos;
      advance();
      if (!consume(TokenType::LParen, "Expected ( after PROJECT/FLATTEN_EXTRACT")) return false;
      if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
        return set_error("Expected base tag identifier inside PROJECT()/FLATTEN_EXTRACT()");
      }
      item.tag = current_.text;
      advance();
      if (!consume(TokenType::RParen, "Expected ) after PROJECT/FLATTEN_EXTRACT argument")) return false;
      if (current_.type != TokenType::KeywordAs) {
        return set_error("PROJECT()/FLATTEN_EXTRACT() requires AS (alias: expression, ...)");
      }
      advance();
      if (!consume(TokenType::LParen, "Expected ( after AS")) return false;
      if (!parse_flatten_extract_alias_expr_pairs(item)) return false;
      item.span = Span{start, current_.pos};
      items.push_back(item);
      saw_field = true;
      return true;
  }
  if (current_.type == TokenType::KeywordSelf) {
    Query::SelectItem item;
    size_t start = current_.pos;
    ScalarExpr expr;
    if (!parse_scalar_expr(expr)) return false;
    if (expr.kind != ScalarExpr::Kind::Operand ||
        expr.operand.axis != Operand::Axis::Self ||
        !expr.operand.qualifier.has_value() ||
        to_lower(*expr.operand.qualifier) != "self") {
      return set_error("Expected self.<field> in SELECT projection");
    }
    item.expr_projection = true;
    item.expr = expr;
    std::string column = "expr";
    switch (expr.operand.field_kind) {
      case Operand::FieldKind::NodeId:
        column = "node_id";
        break;
      case Operand::FieldKind::Tag:
        column = "tag";
        break;
      case Operand::FieldKind::ParentId:
        column = "parent_id";
        break;
      case Operand::FieldKind::DocOrder:
        column = "doc_order";
        break;
      case Operand::FieldKind::SiblingPos:
        column = "sibling_pos";
        break;
      case Operand::FieldKind::MaxDepth:
        column = "max_depth";
        break;
      case Operand::FieldKind::AttributesMap:
        column = "attributes";
        break;
      case Operand::FieldKind::Text:
        column = "text";
        break;
      case Operand::FieldKind::Attribute:
        column = expr.operand.attribute;
        break;
    }
    if (current_.type == TokenType::KeywordAs) {
      advance();
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected alias identifier after AS");
      }
      column = current_.text;
      advance();
    }
    item.field = column;
    item.tag = "*";
    item.span = Span{start, current_.pos};
    items.push_back(item);
    saw_field = true;
    return true;
  }
  if (current_.type == TokenType::Identifier) {
    std::string fn = to_upper(current_.text);
    if (fn != "FLATTEN_TEXT" && fn != "FLATTEN") {
      // fallthrough
    } else {
    Query::SelectItem item;
    item.flatten_text = true;
    size_t start = current_.pos;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after FLATTEN_TEXT/FLATTEN")) return false;
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected base tag identifier inside FLATTEN_TEXT()/FLATTEN()");
    }
    item.tag = current_.text;
    advance();
    if (current_.type == TokenType::Comma) {
      advance();
      if (current_.type != TokenType::Number) {
        return set_error("Expected numeric depth in FLATTEN_TEXT()/FLATTEN()");
      }
      try {
        item.flatten_depth = static_cast<size_t>(std::stoull(current_.text));
      } catch (...) {
        return set_error("Invalid FLATTEN_TEXT()/FLATTEN() depth");
      }
      advance();
    }
    if (!consume(TokenType::RParen, "Expected ) after FLATTEN_TEXT/FLATTEN arguments")) return false;
    if (current_.type == TokenType::KeywordAs) {
      advance();
      if (!consume(TokenType::LParen, "Expected ( after AS")) return false;
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected column alias inside AS()");
      }
      while (true) {
        if (current_.type != TokenType::Identifier) {
          return set_error("Expected column alias inside AS()");
        }
        item.flatten_aliases.push_back(current_.text);
        advance();
        if (current_.type == TokenType::Comma) {
          advance();
          continue;
        }
        if (current_.type == TokenType::RParen) {
          advance();
          break;
        }
        return set_error("Expected , or ) after column alias");
      }
    } else {
      item.flatten_aliases = {"flatten_text"};
    }
    item.span = Span{start, current_.pos};
    items.push_back(item);
    saw_field = true;
    return true;
    }
  }
  if (current_.type == TokenType::Identifier && to_upper(current_.text) == "SUMMARIZE") {
    Query::SelectItem item;
    item.aggregate = Query::SelectItem::Aggregate::Summarize;
    size_t start = current_.pos;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after SUMMARIZE")) return false;
    if (current_.type != TokenType::Star) {
      return set_error("Expected * inside SUMMARIZE()");
    }
    item.tag = "*";
    item.span = Span{start, current_.pos + 1};
    advance();
    if (!consume(TokenType::RParen, "Expected ) after SUMMARIZE argument")) return false;
    items.push_back(item);
    saw_field = true;
    return true;
  }
  if (current_.type == TokenType::Identifier && to_upper(current_.text) == "TFIDF") {
    Query::SelectItem item;
    item.aggregate = Query::SelectItem::Aggregate::Tfidf;
    size_t start = current_.pos;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after TFIDF")) return false;
    bool saw_tag = false;
    bool saw_star = false;
    bool saw_option = false;
    if (current_.type != TokenType::RParen) {
      while (true) {
        if (current_.type == TokenType::Star) {
          if (saw_tag || saw_star) {
            return set_error("TFIDF(*) cannot be combined with other tags");
          }
          item.tfidf_all_tags = true;
          saw_star = true;
          advance();
        } else if (current_.type == TokenType::Identifier) {
          std::string token = to_upper(current_.text);
          if (peek().type == TokenType::Equal) {
            saw_option = true;
            advance();
            if (!consume(TokenType::Equal, "Expected = after TFIDF option")) return false;
            if (token == "TOP_TERMS") {
              if (current_.type != TokenType::Number) {
                return set_error("Expected numeric TOP_TERMS value");
              }
              try {
                item.tfidf_top_terms = static_cast<size_t>(std::stoull(current_.text));
              } catch (...) {
                return set_error("Invalid TOP_TERMS value");
              }
              if (item.tfidf_top_terms == 0) {
                return set_error("TOP_TERMS must be > 0");
              }
              advance();
            } else if (token == "MIN_DF") {
              if (current_.type != TokenType::Number) {
                return set_error("Expected numeric MIN_DF value");
              }
              try {
                item.tfidf_min_df = static_cast<size_t>(std::stoull(current_.text));
              } catch (...) {
                return set_error("Invalid MIN_DF value");
              }
              advance();
            } else if (token == "MAX_DF") {
              if (current_.type != TokenType::Number) {
                return set_error("Expected numeric MAX_DF value");
              }
              try {
                item.tfidf_max_df = static_cast<size_t>(std::stoull(current_.text));
              } catch (...) {
                return set_error("Invalid MAX_DF value");
              }
              advance();
            } else if (token == "STOPWORDS") {
              if (current_.type != TokenType::Identifier && current_.type != TokenType::String) {
                return set_error("Expected STOPWORDS value");
              }
              std::string value = to_upper(current_.text);
              if (value == "NONE" || value == "OFF") {
                item.tfidf_stopwords = Query::SelectItem::TfidfStopwords::None;
              } else if (value == "ENGLISH" || value == "DEFAULT") {
                item.tfidf_stopwords = Query::SelectItem::TfidfStopwords::English;
              } else {
                return set_error("Expected STOPWORDS=ENGLISH or STOPWORDS=NONE");
              }
              advance();
            } else {
              return set_error("Unknown TFIDF option: " + token);
            }
          } else {
            if (saw_option) {
              return set_error("TFIDF tags must precede options");
            }
            if (saw_star) {
              return set_error("TFIDF(*) cannot be combined with other tags");
            }
            item.tfidf_tags.push_back(current_.text);
            saw_tag = true;
            advance();
          }
        } else {
          return set_error("Expected tag or option inside TFIDF()");
        }
        if (current_.type == TokenType::Comma) {
          advance();
          continue;
        }
        break;
      }
    }
    if (!consume(TokenType::RParen, "Expected ) after TFIDF arguments")) return false;
    if (!saw_tag && !saw_star) {
      return set_error("TFIDF() requires at least one tag or *");
    }
    item.span = Span{start, current_.pos};
    items.push_back(item);
    saw_field = true;
    return true;
  }
  if (current_.type == TokenType::Identifier && to_upper(current_.text) == "TRIM") {
    Query::SelectItem item;
    size_t start = current_.pos;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after TRIM")) return false;
    if (current_.type == TokenType::Identifier &&
        (to_upper(current_.text) == "INNER_HTML" ||
         to_upper(current_.text) == "RAW_INNER_HTML")) {
      bool raw_inner_html = to_upper(current_.text) == "RAW_INNER_HTML";
      size_t inner_start = current_.pos;
      advance();
      if (!consume(TokenType::LParen, "Expected ( after inner_html/raw_inner_html")) return false;
      if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
        return set_error("Expected tag identifier inside inner_html()/raw_inner_html()");
      }
      item.tag = current_.text;
      item.field = "inner_html";
      item.inner_html_function = true;
      item.raw_inner_html_function = raw_inner_html;
      advance();
      if (current_.type == TokenType::Comma) {
        advance();
        if (current_.type == TokenType::Number) {
          try {
            item.inner_html_depth = static_cast<size_t>(std::stoull(current_.text));
          } catch (...) {
            return set_error("Invalid inner_html()/raw_inner_html() depth");
          }
          advance();
        } else if (current_.type == TokenType::Identifier &&
                   to_upper(current_.text) == "MAX_DEPTH") {
          item.inner_html_auto_depth = true;
          advance();
        } else {
          return set_error("Expected numeric depth or MAX_DEPTH in inner_html()/raw_inner_html()");
        }
      }
      if (!consume(TokenType::RParen, "Expected ) after inner_html/raw_inner_html argument")) return false;
      item.span = Span{inner_start, current_.pos};
    } else if (current_.type == TokenType::Identifier && to_upper(current_.text) == "TEXT") {
      size_t text_start = current_.pos;
      advance();
      if (!consume(TokenType::LParen, "Expected ( after text")) return false;
      if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
        return set_error("Expected tag identifier inside text()");
      }
      item.tag = current_.text;
      item.field = "text";
      item.text_function = true;
      advance();
      if (!consume(TokenType::RParen, "Expected ) after text argument")) return false;
      item.span = Span{text_start, current_.pos};
    } else {
      if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
        return set_error("Expected tag identifier inside TRIM()");
      }
      item.tag = current_.text;
      advance();
      if (current_.type != TokenType::Dot) {
        return set_error("Expected field after tag inside TRIM()");
      }
      advance();
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected field identifier after '.'");
      }
      item.field = current_.text;
      advance();
    }
    if (!consume(TokenType::RParen, "Expected ) after TRIM argument")) return false;
    item.trim = true;
    item.span = Span{start, current_.pos};
    items.push_back(item);
    saw_field = true;
    return true;
  }
  if (current_.type == TokenType::KeywordCount) {
    Query::SelectItem item;
    item.aggregate = Query::SelectItem::Aggregate::Count;
    size_t start = current_.pos;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after COUNT")) return false;
    if (current_.type == TokenType::Star) {
      item.tag = "*";
      item.field = "count";
      item.span = Span{start, current_.pos + 1};
      advance();
      if (!consume(TokenType::RParen, "Expected ) after COUNT argument")) return false;
      items.push_back(item);
      saw_field = true;
      return true;
    }
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected tag identifier inside COUNT()");
    }
    item.tag = current_.text;
    item.field = "count";
    item.span = Span{start, current_.pos + current_.text.size()};
    advance();
    if (!consume(TokenType::RParen, "Expected ) after COUNT argument")) return false;
    items.push_back(item);
    saw_field = true;
    return true;
  }
  if (current_.type == TokenType::KeywordCase ||
      ((current_.type == TokenType::Identifier || current_.type == TokenType::KeywordTable) &&
       peek().type == TokenType::LParen &&
       (to_upper(current_.text) == "FIRST_TEXT" ||
        to_upper(current_.text) == "LAST_TEXT" ||
        to_upper(current_.text) == "FIRST_ATTR" ||
        to_upper(current_.text) == "LAST_ATTR"))) {
    Query::SelectItem item;
    size_t start = current_.pos;
    Query::SelectItem::FlattenExtractExpr project_expr;
    if (!parse_flatten_extract_expr(project_expr)) return false;
    item.expr_projection = true;
    item.project_expr = std::move(project_expr);
    std::string column = "expr";
    if (item.project_expr.has_value()) {
      if (item.project_expr->kind == Query::SelectItem::FlattenExtractExpr::Kind::CaseWhen) {
        column = "case";
      } else if (item.project_expr->kind == Query::SelectItem::FlattenExtractExpr::Kind::FunctionCall &&
                 !item.project_expr->function_name.empty()) {
        column = to_lower(item.project_expr->function_name);
      }
    }
    if (current_.type == TokenType::KeywordAs) {
      advance();
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected alias identifier after AS");
      }
      column = current_.text;
      advance();
    }
    item.field = column;
    item.tag = "*";
    item.span = Span{start, current_.pos};
    items.push_back(item);
    saw_field = true;
    return true;
  }
  if ((current_.type == TokenType::Identifier || current_.type == TokenType::KeywordTable) &&
      peek().type == TokenType::LParen) {
    std::string fn = to_upper(current_.text);
    const bool scalar_fn =
        fn == "CONCAT" || fn == "SUBSTRING" || fn == "SUBSTR" ||
        fn == "LENGTH" || fn == "CHAR_LENGTH" || fn == "POSITION" ||
        fn == "LOCATE" || fn == "REPLACE" || fn == "LOWER" ||
        fn == "UPPER" || fn == "LTRIM" || fn == "RTRIM" ||
        fn == "TRIM" || fn == "DIRECT_TEXT" || fn == "COALESCE" ||
        fn == "ATTR";
    if (scalar_fn) {
      Query::SelectItem item;
      size_t start = current_.pos;
      ScalarExpr expr;
      if (!parse_scalar_expr(expr)) return false;
      item.expr_projection = true;
      item.expr = expr;
      std::string column = "expr";
      if (expr.kind == ScalarExpr::Kind::FunctionCall && !expr.function_name.empty()) {
        column = to_lower(expr.function_name);
      }
      if (current_.type == TokenType::KeywordAs) {
        advance();
        if (current_.type != TokenType::Identifier) {
          return set_error("Expected alias identifier after AS");
        }
        column = current_.text;
        advance();
      }
      item.field = column;
      item.tag = "*";
      item.span = Span{start, current_.pos};
      items.push_back(item);
      saw_field = true;
      return true;
    }
  }
  if (current_.type == TokenType::Star) {
    Query::SelectItem item;
    item.tag = "*";
    item.span = Span{current_.pos, current_.pos + 1};
    advance();
    items.push_back(item);
    saw_tag_only = true;
    return true;
  }
  if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
    return set_error("Expected tag identifier");
  }
  Token tag_token = current_;
  size_t start = current_.pos;
  advance();
  if (to_upper(tag_token.text) == "TEXT" && current_.type == TokenType::LParen) {
    advance();
    if (current_.type == TokenType::KeywordSelf) {
      Query::SelectItem item;
      item.expr_projection = true;
      item.field = "text";
      item.tag = "*";
      item.text_function = true;
      ScalarExpr expr;
      expr.kind = ScalarExpr::Kind::FunctionCall;
      expr.function_name = "TEXT";
      ScalarExpr arg;
      arg.kind = ScalarExpr::Kind::SelfRef;
      arg.self_ref.span = Span{current_.pos, current_.pos + current_.text.size()};
      arg.span = arg.self_ref.span;
      expr.args.push_back(std::move(arg));
      advance();
      if (!consume(TokenType::RParen, "Expected ) after text argument")) return false;
      expr.span = Span{start, current_.pos};
      item.expr = std::move(expr);
      if (current_.type == TokenType::KeywordAs) {
        advance();
        if (current_.type != TokenType::Identifier) {
          return set_error("Expected alias identifier after AS");
        }
        item.field = current_.text;
        advance();
      }
      item.span = Span{start, current_.pos};
      items.push_back(item);
      saw_field = true;
      return true;
    }
    Query::SelectItem item;
    item.field = "text";
    item.text_function = true;
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected tag identifier inside text()");
    }
    item.tag = current_.text;
    advance();
    if (!consume(TokenType::RParen, "Expected ) after text argument")) return false;
    if (current_.type == TokenType::KeywordAs) {
      advance();
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected alias identifier after AS");
      }
      // WHY: when TEXT(...) is aliased, preserve source semantics by treating it
      // as an explicit scalar projection instead of relying on tag-scoped defaults.
      ScalarExpr expr;
      expr.kind = ScalarExpr::Kind::FunctionCall;
      expr.function_name = "TEXT";
      ScalarExpr arg;
      arg.kind = ScalarExpr::Kind::StringLiteral;
      arg.string_value = to_lower(item.tag);
      arg.span = Span{start, current_.pos};
      expr.args.push_back(std::move(arg));
      expr.span = Span{start, current_.pos};
      item.expr_projection = true;
      item.expr = std::move(expr);
      item.tag = "*";
      item.text_function = false;
      item.field = current_.text;
      advance();
    }
    item.span = Span{start, current_.pos};
    items.push_back(item);
    saw_field = true;
    return true;
  }
  std::string fn_name = to_upper(tag_token.text);
  if ((fn_name == "INNER_HTML" || fn_name == "RAW_INNER_HTML") && current_.type == TokenType::LParen) {
    advance();
    if (current_.type == TokenType::KeywordSelf) {
      Query::SelectItem item;
      item.expr_projection = true;
      item.field = "inner_html";
      item.tag = "*";
      item.inner_html_function = true;
      item.raw_inner_html_function = (fn_name == "RAW_INNER_HTML");
      ScalarExpr expr;
      expr.kind = ScalarExpr::Kind::FunctionCall;
      expr.function_name = fn_name;
      ScalarExpr arg;
      arg.kind = ScalarExpr::Kind::SelfRef;
      arg.self_ref.span = Span{current_.pos, current_.pos + current_.text.size()};
      arg.span = arg.self_ref.span;
      expr.args.push_back(std::move(arg));
      advance();
      if (current_.type == TokenType::Comma) {
        advance();
        ScalarExpr depth_arg;
        if (current_.type == TokenType::Number) {
          depth_arg.kind = ScalarExpr::Kind::NumberLiteral;
          try {
            unsigned long long parsed = std::stoull(current_.text);
            if (parsed > static_cast<unsigned long long>(std::numeric_limits<int64_t>::max())) {
              return set_error("Invalid inner_html()/raw_inner_html() depth");
            }
            depth_arg.number_value = static_cast<int64_t>(parsed);
          } catch (...) {
            return set_error("Invalid inner_html()/raw_inner_html() depth");
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
          item.inner_html_auto_depth = true;
        } else {
          return set_error("Expected numeric depth or MAX_DEPTH in inner_html()/raw_inner_html()");
        }
        if (depth_arg.kind == ScalarExpr::Kind::NumberLiteral) {
          item.inner_html_depth = static_cast<size_t>(depth_arg.number_value);
        }
        expr.args.push_back(std::move(depth_arg));
      }
      if (!consume(TokenType::RParen, "Expected ) after inner_html/raw_inner_html argument")) return false;
      expr.span = Span{start, current_.pos};
      item.expr = std::move(expr);
      if (current_.type == TokenType::KeywordAs) {
        advance();
        if (current_.type != TokenType::Identifier) {
          return set_error("Expected alias identifier after AS");
        }
        item.field = current_.text;
        advance();
      }
      item.span = Span{start, current_.pos};
      items.push_back(item);
      saw_field = true;
      return true;
    }
    Query::SelectItem item;
    item.field = "inner_html";
    item.inner_html_function = true;
    item.raw_inner_html_function = (fn_name == "RAW_INNER_HTML");
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected tag identifier inside inner_html()/raw_inner_html()");
    }
    item.tag = current_.text;
    advance();
    if (current_.type == TokenType::Comma) {
      advance();
      if (current_.type == TokenType::Number) {
        try {
          item.inner_html_depth = static_cast<size_t>(std::stoull(current_.text));
        } catch (...) {
          return set_error("Invalid inner_html()/raw_inner_html() depth");
        }
        advance();
      } else if (current_.type == TokenType::Identifier &&
                 to_upper(current_.text) == "MAX_DEPTH") {
        item.inner_html_auto_depth = true;
        advance();
      } else {
        return set_error("Expected numeric depth or MAX_DEPTH in inner_html()/raw_inner_html()");
      }
    }
    if (!consume(TokenType::RParen, "Expected ) after inner_html/raw_inner_html argument")) return false;
    if (current_.type == TokenType::KeywordAs) {
      advance();
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected alias identifier after AS");
      }
      item.field = current_.text;
      advance();
    }
    item.span = Span{start, current_.pos};
    items.push_back(item);
    saw_field = true;
    return true;
  }
  Query::SelectItem item;
  item.tag = tag_token.text;
  if (current_.type == TokenType::LParen) {
    advance();
    if (current_.type != TokenType::Identifier) {
      return set_error("Expected field identifier inside tag()");
    }
    while (true) {
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected field identifier inside tag()");
      }
      Query::SelectItem field_item;
      field_item.tag = tag_token.text;
      field_item.field = current_.text;
      field_item.span = Span{start, current_.pos + current_.text.size()};
      items.push_back(field_item);
      saw_field = true;
      advance();
      if (current_.type == TokenType::Comma) {
        advance();
        continue;
      }
      if (current_.type == TokenType::RParen) {
        advance();
        break;
      }
      return set_error("Expected , or ) after field identifier");
    }
    return true;
  }
  if (current_.type == TokenType::Dot) {
    advance();
    if (current_.type != TokenType::Identifier) {
      return set_error("Expected field identifier after '.'");
    }
    std::string field_name = current_.text;
    item.field = field_name;
    item.span = Span{start, current_.pos + current_.text.size()};
    advance();
    if (current_.type == TokenType::KeywordAs) {
      advance();
      if (current_.type != TokenType::Identifier) {
        return set_error("Expected alias identifier after AS");
      }
      std::string output_alias = current_.text;
      advance();

      // WHY: `alias.field AS out` must keep reading from the qualified input field,
      // so we store it as an expression projection instead of mutating the source field.
      ScalarExpr expr;
      expr.kind = ScalarExpr::Kind::Operand;
      expr.operand.axis = Operand::Axis::Self;
      expr.operand.qualifier = tag_token.text;
      std::string upper = to_upper(field_name);
      if (upper == "TAG") {
        expr.operand.field_kind = Operand::FieldKind::Tag;
      } else if (upper == "TEXT") {
        expr.operand.field_kind = Operand::FieldKind::Text;
      } else if (upper == "NODE_ID") {
        expr.operand.field_kind = Operand::FieldKind::NodeId;
      } else if (upper == "PARENT_ID") {
        expr.operand.field_kind = Operand::FieldKind::ParentId;
      } else if (upper == "SIBLING_POS") {
        expr.operand.field_kind = Operand::FieldKind::SiblingPos;
      } else if (upper == "MAX_DEPTH") {
        expr.operand.field_kind = Operand::FieldKind::MaxDepth;
      } else if (upper == "DOC_ORDER") {
        expr.operand.field_kind = Operand::FieldKind::DocOrder;
      } else if (upper == "ATTRIBUTES" || upper == "ATTR") {
        expr.operand.field_kind = Operand::FieldKind::AttributesMap;
      } else {
        expr.operand.field_kind = Operand::FieldKind::Attribute;
        expr.operand.attribute = field_name;
      }
      expr.operand.span = item.span;
      expr.span = item.span;
      item.expr_projection = true;
      item.expr = std::move(expr);
      item.tag = "*";
      item.field = output_alias;
      item.span = Span{start, current_.pos};
    }
    saw_field = true;
  } else {
    item.span = Span{start, current_.pos};
    saw_tag_only = true;
  }
  items.push_back(item);
  return true;
}

}  // namespace xsql
