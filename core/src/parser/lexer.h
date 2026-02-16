#pragma once

#include <string>

#include "tokens.h"

namespace xsql {

/// Tokenizes query input into a stream for the parser.
/// MUST be deterministic and MUST not skip meaningful characters.
/// Inputs are query strings; outputs are tokens with position metadata.
class Lexer {
 public:
  /// Constructs a lexer over a stable input string reference.
  /// MUST NOT outlive the referenced input buffer.
  /// Inputs are the query string; side effects are none.
  explicit Lexer(const std::string& input);
  /// Produces the next token from the input stream.
  /// MUST advance the cursor and MUST return End at input exhaustion.
  /// Inputs are internal state; outputs are tokens with positions.
  Token next();

 private:
  /// Lexes a quoted string token, handling unterminated input.
  /// MUST capture raw contents and MUST stop at the matching quote.
  /// Inputs are internal state; outputs are string tokens.
  Token lex_string();
  /// Lexes identifiers and recognizes keyword forms.
  /// MUST map keywords case-insensitively and preserve original text.
  /// Inputs are internal state; outputs are identifier/keyword tokens.
  Token lex_identifier_or_keyword();
  /// Lexes an integer literal token.
  /// MUST stop at the first non-digit to avoid consuming delimiters.
  /// Inputs are internal state; outputs are number tokens.
  Token lex_number();
  /// Skips whitespace and SQL comments between tokens.
  /// MUST treat comments only in normal lexing mode (never inside strings).
  /// Inputs are internal state; outputs are updated cursor positions.
  void skip_ws_and_comments();
  /// Returns and consumes one character while updating line/column counters.
  /// MUST advance byte position by one and track newlines accurately.
  char advance_char();
  /// Returns true when a lexical error was recorded.
  /// MUST remain sticky after the first lexical error.
  bool has_error() const;
  /// Creates a token with current location metadata.
  Token make_token(TokenType type, const std::string& text, size_t start_pos) const;
  /// Records the first lexical error for deferred reporting through tokens.
  void set_error(const std::string& message, size_t position);
  /// Tests whether a character can start an identifier.
  /// MUST align with parser expectations for identifiers.
  /// Inputs are characters; outputs are booleans with no side effects.
  static bool is_ident_start(char c);
  /// Tests whether a character can appear in an identifier.
  /// MUST align with parser expectations for identifiers.
  /// Inputs are characters; outputs are booleans with no side effects.
  static bool is_ident_char(char c);
  /// Converts a token to uppercase for keyword matching.
  /// MUST avoid locale-sensitive behavior to keep parsing deterministic.
  /// Inputs are strings; outputs are uppercase strings.
  static std::string to_upper(const std::string& s);

  const std::string& input_;
  size_t pos_ = 0;
  size_t line_ = 1;
  size_t col_ = 1;
  bool has_error_ = false;
  std::string error_message_;
  size_t error_position_ = 0;
};

}  // namespace xsql
