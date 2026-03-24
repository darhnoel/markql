#include "diagnostics_internal.h"

#include <algorithm>

namespace markql::diagnostics_internal {

DiagnosticSpan span_from_bytes(const std::string& query, size_t byte_start, size_t byte_end) {
  DiagnosticSpan span;
  const size_t size = query.size();
  span.byte_start = std::min(byte_start, size);
  span.byte_end = std::min(std::max(byte_end, span.byte_start + (size == 0 ? 0u : 1u)), size);
  if (size == 0) {
    span.start_line = 1;
    span.start_col = 1;
    span.end_line = 1;
    span.end_col = 1;
    span.byte_start = 0;
    span.byte_end = 0;
    return span;
  }
  if (span.byte_start >= size) {
    span.byte_start = size - 1;
  }
  if (span.byte_end <= span.byte_start) {
    span.byte_end = std::min(size, span.byte_start + 1);
  }

  size_t line = 1;
  size_t col = 1;
  for (size_t i = 0; i < span.byte_start && i < size; ++i) {
    if (query[i] == '\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
  }
  span.start_line = line;
  span.start_col = col;

  for (size_t i = span.byte_start; i < span.byte_end && i < size; ++i) {
    if (query[i] == '\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
  }
  span.end_line = line;
  span.end_col = col;
  return span;
}

std::optional<DiagnosticSpan> find_keyword_span(const std::string& query,
                                                const std::string& keyword) {
  size_t pos = find_icase(query, keyword);
  if (pos == std::string::npos) return std::nullopt;
  return span_from_bytes(query, pos, pos + keyword.size());
}

std::optional<DiagnosticSpan> find_identifier_span(const std::string& query,
                                                   const std::string& identifier) {
  if (identifier.empty()) return std::nullopt;
  const std::string lower_query = to_lower_ascii(query);
  const std::string lower_ident = to_lower_ascii(identifier);
  size_t pos = 0;
  while (true) {
    pos = lower_query.find(lower_ident, pos);
    if (pos == std::string::npos) return std::nullopt;
    const bool left_ok = (pos == 0) || !is_ident_char(query[pos - 1]);
    const size_t right = pos + lower_ident.size();
    const bool right_ok = right >= query.size() || !is_ident_char(query[right]);
    if (left_ok && right_ok) {
      return span_from_bytes(query, pos, right);
    }
    ++pos;
  }
}

DiagnosticSpan best_effort_semantic_span(const std::string& query, const std::string& message) {
  if (contains_icase(message, "ORDER BY")) {
    if (auto span = find_keyword_span(query, "ORDER BY"); span.has_value()) return *span;
  }
  if (contains_icase(message, "TO LIST")) {
    if (auto span = find_keyword_span(query, "TO LIST"); span.has_value()) return *span;
  }
  if (contains_icase(message, "TO TABLE")) {
    if (auto span = find_keyword_span(query, "TO TABLE"); span.has_value()) return *span;
  }
  if (contains_icase(message, "export")) {
    if (auto span = find_keyword_span(query, "TO CSV"); span.has_value()) return *span;
    if (auto span = find_keyword_span(query, "TO PARQUET"); span.has_value()) return *span;
    if (auto span = find_keyword_span(query, "TO JSON"); span.has_value()) return *span;
    if (auto span = find_keyword_span(query, "TO NDJSON"); span.has_value()) return *span;
  }
  if (contains_icase(message, "Duplicate source alias") ||
      contains_icase(message, "Identifier 'doc' is not bound") ||
      contains_icase(message, "Unknown identifier")) {
    if (auto token = extract_single_quoted(message); token.has_value()) {
      if (auto span = find_identifier_span(query, *token); span.has_value()) return *span;
    }
    if (auto span = find_keyword_span(query, "FROM"); span.has_value()) return *span;
  }
  if (contains_icase(message, "CTE")) {
    if (auto span = find_keyword_span(query, "WITH"); span.has_value()) return *span;
  }
  if (contains_icase(message, "JOIN")) {
    if (auto span = find_keyword_span(query, "JOIN"); span.has_value()) return *span;
  }
  if (contains_icase(message, "TEXT()") || contains_icase(message, "INNER_HTML()") ||
      contains_icase(message, "RAW_INNER_HTML()")) {
    if (auto span = find_keyword_span(query, "SELECT"); span.has_value()) return *span;
  }
  if (contains_icase(message, "LIMIT")) {
    if (auto span = find_keyword_span(query, "LIMIT"); span.has_value()) return *span;
  }
  if (contains_icase(message, "EXCLUDE")) {
    if (auto span = find_keyword_span(query, "EXCLUDE"); span.has_value()) return *span;
  }
  if (contains_icase(message, "PROJECT()") || contains_icase(message, "FLATTEN_EXTRACT()")) {
    if (auto span = find_keyword_span(query, "PROJECT"); span.has_value()) return *span;
    if (auto span = find_keyword_span(query, "FLATTEN_EXTRACT"); span.has_value()) return *span;
  }
  if (contains_icase(message, "FLATTEN_TEXT()")) {
    if (auto span = find_keyword_span(query, "FLATTEN_TEXT"); span.has_value()) return *span;
    if (auto span = find_keyword_span(query, "FLATTEN"); span.has_value()) return *span;
  }
  if (contains_icase(message, "Expected source alias") ||
      contains_icase(message, "requires an alias")) {
    if (auto span = find_keyword_span(query, "FROM"); span.has_value()) return *span;
  }
  if (contains_icase(message, "WHERE")) {
    if (auto span = find_keyword_span(query, "WHERE"); span.has_value()) return *span;
  }
  if (query.empty()) return span_from_bytes(query, 0, 0);
  return span_from_bytes(query, 0, 1);
}

void set_syntax_details(Diagnostic& d, const std::string& query, const std::string& parser_message,
                        size_t error_byte) {
  const TokenSlice encountered = token_at_or_after(query, error_byte);
  const std::string upper = to_upper_ascii(parser_message);
  const std::string encountered_upper = normalize_token_name(encountered.text);
  const ParseExpectation expectation = classify_parse_expectation(parser_message);
  d.category = "parse";
  d.code = "MQL-SYN-0001";
  d.message = parser_message;
  d.why = "MarkQL could not parse the query at this token.";
  d.help =
      "Review the local syntax near the highlighted token and compare it with the surrounding "
      "MarkQL construct.";
  d.example.clear();
  d.expected.clear();
  d.encountered = encountered_upper;
  d.doc_ref = kGrammarDoc;

  if (upper.find("EXPECTED TAG IDENTIFIER") != std::string::npos && encountered_upper == "FROM") {
    d.message = "Missing projection after SELECT";
    d.why = "SELECT must contain at least one projection before FROM in MarkQL.";
    d.help = "Add a tag, self, *, or a projection expression after SELECT.";
    d.example = "SELECT div FROM doc";
    d.expected = "projection after SELECT";
    return;
  }

  if (upper.find("EXPECTED FROM") != std::string::npos &&
      (encountered_upper == "WHERE" || encountered_upper == "ORDER" ||
       encountered_upper == "LIMIT" || encountered_upper == "TO" || encountered_upper == "JOIN" ||
       encountered_upper == "EXCLUDE")) {
    d.code = "MQL-SYN-0005";
    d.message = "Invalid clause order before FROM";
    d.why =
        "MarkQL clause order is WITH ... SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ... TO "
        "...";
    d.help = "Move the FROM clause so it appears before later clauses.";
    d.example = "SELECT div FROM doc WHERE tag = 'div'";
    d.expected = "FROM";
    return;
  }

  if (upper.find("CASE EXPRESSION REQUIRES AT LEAST ONE WHEN") != std::string::npos) {
    d.message = "CASE expression requires WHEN ... THEN";
    d.why = "A CASE expression in MarkQL must contain at least one WHEN ... THEN branch.";
    d.help = "Add at least one WHEN ... THEN branch before END.";
    d.example = "SELECT CASE WHEN tag = 'div' THEN 'yes' ELSE 'no' END AS flag FROM doc";
    return;
  }

  if (upper.find("EXISTS") != std::string::npos &&
      upper.find("EXPECTED ) AFTER EXISTS") != std::string::npos) {
    d.message = "Malformed EXISTS(...) predicate";
    d.why = "EXISTS() in MarkQL requires an axis name and may include an inner WHERE clause.";
    d.help = "Use EXISTS(child) or EXISTS(child WHERE tag = 'a').";
    d.example = "SELECT div FROM doc WHERE EXISTS(child WHERE tag = 'a')";
    return;
  }

  if (upper.find("EXPECTED CTE NAME AFTER WITH") != std::string::npos ||
      upper.find("EXPECTED AS AFTER CTE NAME") != std::string::npos ||
      upper.find("EXPECTED ( AFTER CTE AS") != std::string::npos ||
      upper.find("EXPECTED ) AFTER CTE SUBQUERY") != std::string::npos) {
    d.message = "Malformed WITH clause";
    d.why = "Each CTE must follow WITH name AS (subquery).";
    d.help = "Use WITH rows AS (SELECT ...) SELECT ... FROM rows";
    d.example = "WITH rows AS (SELECT div FROM doc) SELECT * FROM rows";
    return;
  }

  if (upper.find("PROJECT/FLATTEN_EXTRACT") != std::string::npos ||
      upper.find("PROJECT()") != std::string::npos ||
      upper.find("FLATTEN_EXTRACT()") != std::string::npos) {
    d.message = "Invalid PROJECT()/FLATTEN_EXTRACT() usage";
    d.why =
        "PROJECT()/FLATTEN_EXTRACT() requires a base tag and AS (alias: expression, ...) mapping.";
    d.help = "Use PROJECT(li) AS (title: TEXT(h2))";
    d.example = "SELECT PROJECT(li) AS (title: TEXT(h2)) FROM doc WHERE tag = 'li'";
    return;
  }

  if (upper.find("EXPECTED TAG IDENTIFIER INSIDE TEXT()") != std::string::npos ||
      upper.find("EXPECTED TAG IDENTIFIER INSIDE ATTR()") != std::string::npos ||
      upper.find("EXPECTED TAG IDENTIFIER OR SELF INSIDE EXTRACTION FUNCTION") !=
          std::string::npos ||
      upper.find("EXPECTED TAG IDENTIFIER OR SELF INSIDE ATTR()") != std::string::npos) {
    d.message = "This function requires a node/tag reference";
    d.why = "MarkQL extraction functions operate on a tag or self, not on a scalar literal.";
    d.help = "Pass a tag name, a bound alias, or self to the function.";
    d.example = "SELECT TEXT(self) FROM doc WHERE self.tag = 'div'";
    d.expected = "tag name, alias, or self";
    return;
  }

  if (apply_family_based_syntax_details(d, query, parser_message, error_byte, encountered,
                                        expectation)) {
    return;
  }

  if (upper.find("UNEXPECTED TOKEN AFTER QUERY") != std::string::npos) {
    d.code = "MQL-SYN-0002";
    d.message = "Unexpected trailing input after the query";
    d.why = "MarkQL finished parsing one query, but more tokens remained.";
    d.help =
        "Remove trailing tokens after the query terminates, or split multiple statements with ';'.";
    d.example = "SELECT div FROM doc;";
    return;
  }
  if (upper.find("EXPECTED ) AFTER TEXT ARGUMENT") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "TEXT(...) must close its argument list before the query continues.";
    d.help = "Close TEXT(...) with ')' before continuing.";
    d.example = "SELECT TEXT(div) FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ) AFTER INNER_HTML/RAW_INNER_HTML ARGUMENT") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why =
        "INNER_HTML(...)/RAW_INNER_HTML(...) must close its argument list before the query "
        "continues.";
    d.help = "Close INNER_HTML(...)/RAW_INNER_HTML(...) with ')'.";
    d.example = "SELECT INNER_HTML(div) FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ) AFTER ATTR ARGUMENTS") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "ATTR(tag, attr) must close after the attribute argument.";
    d.help = "Close ATTR(...) with ')' after the attribute name.";
    d.example = "SELECT ATTR(a, href) FROM doc WHERE tag = 'a'";
    return;
  }
  if (upper.find("EXPECTED ) AFTER POSITION ARGUMENTS") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "POSITION(substr IN str) must close after the string expression.";
    d.help = "Close POSITION(...) with ')' after the second argument.";
    d.example = "SELECT div FROM doc WHERE POSITION('a' IN text) > 0";
    return;
  }
  if (upper.find("EXPECTED ) AFTER FUNCTION ARGUMENTS") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "This function call must close its argument list before the query continues.";
    d.help = "Close the function call with ')' after the last argument.";
    d.example = "SELECT LOWER(text) AS lowered FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ) AFTER TRIM ARGUMENT") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "TRIM(...) must close its argument list before aliasing or continuing the query.";
    d.help = "Close TRIM(...) with ')' after the argument.";
    d.example = "SELECT TRIM(text) AS cleaned FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ) AFTER COUNT ARGUMENT") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "COUNT(...) must close after its tag or '*' argument.";
    d.help = "Close COUNT(...) with ')' after the argument.";
    d.example = "SELECT COUNT(*) FROM doc";
    return;
  }
  if (upper.find("EXPECTED ) AFTER TFIDF ARGUMENTS") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "TFIDF(...) must close after its tag list and options.";
    d.help = "Close TFIDF(...) with ')' after the final tag or option.";
    d.example = "SELECT TFIDF(div, TOP_TERMS=10) FROM doc";
    return;
  }
  if (upper.find("EXPECTED )") != std::string::npos) {
    d.code = "MQL-SYN-0003";
    d.expected = ")";
    d.why = "A clause or function call was opened earlier and was not closed here.";
    d.help = "Close the open parenthesis before continuing.";
    d.example = "SELECT ATTR(self, id) FROM doc WHERE self.tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ( AFTER COUNT") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "COUNT in MarkQL is written as COUNT(...).";
    d.help = "Add '(' after COUNT.";
    d.example = "SELECT COUNT(*) FROM doc";
    return;
  }
  if (upper.find("EXPECTED ( AFTER TRIM") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "TRIM in MarkQL is written as TRIM(...).";
    d.help = "Add '(' after TRIM.";
    d.example = "SELECT TRIM(text) FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ( AFTER TEXT") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "TEXT in MarkQL is written as TEXT(tag|self).";
    d.help = "Add '(' after TEXT.";
    d.example = "SELECT TEXT(div) FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ( AFTER INNER_HTML/RAW_INNER_HTML") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "INNER_HTML and RAW_INNER_HTML in MarkQL are written with parentheses.";
    d.help = "Add '(' after INNER_HTML or RAW_INNER_HTML.";
    d.example = "SELECT INNER_HTML(div) FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ( AFTER COALESCE") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "COALESCE in MarkQL is written as COALESCE(a, b, ...).";
    d.help = "Add '(' after COALESCE.";
    d.example = "SELECT COALESCE(TEXT(span), 'n/a') AS value FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED ( AFTER TFIDF") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "TFIDF in MarkQL is written as TFIDF(tag, ...).";
    d.help = "Add '(' after TFIDF.";
    d.example = "SELECT TFIDF(div) FROM doc";
    return;
  }
  if (upper.find("EXPECTED ( AFTER FUNCTION NAME") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "Scalar functions in MarkQL use parentheses for their argument list.";
    d.help = "Add '(' after the function name.";
    d.example = "SELECT LOWER(text) AS lowered FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED (") != std::string::npos) {
    d.code = "MQL-SYN-0004";
    d.expected = "(";
    d.why = "This MarkQL construct requires a parenthesized argument list or subquery.";
    d.help = "Add the missing '(' for the function or clause.";
    d.example = "SELECT TEXT(self) FROM doc WHERE self.tag = 'div'";
    return;
  }
  if (upper.find("EXPECTED SELECT") != std::string::npos ||
      upper.find("EXPECTED FROM") != std::string::npos ||
      upper.find("EXPECTED WHERE") != std::string::npos) {
    d.code = "MQL-SYN-0005";
    d.expected = upper.find("EXPECTED SELECT") != std::string::npos ? "SELECT"
                 : upper.find("EXPECTED FROM") != std::string::npos ? "FROM"
                                                                    : "WHERE";
    d.why = "The query does not follow MarkQL's clause order.";
    d.help = "Use canonical SQL order: WITH ... SELECT ... FROM ... WHERE ...";
    d.example = "SELECT div FROM doc WHERE tag = 'div'";
    return;
  }
  if (upper.find("JOIN REQUIRES ON") != std::string::npos) {
    d.code = "MQL-SYN-0006";
    d.why = "JOIN and LEFT JOIN need an ON predicate in MarkQL relation queries.";
    d.help = "Add ON <condition> after JOIN or use CROSS JOIN without ON.";
    d.example = "SELECT * FROM a JOIN b ON a.id = b.id";
    return;
  }
  if (upper.find("CROSS JOIN DOES NOT ALLOW ON") != std::string::npos) {
    d.code = "MQL-SYN-0007";
    d.why = "CROSS JOIN in MarkQL is unconditional and does not accept an ON clause.";
    d.help = "Remove ON from CROSS JOIN, or change CROSS JOIN to JOIN/LEFT JOIN.";
    d.example = "SELECT * FROM a CROSS JOIN b";
    return;
  }
  if (upper.find("LATERAL SUBQUERY REQUIRES AN ALIAS") != std::string::npos) {
    d.code = "MQL-SYN-0008";
    d.why = "A LATERAL subquery becomes a source in FROM/JOIN and therefore needs an alias.";
    d.help = "Add AS <alias> after the LATERAL subquery.";
    d.example = "SELECT * FROM a CROSS JOIN LATERAL (SELECT ...) AS x";
    return;
  }

  if (upper.find("EXPECTED END TO CLOSE CASE EXPRESSION") != std::string::npos) {
    d.why = "CASE expressions in MarkQL must end with END.";
    d.expected = "END";
    set_typo_aware_help(d, encountered.text, {"END"}, "Close the CASE expression with END.",
                        "SELECT CASE WHEN tag = 'div' THEN 'yes' ELSE 'no' END FROM doc");
    d.example = "SELECT CASE WHEN tag = 'div' THEN 'yes' ELSE 'no' END FROM doc";
    return;
  }

  d.help =
      "Check the syntax near the highlighted token. If this is a full query, the canonical order "
      "is WITH ... SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ... TO ...";
  d.example = "SELECT div FROM doc";
}

void set_semantic_details(Diagnostic& d) {
  d.category = "semantic";
  d.code = "MQL-SEM-0999";
  d.why = "The query parsed, but it violates a MarkQL validation rule.";
  d.help = "Review the failing clause and adjust query shape to match MarkQL validation rules.";
  d.example.clear();
  d.expected.clear();
  d.encountered.clear();
  d.doc_ref = kCliDoc;
  const std::string m = d.message;
  if (contains_icase(m, "Duplicate source alias")) {
    d.category = "binding";
    d.code = "MQL-SEM-0101";
    d.why = "Each FROM/JOIN source alias must be unique within the same query scope.";
    d.help = "Use unique aliases for each FROM/JOIN source in the same scope.";
    d.example = "SELECT n.node_id FROM doc AS n JOIN doc AS m ON n.node_id = m.node_id";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Duplicate CTE name")) {
    d.category = "binding";
    d.code = "MQL-SEM-0102";
    d.why = "Each WITH binding name must be unique within the same WITH clause.";
    d.help = "Rename one CTE so each WITH binding name is unique.";
    d.example =
        "WITH left_rows AS (SELECT * FROM doc), right_rows AS (SELECT * FROM doc) SELECT * FROM "
        "left_rows";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Unknown identifier")) {
    d.category = "binding";
    d.code = "MQL-SEM-0103";
    d.why =
        "Identifiers must resolve to a bound FROM/JOIN alias or a supported legacy tag binding.";
    d.help = "Reference a bound FROM alias (or legacy tag binding) and check spelling.";
    d.example = "SELECT n.node_id FROM doc AS n WHERE n.tag = 'div'";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Identifier 'doc' is not bound")) {
    d.category = "binding";
    d.code = "MQL-SEM-0104";
    d.why =
        "Once FROM doc AS <alias> is used, that alias becomes the bound name for the row source.";
    d.help = "When FROM doc AS <alias> is used, reference only that alias (not doc.*).";
    d.example = "SELECT n.node_id FROM doc AS n WHERE n.tag = 'div'";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Derived table requires an alias")) {
    d.category = "binding";
    d.code = "MQL-SEM-0105";
    d.why = "A derived subquery in FROM/JOIN must be named so later clauses can reference it.";
    d.help = "Add AS <alias> after the derived subquery source.";
    d.example = "SELECT rows.node_id FROM (SELECT self.node_id FROM doc) AS rows";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "TO LIST()")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0201";
    d.why =
        "TO LIST() emits one list value per row, so the SELECT shape must contain exactly one "
        "projected column.";
    d.help = "TO LIST() requires exactly one projected column.";
    d.example = "SELECT a.href FROM doc WHERE href IS NOT NULL TO LIST()";
    d.doc_ref = kCliDoc;
    return;
  }
  if (contains_icase(m, "TO TABLE()")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0202";
    d.why =
        "TO TABLE() is only defined for table-node extraction, not for arbitrary projections or "
        "relation queries.";
    d.help = "Use TO TABLE() only with SELECT table tag-only queries.";
    d.example = "SELECT table FROM doc TO TABLE()";
    d.doc_ref = kCliDoc;
    return;
  }
  if (contains_icase(m, "Export")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0203";
    d.why = "Export targets require a valid MarkQL TO <format>(...) sink shape.";
    d.help = "Check export sink syntax and ensure required path arguments are present.";
    d.example = "SELECT a.href FROM doc TO CSV('links.csv')";
    d.doc_ref = kCliDoc;
    return;
  }
  if (contains_icase(m, "PROJECT()/FLATTEN_EXTRACT()") ||
      contains_icase(m, "FLATTEN_TEXT()/PROJECT()/FLATTEN_EXTRACT()")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0204";
    d.why = "MarkQL restricts PROJECT()/FLATTEN_EXTRACT()/FLATTEN_TEXT() to specific query shapes.";
    d.help = "Use one extraction helper per query and provide the required AS mapping.";
    d.example = "SELECT PROJECT(li) AS (title: TEXT(h2)) FROM doc WHERE tag = 'li'";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "Cannot mix tag-only and projected fields in SELECT") ||
      contains_icase(m, "Projected fields must use a single tag") ||
      contains_icase(m, "EXCLUDE requires SELECT *")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0205";
    d.why = "MarkQL SELECT output shape must stay internally consistent within one query.";
    d.help = "Use one SELECT shape at a time: tag-only, wildcard, or projected fields.";
    d.example = "SELECT a.href FROM doc WHERE tag = 'a'";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "TEXT()/INNER_HTML()/RAW_INNER_HTML()")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0301";
    d.why =
        "These node extraction helpers require a WHERE clause that narrows rows with more than a "
        "plain tag check.";
    d.help =
        "Add a WHERE clause with a non-tag filter (attributes/parent/etc.) before projecting "
        "TEXT()/INNER_HTML().";
    d.example = "SELECT TEXT(self) FROM doc WHERE attributes.id IS NOT NULL";
    d.doc_ref = kFunctionsDoc;
    return;
  }
  if (contains_icase(m, "ORDER BY")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0401";
    d.why =
        "ORDER BY is only supported for specific result shapes and field sets in the current "
        "engine.";
    d.help = "ORDER BY supports a restricted field set; adjust ORDER BY fields or aggregate usage.";
    d.example = "SELECT self.node_id FROM doc WHERE self.tag = 'div' ORDER BY node_id DESC";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "LIMIT")) {
    d.category = "structural_rule";
    d.code = "MQL-SEM-0402";
    d.why = "LIMIT must fit the engine's supported numeric range and query shape.";
    d.help = "Reduce LIMIT to a supported value.";
    d.example = "SELECT div FROM doc LIMIT 10";
    d.doc_ref = kGrammarDoc;
    return;
  }
  if (contains_icase(m, "PARSE()") || contains_icase(m, "FRAGMENTS()") ||
      contains_icase(m, "RAW()")) {
    d.category = "semantic";
    d.code = "MQL-SEM-0501";
    d.why = "Source constructors accept only supported argument shapes and valid text inputs.";
    d.help = "Ensure source constructors receive valid HTML strings or supported subqueries.";
    d.example = "SELECT self FROM PARSE('<div>ok</div>') AS n";
    d.doc_ref = kSourcesDoc;
    return;
  }
}

bool looks_like_runtime_io(std::string_view message) {
  return contains_icase(message, "Failed to open file") ||
         contains_icase(message, "Failed to fetch URL") ||
         contains_icase(message, "URL fetching is disabled") ||
         contains_icase(message, "Unsupported Content-Type");
}

}  // namespace markql::diagnostics_internal
