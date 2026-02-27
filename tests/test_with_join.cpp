#include <exception>

#include "query_parser.h"
#include "test_harness.h"
#include "test_utils.h"

namespace {

std::string baseline_query() {
  return
      "WITH rows AS ("
      "  SELECT n.node_id AS row_id "
      "  FROM doc AS n "
      "  WHERE n.tag = 'tr' AND EXISTS(child WHERE tag = 'td')"
      "), "
      "cells AS ("
      "  SELECT r.row_id, c.sibling_pos AS pos, TEXT(c) AS val "
      "  FROM rows AS r "
      "  CROSS JOIN LATERAL ("
      "    SELECT c "
      "    FROM doc AS c "
      "    WHERE c.parent_id = r.row_id AND c.tag = 'td'"
      "  ) AS c"
      ") "
      "SELECT r.row_id, c2.val AS item_id, c4.val AS item_name "
      "FROM rows AS r "
      "LEFT JOIN cells AS c2 ON c2.row_id = r.row_id AND c2.pos = 2 "
      "LEFT JOIN cells AS c4 ON c4.row_id = r.row_id AND c4.pos = 4 "
      "ORDER BY r.row_id";
}

void test_parse_with_single_and_multiple_ctes() {
  auto single = xsql::parse_query(
      "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n) "
      "SELECT rows.row_id FROM rows");
  expect_true(single.query.has_value(), "WITH single CTE parses");

  auto multi = xsql::parse_query(
      "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n), "
      "cells AS (SELECT rows.row_id FROM rows) "
      "SELECT cells.row_id FROM cells");
  expect_true(multi.query.has_value(), "WITH multiple CTEs parses");
}

void test_parse_derived_table_and_joins() {
  auto derived = xsql::parse_query(
      "SELECT t.row_id FROM (SELECT n.node_id AS row_id FROM doc AS n) AS t");
  expect_true(derived.query.has_value(), "derived table source parses");

  auto joins = xsql::parse_query(
      "SELECT r.node_id, c.node_id "
      "FROM doc AS r "
      "JOIN doc AS c ON c.parent_id = r.node_id");
  expect_true(joins.query.has_value(), "inner JOIN parses");

  auto left_join = xsql::parse_query(
      "SELECT r.node_id, c.node_id "
      "FROM doc AS r "
      "LEFT JOIN doc AS c ON c.parent_id = r.node_id");
  expect_true(left_join.query.has_value(), "LEFT JOIN parses");

  auto cross_join = xsql::parse_query(
      "SELECT r.node_id, c.node_id FROM doc AS r CROSS JOIN doc AS c");
  expect_true(cross_join.query.has_value(), "CROSS JOIN parses");
}

void test_parse_cross_join_lateral() {
  auto parsed = xsql::parse_query(
      "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n) "
      "SELECT r.row_id "
      "FROM rows AS r "
      "CROSS JOIN LATERAL ("
      "  SELECT c "
      "  FROM doc AS c "
      "  WHERE c.parent_id = r.row_id"
      ") AS c");
  expect_true(parsed.query.has_value(), "CROSS JOIN LATERAL parses");
}

void test_parse_reject_duplicate_cte_name() {
  auto parsed = xsql::parse_query(
      "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n), "
      "rows AS (SELECT n.node_id AS row_id FROM doc AS n) "
      "SELECT rows.row_id FROM rows");
  expect_true(!parsed.query.has_value(), "duplicate CTE name should fail parse");
  expect_true(parsed.error.has_value(), "duplicate CTE name returns parse error");
  if (parsed.error.has_value()) {
    expect_true(parsed.error->message == "Duplicate CTE name 'rows' in WITH",
                "duplicate CTE name message");
  }
}

void test_parse_reject_derived_table_without_alias() {
  auto parsed = xsql::parse_query(
      "SELECT row_id FROM (SELECT n.node_id AS row_id FROM doc AS n)");
  expect_true(!parsed.query.has_value(), "derived table without alias fails");
  expect_true(parsed.error.has_value(), "derived table without alias error is populated");
  if (parsed.error.has_value()) {
    expect_true(parsed.error->message == "Derived table requires an alias",
                "derived table alias error message");
  }
}

void test_parse_reject_join_without_on() {
  auto parsed = xsql::parse_query(
      "SELECT r.node_id FROM doc AS r JOIN doc AS c");
  expect_true(!parsed.query.has_value(), "JOIN without ON fails parse");
  expect_true(parsed.error.has_value(), "JOIN without ON parse error");
  if (parsed.error.has_value()) {
    expect_true(parsed.error->message == "JOIN requires ON clause",
                "JOIN without ON message");
  }
}

void test_parse_reject_cross_join_with_on() {
  auto parsed = xsql::parse_query(
      "SELECT r.node_id FROM doc AS r CROSS JOIN doc AS c ON c.parent_id = r.node_id");
  expect_true(!parsed.query.has_value(), "CROSS JOIN with ON fails parse");
  expect_true(parsed.error.has_value(), "CROSS JOIN with ON parse error");
  if (parsed.error.has_value()) {
    expect_true(parsed.error->message == "CROSS JOIN does not allow ON",
                "CROSS JOIN ON rejection message");
  }
}

void test_parse_reject_lateral_without_alias() {
  auto parsed = xsql::parse_query(
      "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n) "
      "SELECT r.row_id "
      "FROM rows AS r "
      "CROSS JOIN LATERAL ("
      "  SELECT c FROM doc AS c WHERE c.parent_id = r.row_id"
      ")");
  expect_true(!parsed.query.has_value(), "LATERAL without alias fails parse");
  expect_true(parsed.error.has_value(), "LATERAL without alias parse error");
  if (parsed.error.has_value()) {
    expect_true(parsed.error->message == "LATERAL subquery requires an alias",
                "LATERAL alias message");
  }
}

void test_lateral_unknown_alias_throws() {
  std::string html =
      "<table>"
      "<tr><td>A</td></tr>"
      "</table>";
  bool threw = false;
  try {
    run_query(
        html,
        "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n WHERE n.tag = 'tr') "
        "SELECT r.row_id "
        "FROM rows AS r "
        "CROSS JOIN LATERAL ("
        "  SELECT c FROM doc AS c WHERE c.parent_id = x.row_id"
        ") AS c");
  } catch (const std::exception& ex) {
    threw = true;
    expect_true(
        std::string(ex.what()) ==
            "Unknown identifier 'x' (expected a FROM alias or legacy tag binding)",
        "unknown alias inside LATERAL message");
  }
  expect_true(threw, "unknown alias in LATERAL should throw");
}

void test_with_left_join_lateral_baseline_values() {
  std::string html =
      "<table>"
      "<tr><td>A</td><td>ID-123</td><td>...</td><td>Apple</td></tr>"
      "<tr><td>B</td><td>ID-999</td><td>...</td><td>Banana</td></tr>"
      "</table>";
  auto result = run_query(html, baseline_query());
  expect_true(result.columns == std::vector<std::string>{"row_id", "item_id", "item_name"},
              "baseline columns");
  expect_eq(result.rows.size(), 2, "baseline row count");
  if (result.rows.size() != 2) return;
  expect_true(result.rows[0].computed_fields["item_id"] == "ID-123", "row1 item_id");
  expect_true(result.rows[0].computed_fields["item_name"] == "Apple", "row1 item_name");
  expect_true(result.rows[1].computed_fields["item_id"] == "ID-999", "row2 item_id");
  expect_true(result.rows[1].computed_fields["item_name"] == "Banana", "row2 item_name");
}

void test_with_left_join_lateral_missing_right_value_null() {
  std::string html =
      "<table>"
      "<tr><td>A</td><td>ID-123</td><td>...</td><td>Apple</td></tr>"
      "<tr><td>B</td><td>ID-999</td><td>...</td></tr>"
      "</table>";
  auto result = run_query(html, baseline_query());
  expect_eq(result.rows.size(), 2, "missing-right row count");
  if (result.rows.size() != 2) return;
  expect_true(result.rows[0].computed_fields["item_name"] == "Apple", "first row keeps item_name");
  expect_true(result.rows[1].computed_fields.find("item_name") ==
                  result.rows[1].computed_fields.end(),
              "missing fourth cell becomes NULL");
}

}  // namespace

void register_with_join_tests(std::vector<TestCase>& tests) {
  tests.push_back({"parse_with_single_and_multiple_ctes", test_parse_with_single_and_multiple_ctes});
  tests.push_back({"parse_derived_table_and_joins", test_parse_derived_table_and_joins});
  tests.push_back({"parse_cross_join_lateral", test_parse_cross_join_lateral});
  tests.push_back({"parse_reject_duplicate_cte_name", test_parse_reject_duplicate_cte_name});
  tests.push_back({"parse_reject_derived_table_without_alias",
                   test_parse_reject_derived_table_without_alias});
  tests.push_back({"parse_reject_join_without_on", test_parse_reject_join_without_on});
  tests.push_back({"parse_reject_cross_join_with_on", test_parse_reject_cross_join_with_on});
  tests.push_back({"parse_reject_lateral_without_alias", test_parse_reject_lateral_without_alias});
  tests.push_back({"lateral_unknown_alias_throws", test_lateral_unknown_alias_throws});
  tests.push_back({"with_left_join_lateral_baseline_values",
                   test_with_left_join_lateral_baseline_values});
  tests.push_back({"with_left_join_lateral_missing_right_value_null",
                   test_with_left_join_lateral_missing_right_value_null});
}
