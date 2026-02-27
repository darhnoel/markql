#include "lexer.h"

#include <cctype>

namespace xsql {

Lexer::Lexer(const std::string& input) : input_(input) {}

Token Lexer::next() {
  skip_ws_and_comments();
  if (has_error()) {
    return make_token(TokenType::Invalid, error_message_, error_position_);
  }
  if (pos_ >= input_.size()) {
    return make_token(TokenType::End, "", pos_);
  }

  size_t start = pos_;
  char c = input_[pos_];
  if (c == ',') {
    advance_char();
    return make_token(TokenType::Comma, ",", start);
  }
  if (c == ':') {
    advance_char();
    return make_token(TokenType::Colon, ":", start);
  }
  if (c == '.') {
    advance_char();
    return make_token(TokenType::Dot, ".", start);
  }
  if (c == '(') {
    advance_char();
    return make_token(TokenType::LParen, "(", start);
  }
  if (c == ')') {
    advance_char();
    return make_token(TokenType::RParen, ")", start);
  }
  if (c == ';') {
    advance_char();
    return make_token(TokenType::Semicolon, ";", start);
  }
  if (c == '*') {
    advance_char();
    return make_token(TokenType::Star, "*", start);
  }
  if (c == '=') {
    advance_char();
    return make_token(TokenType::Equal, "=", start);
  }
  if (c == '!') {
    if (pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
      advance_char();
      advance_char();
      return make_token(TokenType::NotEqual, "!=", start);
    }
  }
  if (c == '>') {
    if (pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
      advance_char();
      advance_char();
      return make_token(TokenType::GreaterEqual, ">=", start);
    }
    advance_char();
    return make_token(TokenType::Greater, ">", start);
  }
  if (c == '<') {
    if (pos_ + 1 < input_.size() && input_[pos_ + 1] == '>') {
      advance_char();
      advance_char();
      return make_token(TokenType::NotEqual, "<>", start);
    }
    if (pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
      advance_char();
      advance_char();
      return make_token(TokenType::LessEqual, "<=", start);
    }
    advance_char();
    return make_token(TokenType::Less, "<", start);
  }
  if (c == '~') {
    advance_char();
    return make_token(TokenType::RegexMatch, "~", start);
  }
  if (c == '\'' || c == '\"') {
    return lex_string();
  }
  if (std::isdigit(static_cast<unsigned char>(c))) {
    return lex_number();
  }
  if (is_ident_start(c)) {
    return lex_identifier_or_keyword();
  }

  // WHY: advance on unknown input to avoid infinite loops on malformed queries.
  advance_char();
  return make_token(TokenType::End, "", start);
}

Token Lexer::lex_string() {
  size_t start = pos_;
  char quote = advance_char();
  std::string out;
  while (pos_ < input_.size()) {
    char c = advance_char();
    if (c == quote) {
      return make_token(TokenType::String, out, start);
    }
    out.push_back(c);
  }
  return make_token(TokenType::String, out, start);
}

Token Lexer::lex_identifier_or_keyword() {
  size_t start = pos_;
  std::string out;
  while (pos_ < input_.size() && is_ident_char(input_[pos_])) {
    out.push_back(advance_char());
  }
  std::string upper = to_upper(out);
  if (upper == "SELECT") return make_token(TokenType::KeywordSelect, out, start);
  if (upper == "WITH") return make_token(TokenType::KeywordWith, out, start);
  if (upper == "FROM") return make_token(TokenType::KeywordFrom, out, start);
  if (upper == "JOIN") return make_token(TokenType::KeywordJoin, out, start);
  if (upper == "LEFT") return make_token(TokenType::KeywordLeft, out, start);
  if (upper == "INNER") return make_token(TokenType::KeywordInner, out, start);
  if (upper == "CROSS") return make_token(TokenType::KeywordCross, out, start);
  if (upper == "LATERAL") return make_token(TokenType::KeywordLateral, out, start);
  if (upper == "ON") return make_token(TokenType::KeywordOn, out, start);
  if (upper == "WHERE") return make_token(TokenType::KeywordWhere, out, start);
  if (upper == "AND") return make_token(TokenType::KeywordAnd, out, start);
  if (upper == "OR") return make_token(TokenType::KeywordOr, out, start);
  if (upper == "IN") return make_token(TokenType::KeywordIn, out, start);
  if (upper == "EXISTS") return make_token(TokenType::KeywordExists, out, start);
  if (upper == "DOCUMENT") return make_token(TokenType::KeywordDocument, out, start);
  if (upper == "LIMIT") return make_token(TokenType::KeywordLimit, out, start);
  if (upper == "EXCLUDE") return make_token(TokenType::KeywordExclude, out, start);
  if (upper == "ORDER") return make_token(TokenType::KeywordOrder, out, start);
  if (upper == "BY") return make_token(TokenType::KeywordBy, out, start);
  if (upper == "ASC") return make_token(TokenType::KeywordAsc, out, start);
  if (upper == "DESC") return make_token(TokenType::KeywordDesc, out, start);
  if (upper == "AS") return make_token(TokenType::KeywordAs, out, start);
  if (upper == "TO") return make_token(TokenType::KeywordTo, out, start);
  if (upper == "LIST") return make_token(TokenType::KeywordList, out, start);
  if (upper == "COUNT") return make_token(TokenType::KeywordCount, out, start);
  if (upper == "TABLE") return make_token(TokenType::KeywordTable, out, start);
  if (upper == "CSV") return make_token(TokenType::KeywordCsv, out, start);
  if (upper == "PARQUET") return make_token(TokenType::KeywordParquet, out, start);
  if (upper == "JSON") return make_token(TokenType::KeywordJson, out, start);
  if (upper == "NDJSON") return make_token(TokenType::KeywordNdjson, out, start);
  if (upper == "RAW") return make_token(TokenType::KeywordRaw, out, start);
  if (upper == "FRAGMENTS") return make_token(TokenType::KeywordFragments, out, start);
  if (upper == "PARSE") return make_token(TokenType::KeywordParse, out, start);
  if (upper == "CONTAINS") return make_token(TokenType::KeywordContains, out, start);
  if (upper == "HAS_DIRECT_TEXT") return make_token(TokenType::KeywordHasDirectText, out, start);
  if (upper == "LIKE") return make_token(TokenType::KeywordLike, out, start);
  if (upper == "ALL") return make_token(TokenType::KeywordAll, out, start);
  if (upper == "ANY") return make_token(TokenType::KeywordAny, out, start);
  if (upper == "IS") return make_token(TokenType::KeywordIs, out, start);
  if (upper == "NOT") return make_token(TokenType::KeywordNot, out, start);
  if (upper == "NULL") return make_token(TokenType::KeywordNull, out, start);
  if (upper == "CASE") return make_token(TokenType::KeywordCase, out, start);
  if (upper == "WHEN") return make_token(TokenType::KeywordWhen, out, start);
  if (upper == "THEN") return make_token(TokenType::KeywordThen, out, start);
  if (upper == "ELSE") return make_token(TokenType::KeywordElse, out, start);
  if (upper == "END") return make_token(TokenType::KeywordEnd, out, start);
  if (upper == "SHOW") return make_token(TokenType::KeywordShow, out, start);
  if (upper == "DESCRIBE") return make_token(TokenType::KeywordDescribe, out, start);
  if (upper == "PROJECT") return make_token(TokenType::KeywordProject, out, start);
  if (upper == "INPUT") return make_token(TokenType::KeywordInput, out, start);
  if (upper == "INPUTS") return make_token(TokenType::KeywordInputs, out, start);
  if (upper == "FUNCTIONS") return make_token(TokenType::KeywordFunctions, out, start);
  if (upper == "AXES") return make_token(TokenType::KeywordAxes, out, start);
  if (upper == "OPERATORS") return make_token(TokenType::KeywordOperators, out, start);
  if (upper == "SELF") return make_token(TokenType::KeywordSelf, out, start);
  return make_token(TokenType::Identifier, out, start);
}

Token Lexer::lex_number() {
  size_t start = pos_;
  std::string out;
  while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
    out.push_back(advance_char());
  }
  return make_token(TokenType::Number, out, start);
}

void Lexer::skip_ws_and_comments() {
  while (true) {
    size_t before = pos_;
    while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
      advance_char();
    }
    if (pos_ + 1 < input_.size() && input_[pos_] == '-' && input_[pos_ + 1] == '-') {
      advance_char();
      advance_char();
      while (pos_ < input_.size() && input_[pos_] != '\n') {
        advance_char();
      }
      continue;
    }
    if (pos_ + 1 < input_.size() && input_[pos_] == '/' && input_[pos_ + 1] == '*') {
      size_t start = pos_;
      advance_char();
      advance_char();
      bool closed = false;
      while (pos_ < input_.size()) {
        if (pos_ + 1 < input_.size() && input_[pos_] == '*' && input_[pos_ + 1] == '/') {
          advance_char();
          advance_char();
          closed = true;
          break;
        }
        advance_char();
      }
      if (!closed) {
        set_error("Unterminated block comment", start);
        return;
      }
      continue;
    }
    if (pos_ == before) {
      break;
    }
  }
}

char Lexer::advance_char() {
  char c = input_[pos_++];
  if (c == '\n') {
    ++line_;
    col_ = 1;
  } else {
    ++col_;
  }
  return c;
}

bool Lexer::has_error() const { return has_error_; }

Token Lexer::make_token(TokenType type, const std::string& text, size_t start_pos) const {
  return Token{type, text, start_pos};
}

void Lexer::set_error(const std::string& message, size_t position) {
  if (has_error_) return;
  has_error_ = true;
  error_message_ = message;
  error_position_ = position;
}

bool Lexer::is_ident_start(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool Lexer::is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

std::string Lexer::to_upper(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

}  // namespace xsql
