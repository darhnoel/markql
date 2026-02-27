#include <exception>

#include "test_harness.h"
#include "test_utils.h"
#include "query_parser.h"

namespace {

void test_alias_qualifier() {
  std::string html = "<a id='root' href='x'></a>";
  auto result = run_query(html, "SELECT a FROM document AS doc WHERE doc.attributes.id = 'root'");
  expect_eq(result.rows.size(), 1, "alias qualifier");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "a", "alias qualifier tag");
    expect_true(result.rows[0].attributes["id"] == "root", "alias qualifier id");
    expect_true(result.rows[0].attributes["href"] == "x", "alias qualifier href");
  }
}

void test_alias_source_only() {
  std::string html = "<a id='root' href='x'></a>";
  auto result = run_query(html, "SELECT a FROM document AS doc WHERE attributes.id = 'root'");
  expect_eq(result.rows.size(), 1, "alias source only");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "a", "alias source only tag");
    expect_true(result.rows[0].attributes["id"] == "root", "alias source only id");
    expect_true(result.rows[0].attributes["href"] == "x", "alias source only href");
  }
}

void test_attr_shorthand_self_and_qualified_aliases() {
  std::string html =
      "<div id='root'><a id='x' href='k'></a></div>"
      "<div id='other'><a id='y'></a></div>";
  auto implicit = run_query(
      html,
      "SELECT a FROM doc WHERE attr.id = 'x'");
  expect_eq(implicit.rows.size(), 1, "attr shorthand self");
  if (!implicit.rows.empty()) {
    expect_true(implicit.rows[0].attributes["id"] == "x", "attr shorthand self value");
  }

  auto qualified = run_query(
      html,
      "SELECT a FROM doc AS n WHERE n.attr.id = 'x'");
  expect_eq(qualified.rows.size(), 1, "attr shorthand explicit alias");
  if (!qualified.rows.empty()) {
    expect_true(qualified.rows[0].attributes["id"] == "x", "attr shorthand alias value");
  }
}

void test_attr_shorthand_axis_paths() {
  std::string html = "<div id='root'><span id='child'></span></div><div id='skip'><span></span></div>";
  auto result = run_query(
      html,
      "SELECT span FROM doc WHERE parent.attr.id = 'root'");
  expect_eq(result.rows.size(), 1, "attr shorthand on axis");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].attributes["id"] == "child", "attr shorthand axis value");
  }
}

void test_parse_alias_field_with_implicit_doc() {
  auto parsed = xsql::parse_query(
      "SELECT doc.node_id, TEXT(doc) FROM doc WHERE doc.tag = 'div'");
  expect_true(parsed.query.has_value(), "parse implicit doc alias field references");
}

void test_parse_alias_field_with_explicit_alias() {
  auto parsed = xsql::parse_query(
      "SELECT n.node_id, TEXT(n) FROM doc AS n WHERE n.tag = 'div'");
  expect_true(parsed.query.has_value(), "parse explicit alias field references");
}

void test_parse_legacy_tag_binding_still_works() {
  auto parsed = xsql::parse_query(
      "SELECT li.node_id, TEXT(li) FROM doc WHERE tag = 'li'");
  expect_true(parsed.query.has_value(), "parse legacy tag binding");
}

void test_duplicate_source_alias_rejected() {
  auto parsed = xsql::parse_query(
      "SELECT n.node_id FROM doc AS n AS m WHERE n.tag = 'div'");
  expect_true(!parsed.query.has_value(), "duplicate alias should fail parse");
  expect_true(parsed.error.has_value(), "duplicate alias returns parse error");
  if (parsed.error.has_value()) {
    expect_true(parsed.error->message == "Duplicate source alias 'n' in FROM",
                "duplicate alias message");
  }
}

void test_implicit_doc_alias_matches_explicit_alias_values() {
  std::string html = "<div>One</div><div>Two</div><span>Skip</span>";
  auto implicit = run_query(
      html,
      "SELECT doc.node_id, TEXT(doc) FROM doc "
      "WHERE doc.tag = 'div' AND doc.parent_id IS NOT NULL ORDER BY node_id");
  auto explicit_alias = run_query(
      html,
      "SELECT n.node_id, TEXT(n) FROM doc AS n "
      "WHERE n.tag = 'div' AND n.parent_id IS NOT NULL ORDER BY node_id");
  expect_eq(implicit.rows.size(), 2, "implicit doc alias expected row count");
  expect_eq(explicit_alias.rows.size(), 2, "explicit alias expected row count");
  expect_eq(implicit.rows.size(), explicit_alias.rows.size(), "implicit/explicit alias row count");
  if (implicit.rows.size() != explicit_alias.rows.size()) return;
  for (size_t i = 0; i < implicit.rows.size(); ++i) {
    expect_true(implicit.rows[i].node_id == explicit_alias.rows[i].node_id,
                "implicit/explicit alias node_id equality");
    expect_true(implicit.rows[i].tag == explicit_alias.rows[i].tag,
                "implicit/explicit alias tag equality");
    expect_true(implicit.rows[i].text == explicit_alias.rows[i].text,
                "implicit/explicit alias text equality");
  }
  if (implicit.rows.size() >= 2) {
    expect_true(implicit.rows[0].text == "One", "implicit alias row1 value");
    expect_true(implicit.rows[1].text == "Two", "implicit alias row2 value");
  }
}

void test_doc_identifier_rejected_after_explicit_realias() {
  bool threw = false;
  try {
    std::string html = "<div>One</div>";
    run_query(html, "SELECT doc.node_id FROM doc AS n WHERE n.tag = 'div'");
  } catch (const std::exception& ex) {
    threw = true;
    expect_true(std::string(ex.what()) ==
                    "Identifier 'doc' is not bound; did you mean 'n'?",
                "doc rebinding error message");
  }
  expect_true(threw, "doc identifier must fail after explicit alias");
}

void test_unknown_identifier_error_mentions_alias_or_legacy_binding() {
  bool threw = false;
  try {
    std::string html = "<div>One</div>";
    run_query(html, "SELECT div FROM doc AS n WHERE x.tag = 'div'");
  } catch (const std::exception& ex) {
    threw = true;
    expect_true(std::string(ex.what()) ==
                    "Unknown identifier 'x' (expected a FROM alias or legacy tag binding)",
                "unknown identifier error message");
  }
  expect_true(threw, "unknown identifier should fail validation");
}

}  // namespace

void register_source_alias_tests(std::vector<TestCase>& tests) {
  tests.push_back({"alias_qualifier", test_alias_qualifier});
  tests.push_back({"alias_source_only", test_alias_source_only});
  tests.push_back({"attr_shorthand_self_and_qualified_aliases",
                   test_attr_shorthand_self_and_qualified_aliases});
  tests.push_back({"attr_shorthand_axis_paths", test_attr_shorthand_axis_paths});
  tests.push_back({"parse_alias_field_with_implicit_doc", test_parse_alias_field_with_implicit_doc});
  tests.push_back({"parse_alias_field_with_explicit_alias", test_parse_alias_field_with_explicit_alias});
  tests.push_back({"parse_legacy_tag_binding_still_works", test_parse_legacy_tag_binding_still_works});
  tests.push_back({"duplicate_source_alias_rejected", test_duplicate_source_alias_rejected});
  tests.push_back({"implicit_doc_alias_matches_explicit_alias_values",
                   test_implicit_doc_alias_matches_explicit_alias_values});
  tests.push_back({"doc_identifier_rejected_after_explicit_realias",
                   test_doc_identifier_rejected_after_explicit_realias});
  tests.push_back({"unknown_identifier_error_mentions_alias_or_legacy_binding",
                   test_unknown_identifier_error_mentions_alias_or_legacy_binding});
}
