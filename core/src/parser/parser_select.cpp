#include "parser_internal.h"

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
  if (current_.type != TokenType::Identifier) {
    return set_error("Expected expression inside PROJECT/FLATTEN_EXTRACT");
  }
  size_t start = current_.pos;
  std::string ident = current_.text;
  std::string fn = to_upper(current_.text);
  advance();
  if (current_.type != TokenType::LParen) {
    expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::AliasRef;
    expr.alias_ref = ident;
    expr.span = Span{start, current_.pos};
    return true;
  }

  if (fn == "TEXT") {
    if (!consume(TokenType::LParen, "Expected ( after TEXT")) return false;
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected tag identifier inside TEXT()");
    }
    expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::Text;
    expr.tag = current_.text;
    advance();
    if (current_.type == TokenType::KeywordWhere) {
      advance();
      Expr where_expr;
      if (!parse_expr(where_expr)) return false;
      expr.where = where_expr;
    }
    if (!consume(TokenType::RParen, "Expected ) after TEXT expression")) return false;
    expr.span = Span{start, current_.pos};
    return true;
  }

  if (fn == "DIRECT_TEXT") {
    if (!consume(TokenType::LParen, "Expected ( after DIRECT_TEXT")) return false;
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected tag identifier inside DIRECT_TEXT()");
    }
    expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::FunctionCall;
    expr.function_name = "DIRECT_TEXT";
    Query::SelectItem::FlattenExtractExpr tag_expr;
    tag_expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::StringLiteral;
    tag_expr.string_value = to_lower(current_.text);
    expr.args.push_back(std::move(tag_expr));
    advance();
    if (current_.type == TokenType::KeywordWhere) {
      advance();
      Expr where_expr;
      if (!parse_expr(where_expr)) return false;
      expr.where = where_expr;
    }
    if (!consume(TokenType::RParen, "Expected ) after DIRECT_TEXT expression")) return false;
    expr.span = Span{start, current_.pos};
    return true;
  }

  if (fn == "ATTR") {
    if (!consume(TokenType::LParen, "Expected ( after ATTR")) return false;
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected tag identifier inside ATTR()");
    }
    expr.kind = Query::SelectItem::FlattenExtractExpr::Kind::Attr;
    expr.tag = current_.text;
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
      expr.where = where_expr;
    }
    if (!consume(TokenType::RParen, "Expected ) after ATTR expression")) return false;
    expr.span = Span{start, current_.pos};
    return true;
  }

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
        if (current_.type != TokenType::Number) {
          return set_error("Expected numeric depth in inner_html()/raw_inner_html()");
        }
        try {
          item.inner_html_depth = static_cast<size_t>(std::stoull(current_.text));
        } catch (...) {
          return set_error("Invalid inner_html()/raw_inner_html() depth");
        }
        advance();
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
    Query::SelectItem item;
    item.field = "text";
    item.text_function = true;
    advance();
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected tag identifier inside text()");
    }
    item.tag = current_.text;
    advance();
    if (!consume(TokenType::RParen, "Expected ) after text argument")) return false;
    item.span = Span{start, current_.pos};
    items.push_back(item);
    saw_field = true;
    return true;
  }
  std::string fn_name = to_upper(tag_token.text);
  if ((fn_name == "INNER_HTML" || fn_name == "RAW_INNER_HTML") && current_.type == TokenType::LParen) {
    Query::SelectItem item;
    item.field = "inner_html";
    item.inner_html_function = true;
    item.raw_inner_html_function = (fn_name == "RAW_INNER_HTML");
    advance();
    if (current_.type != TokenType::Identifier && current_.type != TokenType::KeywordTable) {
      return set_error("Expected tag identifier inside inner_html()/raw_inner_html()");
    }
    item.tag = current_.text;
    advance();
    if (current_.type == TokenType::Comma) {
      advance();
      if (current_.type != TokenType::Number) {
        return set_error("Expected numeric depth in inner_html()/raw_inner_html()");
      }
      try {
        item.inner_html_depth = static_cast<size_t>(std::stoull(current_.text));
      } catch (...) {
        return set_error("Invalid inner_html()/raw_inner_html() depth");
      }
      advance();
    }
    if (!consume(TokenType::RParen, "Expected ) after inner_html/raw_inner_html argument")) return false;
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
    item.field = current_.text;
    item.span = Span{start, current_.pos + current_.text.size()};
    advance();
    saw_field = true;
  } else {
    item.span = Span{start, current_.pos};
    saw_tag_only = true;
  }
  items.push_back(item);
  return true;
}

}  // namespace xsql
