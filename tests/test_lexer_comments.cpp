#include "test_harness.h"
#include "test_utils.h"

#include <vector>

#include "parser/lexer.h"
#include "query_parser.h"

namespace {

std::vector<xsql::TokenType> lex_types(const std::string& input) {
  xsql::Lexer lexer(input);
  std::vector<xsql::TokenType> out;
  while (true) {
    xsql::Token token = lexer.next();
    out.push_back(token.type);
    if (token.type == xsql::TokenType::End || token.type == xsql::TokenType::Invalid) {
      break;
    }
  }
  return out;
}

void test_line_comment_before_tokens() {
  auto types = lex_types("-- comment\nSELECT div FROM document");
  expect_true(types.size() >= 5, "line comment before tokens keeps query tokens");
  if (types.size() >= 4) {
    expect_true(types[0] == xsql::TokenType::KeywordSelect, "first token is SELECT");
    expect_true(types[1] == xsql::TokenType::Identifier, "second token is identifier");
    expect_true(types[2] == xsql::TokenType::KeywordFrom, "third token is FROM");
    expect_true(types[3] == xsql::TokenType::KeywordDocument, "fourth token is DOCUMENT");
  }
}

void test_line_comment_between_tokens() {
  auto types = lex_types("SELECT -- keep\n div FROM document");
  expect_true(types.size() >= 5, "line comment between tokens keeps sequence");
  if (types.size() >= 4) {
    expect_true(types[0] == xsql::TokenType::KeywordSelect, "token 1 is SELECT");
    expect_true(types[1] == xsql::TokenType::Identifier, "token 2 is identifier");
    expect_true(types[2] == xsql::TokenType::KeywordFrom, "token 3 is FROM");
    expect_true(types[3] == xsql::TokenType::KeywordDocument, "token 4 is DOCUMENT");
  }
}

void test_line_comment_at_eof() {
  auto types = lex_types("SELECT div -- trailing");
  expect_true(!types.empty(), "lexer emits tokens with eof line comment");
  if (types.size() >= 2) {
    expect_true(types[0] == xsql::TokenType::KeywordSelect, "token 1 is SELECT");
    expect_true(types[1] == xsql::TokenType::Identifier, "token 2 is identifier");
  }
  expect_true(types.back() == xsql::TokenType::End, "line comment at EOF ends cleanly");
}

void test_block_comment_single_line() {
  auto types = lex_types("/* c */ SELECT div FROM document");
  expect_true(types.size() >= 5, "block comment before statement works");
  if (types.size() >= 4) {
    expect_true(types[0] == xsql::TokenType::KeywordSelect, "token 1 is SELECT");
    expect_true(types[1] == xsql::TokenType::Identifier, "token 2 is identifier");
    expect_true(types[2] == xsql::TokenType::KeywordFrom, "token 3 is FROM");
    expect_true(types[3] == xsql::TokenType::KeywordDocument, "token 4 is DOCUMENT");
  }
}

void test_block_comment_multi_line() {
  auto parsed = xsql::parse_query("/* one\n two */ SELECT div FROM document");
  expect_true(parsed.query.has_value(), "multi-line block comment is skipped");
}

void test_unterminated_block_comment_error() {
  std::string query = "SELECT div FROM document /* missing";
  auto parsed = xsql::parse_query(query);
  expect_true(!parsed.query.has_value(), "unterminated block comment fails parse");
  expect_true(parsed.error.has_value(), "unterminated block comment returns parse error");
  if (parsed.error.has_value()) {
    expect_true(parsed.error->message == "Unterminated block comment",
                "unterminated block comment uses deterministic message");
    size_t marker = query.find("/*");
    expect_true(parsed.error->position == marker,
                "unterminated block comment position points to block start");
  }
}

void test_comment_markers_inside_string_literals() {
  auto parsed_dash = xsql::parse_query(
      "SELECT div FROM document WHERE text = 'a--b'");
  expect_true(parsed_dash.query.has_value(), "-- inside string literal is not comment");

  auto parsed_block = xsql::parse_query(
      "SELECT div FROM document WHERE text = '/*x*/'");
  expect_true(parsed_block.query.has_value(), "/* */ inside string literal is not comment");
}

void test_query_without_comments_unchanged_behavior() {
  std::string html = "<div id='x'></div><div id='y'></div>";
  auto baseline = run_query(html, "SELECT div FROM document WHERE attributes.id = 'x'");
  auto with_space = run_query(html, "SELECT div FROM document WHERE attributes.id = 'x' ");
  expect_eq(baseline.rows.size(), with_space.rows.size(),
            "query behavior unchanged without comments");
}

}  // namespace

void register_lexer_comment_tests(std::vector<TestCase>& tests) {
  tests.push_back({"line_comment_before_tokens", test_line_comment_before_tokens});
  tests.push_back({"line_comment_between_tokens", test_line_comment_between_tokens});
  tests.push_back({"line_comment_at_eof", test_line_comment_at_eof});
  tests.push_back({"block_comment_single_line", test_block_comment_single_line});
  tests.push_back({"block_comment_multi_line", test_block_comment_multi_line});
  tests.push_back({"unterminated_block_comment_error", test_unterminated_block_comment_error});
  tests.push_back({"comment_markers_inside_string_literals", test_comment_markers_inside_string_literals});
  tests.push_back({"query_without_comments_unchanged_behavior", test_query_without_comments_unchanged_behavior});
}
