#include "parser_internal.h"

namespace xsql {

/// Parses the FROM source, supporting document, path, or URL forms.
/// MUST normalize source kinds and capture spans for errors.
/// Inputs are tokens; outputs are Source or errors.
bool Parser::parse_source(Source& src) {
  if (current_.type == TokenType::LParen) {
    size_t start = current_.pos;
    advance();
    std::shared_ptr<Query> subquery;
    if (!parse_subquery(subquery)) return false;
    if (!consume(TokenType::RParen, "Expected ) after subquery source")) return false;
    src.kind = Source::Kind::DerivedSubquery;
    src.derived_query = std::move(subquery);
    src.span = Span{start, current_.pos};
    return parse_source_alias(src, true, "Derived table requires an alias");
  }
  if (current_.type == TokenType::KeywordDocument) {
    src.kind = Source::Kind::Document;
    src.value = "document";
    src.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    if (!parse_source_alias(src)) return false;
    if (!src.alias.has_value()) {
      // WHY: keep `doc.field` usable without forcing `AS doc` on the default source.
      src.alias = "doc";
    }
    return true;
  }
  if (current_.type == TokenType::KeywordRaw) {
    size_t start = current_.pos;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after RAW")) return false;
    if (current_.type != TokenType::String) {
      return set_error("Expected string literal inside RAW()");
    }
    src.kind = Source::Kind::RawHtml;
    src.value = current_.text;
    src.span = Span{start, current_.pos + current_.text.size()};
    advance();
    if (!consume(TokenType::RParen, "Expected ) after RAW argument")) return false;
    return parse_source_alias(src);
  }
  if (current_.type == TokenType::KeywordFragments) {
    size_t start = current_.pos;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after FRAGMENTS")) return false;
    src.kind = Source::Kind::Fragments;
    if (current_.type == TokenType::KeywordRaw) {
      size_t raw_start = current_.pos;
      advance();
      if (!consume(TokenType::LParen, "Expected ( after RAW")) return false;
      if (current_.type != TokenType::String) {
        return set_error("Expected string literal inside RAW()");
      }
      src.fragments_raw = current_.text;
      src.span = Span{raw_start, current_.pos + current_.text.size()};
      advance();
      if (!consume(TokenType::RParen, "Expected ) after RAW argument")) return false;
    } else {
      std::shared_ptr<Query> subquery;
      if (!parse_subquery(subquery)) return false;
      src.fragments_query = std::move(subquery);
    }
    if (!consume(TokenType::RParen, "Expected ) after FRAGMENTS argument")) return false;
    src.span = Span{start, current_.pos};
    return parse_source_alias(src);
  }
  if (current_.type == TokenType::KeywordParse) {
    size_t start = current_.pos;
    advance();
    if (!consume(TokenType::LParen, "Expected ( after PARSE")) return false;
    src.kind = Source::Kind::Parse;
    if (current_.type == TokenType::KeywordSelect ||
        current_.type == TokenType::KeywordWith) {
      std::shared_ptr<Query> subquery;
      if (!parse_subquery(subquery)) return false;
      src.parse_query = std::move(subquery);
    } else {
      auto expr = std::make_shared<ScalarExpr>();
      if (!parse_scalar_expr(*expr)) return false;
      src.parse_expr = std::move(expr);
    }
    if (!consume(TokenType::RParen, "Expected ) after PARSE argument")) return false;
    src.span = Span{start, current_.pos};
    return parse_source_alias(src);
  }
  if (current_.type == TokenType::String) {
    src.value = current_.text;
    src.span = Span{current_.pos, current_.pos + current_.text.size()};
    if (starts_with(src.value, "http://") || starts_with(src.value, "https://")) {
      src.kind = Source::Kind::Url;
    } else {
      src.kind = Source::Kind::Path;
    }
    advance();
    return parse_source_alias(src);
  }
  if (current_.type == TokenType::Identifier) {
    const std::string ident = to_lower(current_.text);
    if (cte_names_.find(ident) != cte_names_.end()) {
      src.kind = Source::Kind::CteRef;
      src.value = current_.text;
      src.alias = current_.text;
      src.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      if (!parse_source_alias(src)) return false;
      return true;
    }
    if (ident == "doc" || ident == "document") {
      src.kind = Source::Kind::Document;
      src.value = "document";
      src.span = Span{current_.pos, current_.pos + current_.text.size()};
      advance();
      if (!parse_source_alias(src)) return false;
      if (!src.alias.has_value()) {
        src.alias = "doc";
      }
      return true;
    }
    src.kind = Source::Kind::Document;
    src.value = "document";
    src.alias = current_.text;
    src.span = Span{current_.pos, current_.pos + current_.text.size()};
    advance();
    return true;
  }
  return set_error(
      "Expected document, CTE name, derived subquery, string literal, RAW(), FRAGMENTS(), or PARSE() source");
}

/// Parses a full query inside FRAGMENTS(), stopping before a closing ).
/// MUST return with current_ positioned at the closing parenthesis.
/// Inputs are token streams; outputs are Query pointers or errors.
bool Parser::parse_subquery(std::shared_ptr<Query>& out) {
  const auto outer_cte_names = cte_names_;
  auto q = std::make_shared<Query>();
  if (!parse_query_body(*q)) {
    cte_names_ = outer_cte_names;
    return false;
  }
  if (current_.type == TokenType::Semicolon) {
    advance();
  }
  if (current_.type != TokenType::RParen) {
    cte_names_ = outer_cte_names;
    return set_error("Expected ) after subquery");
  }
  out = std::move(q);
  cte_names_ = outer_cte_names;
  return true;
}

/// Parses an optional alias after a source.
/// MUST accept AS or bare identifiers for aliasing.
/// Inputs are tokens; outputs are updated Source or errors.
bool Parser::parse_source_alias(Source& src, bool require_alias, const char* required_msg) {
  if (current_.type == TokenType::KeywordAs) {
    advance();
    if (current_.type != TokenType::Identifier) {
      return set_error("Expected alias identifier after AS");
    }
    src.alias = current_.text;
    advance();
    return true;
  }
  if (current_.type == TokenType::Identifier) {
    src.alias = current_.text;
    advance();
    return true;
  }
  if (require_alias) {
    return set_error(required_msg != nullptr ? required_msg : "Expected source alias");
  }
  return true;
}

}  // namespace xsql
