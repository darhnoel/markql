#include <exception>

#include "lang/markql_parser.h"
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
  auto single = markql::parse_query(
      "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n) "
      "SELECT rows.row_id FROM rows");
  expect_true(single.query.has_value(), "WITH single CTE parses");

  auto multi = markql::parse_query(
      "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n), "
      "cells AS (SELECT rows.row_id FROM rows) "
      "SELECT cells.row_id FROM cells");
  expect_true(multi.query.has_value(), "WITH multiple CTEs parses");
}

void test_parse_derived_table_and_joins() {
  auto derived = markql::parse_query(
      "SELECT t.row_id FROM (SELECT n.node_id AS row_id FROM doc AS n) AS t");
  expect_true(derived.query.has_value(), "derived table source parses");

  auto joins = markql::parse_query(
      "SELECT r.node_id, c.node_id "
      "FROM doc AS r "
      "JOIN doc AS c ON c.parent_id = r.node_id");
  expect_true(joins.query.has_value(), "inner JOIN parses");

  auto left_join = markql::parse_query(
      "SELECT r.node_id, c.node_id "
      "FROM doc AS r "
      "LEFT JOIN doc AS c ON c.parent_id = r.node_id");
  expect_true(left_join.query.has_value(), "LEFT JOIN parses");

  auto cross_join = markql::parse_query(
      "SELECT r.node_id, c.node_id FROM doc AS r CROSS JOIN doc AS c");
  expect_true(cross_join.query.has_value(), "CROSS JOIN parses");
}

void test_parse_cross_join_lateral() {
  auto parsed = markql::parse_query(
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
  auto parsed = markql::parse_query(
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
  auto parsed = markql::parse_query(
      "SELECT row_id FROM (SELECT n.node_id AS row_id FROM doc AS n)");
  expect_true(!parsed.query.has_value(), "derived table without alias fails");
  expect_true(parsed.error.has_value(), "derived table without alias error is populated");
  if (parsed.error.has_value()) {
    expect_true(parsed.error->message == "Derived table requires an alias",
                "derived table alias error message");
  }
}

void test_parse_reject_join_without_on() {
  auto parsed = markql::parse_query(
      "SELECT r.node_id FROM doc AS r JOIN doc AS c");
  expect_true(!parsed.query.has_value(), "JOIN without ON fails parse");
  expect_true(parsed.error.has_value(), "JOIN without ON parse error");
  if (parsed.error.has_value()) {
    expect_true(parsed.error->message == "JOIN requires ON clause",
                "JOIN without ON message");
  }
}

void test_parse_reject_cross_join_with_on() {
  auto parsed = markql::parse_query(
      "SELECT r.node_id FROM doc AS r CROSS JOIN doc AS c ON c.parent_id = r.node_id");
  expect_true(!parsed.query.has_value(), "CROSS JOIN with ON fails parse");
  expect_true(parsed.error.has_value(), "CROSS JOIN with ON parse error");
  if (parsed.error.has_value()) {
    expect_true(parsed.error->message == "CROSS JOIN does not allow ON",
                "CROSS JOIN ON rejection message");
  }
}

void test_parse_reject_lateral_without_alias() {
  auto parsed = markql::parse_query(
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

void test_lateral_select_self_equivalent_to_select_alias() {
  std::string html =
      "<div id='root'><span>A</span><span>B</span></div>";
  auto legacy = run_query(
      html,
      "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n WHERE n.tag = 'div') "
      "SELECT c.node_id "
      "FROM rows AS r "
      "CROSS JOIN LATERAL ("
      "  SELECT node_span "
      "  FROM doc AS node_span "
      "  WHERE node_span.parent_id = r.row_id AND node_span.tag = 'span'"
      ") AS c "
      "ORDER BY node_id");
  auto canonical = run_query(
      html,
      "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n WHERE n.tag = 'div') "
      "SELECT c.node_id "
      "FROM rows AS r "
      "CROSS JOIN LATERAL ("
      "  SELECT self "
      "  FROM doc AS node_span "
      "  WHERE node_span.parent_id = r.row_id AND node_span.tag = 'span'"
      ") AS c "
      "ORDER BY node_id");
  expect_eq(canonical.rows.size(), legacy.rows.size(),
            "select self and select <alias> equivalent row count in lateral");
  if (canonical.rows.size() != legacy.rows.size()) return;
  for (size_t i = 0; i < canonical.rows.size(); ++i) {
    expect_true(canonical.rows[i].node_id == legacy.rows[i].node_id,
                "select self and select <alias> equivalent node_id");
  }
}

void test_with_qualified_parent_axis_and_case_projection() {
  std::string html =
      "<table>"
      "<tr>Sunday<td data-date='2025-02-23'>A</td></tr>"
      "</table>";
  auto result = run_query(
      html,
      "WITH cells AS ("
      "  SELECT n.node_id AS node_id, LTRIM(RTRIM(n.parent.text)) AS raw_day "
      "  FROM doc AS n "
      "  WHERE n.tag = 'td'"
      ") "
      "SELECT c.raw_day, "
      "CASE WHEN c.raw_day LIKE 'Sunday%' THEN 'Sunday' ELSE c.raw_day END AS day "
      "FROM cells AS c "
      "ORDER BY c.node_id");
  expect_eq(result.rows.size(), 1, "qualified parent axis row count");
  if (result.rows.empty()) return;
  const std::string raw = result.rows[0].computed_fields["raw_day"];
  expect_true(raw.rfind("Sunday", 0) == 0, "parent text projection");
  expect_true(result.rows[0].computed_fields["day"] == "Sunday", "CASE projection in relation runtime");
}

void test_with_join_to_csv_sets_export_sink() {
  std::string html =
      "<table>"
      "<tr><td>A</td><td>ID-123</td><td>...</td><td>Apple</td></tr>"
      "</table>";
  auto result = run_query(
      html,
      "WITH rows AS (SELECT n.node_id AS row_id FROM doc AS n WHERE n.tag = 'tr') "
      "SELECT r.row_id FROM rows AS r TO CSV('out.csv')");
  expect_true(result.export_sink.kind == markql::QueryResult::ExportSink::Kind::Csv,
              "WITH/JOIN runtime keeps TO CSV export sink");
  expect_true(result.export_sink.path == "out.csv",
              "WITH/JOIN runtime keeps export path");
}

void test_with_inner_equi_join_by_cte_field() {
  std::string html =
      "<table>"
      "<tr><td>A</td><td>B</td></tr>"
      "<tr><td>C</td></tr>"
      "</table>";
  auto result = run_query(
      html,
      "WITH rows AS ("
      "  SELECT n.node_id AS row_id "
      "  FROM doc AS n "
      "  WHERE n.tag = 'tr'"
      "), "
      "cells AS ("
      "  SELECT n.parent_id AS row_id, TEXT(n) AS cell_text "
      "  FROM doc AS n "
      "  WHERE n.tag = 'td'"
      ") "
      "SELECT r.row_id, c.cell_text "
      "FROM rows AS r "
      "JOIN cells AS c ON r.row_id = c.row_id "
      "ORDER BY r.row_id, c.cell_text");
  expect_eq(result.rows.size(), 3, "inner equi join row count");
  if (result.rows.size() != 3) return;
  expect_true(result.rows[0].computed_fields["cell_text"] == "A", "inner equi join first value");
  expect_true(result.rows[1].computed_fields["cell_text"] == "B", "inner equi join second value");
  expect_true(result.rows[2].computed_fields["cell_text"] == "C", "inner equi join third value");
}

void test_with_left_equi_join_preserves_unmatched_rows() {
  std::string html =
      "<table>"
      "<tr><td>A</td></tr>"
      "<tr><td>B</td></tr>"
      "</table>";
  auto result = run_query(
      html,
      "WITH rows AS ("
      "  SELECT n.node_id AS row_id "
      "  FROM doc AS n "
      "  WHERE n.tag = 'tr'"
      "), "
      "only_a AS ("
      "  SELECT n.parent_id AS row_id, TEXT(n) AS cell_text "
      "  FROM doc AS n "
      "  WHERE n.tag = 'td' AND TEXT(n) = 'A'"
      ") "
      "SELECT r.row_id, c.cell_text "
      "FROM rows AS r "
      "LEFT JOIN only_a AS c ON r.row_id = c.row_id "
      "ORDER BY r.row_id");
  expect_eq(result.rows.size(), 2, "left equi join keeps unmatched row");
  if (result.rows.size() != 2) return;
  expect_true(result.rows[0].computed_fields["cell_text"] == "A",
              "left equi join matched row value");
  expect_true(result.rows[1].computed_fields.find("cell_text") ==
                  result.rows[1].computed_fields.end(),
              "left equi join unmatched row remains NULL");
}

void test_with_hash_join_preserves_duplicate_multiplicity() {
  std::string html =
      "<root>"
      "<x>dup</x>"
      "<x>dup</x>"
      "<y>dup</y>"
      "<y>dup</y>"
      "</root>";
  auto result = run_query(
      html,
      "WITH left_vals AS ("
      "  SELECT n.text AS k "
      "  FROM doc AS n "
      "  WHERE n.tag = 'x'"
      "), "
      "right_vals AS ("
      "  SELECT n.text AS k, n.node_id AS rid "
      "  FROM doc AS n "
      "  WHERE n.tag = 'y'"
      ") "
      "SELECT l.k, r.rid "
      "FROM left_vals AS l "
      "JOIN right_vals AS r ON l.k = r.k "
      "ORDER BY l.k, r.rid");
  expect_eq(result.rows.size(), 4, "hash join keeps one-to-many duplicate multiplicity");
  if (result.rows.size() != 4) return;
  for (const auto& row : result.rows) {
    expect_true(row.computed_fields.at("k") == "dup", "duplicate join key preserved");
  }
  const std::string rid0 = result.rows[0].computed_fields.at("rid");
  const std::string rid1 = result.rows[1].computed_fields.at("rid");
  const std::string rid2 = result.rows[2].computed_fields.at("rid");
  const std::string rid3 = result.rows[3].computed_fields.at("rid");
  expect_true(rid0 == rid1, "first right row appears once per matching left row");
  expect_true(rid2 == rid3, "second right row appears once per matching left row");
  expect_true(rid0 != rid2, "both distinct right duplicates survive join");
}

void test_with_hash_join_null_keys_do_not_match() {
  std::string html =
      "<root>"
      "<x class='a'></x>"
      "<x></x>"
      "<y class='a'></y>"
      "<y></y>"
      "</root>";
  auto result = run_query(
      html,
      "WITH left_vals AS ("
      "  SELECT n.node_id AS lid, n.class AS cls "
      "  FROM doc AS n "
      "  WHERE n.tag = 'x'"
      "), "
      "right_vals AS ("
      "  SELECT n.class AS cls, n.node_id AS rid "
      "  FROM doc AS n "
      "  WHERE n.tag = 'y'"
      ") "
      "SELECT l.lid, l.cls, r.rid "
      "FROM left_vals AS l "
      "LEFT JOIN right_vals AS r ON l.cls = r.cls "
      "ORDER BY l.lid");
  expect_eq(result.rows.size(), 2, "left join row count with nullable join keys");
  if (result.rows.size() != 2) return;
  expect_true(result.rows[0].computed_fields.at("cls") == "a",
              "non-null key matches expected right row");
  expect_true(result.rows[0].computed_fields.find("rid") !=
                  result.rows[0].computed_fields.end(),
              "non-null key produces a right-side match");
  expect_true(result.rows[1].computed_fields.find("cls") ==
                  result.rows[1].computed_fields.end(),
              "null left key remains null");
  expect_true(result.rows[1].computed_fields.find("rid") ==
                  result.rows[1].computed_fields.end(),
              "null key does not match null key on right side");
}

void test_with_repeated_cell_nodes_joins_correctness() {
  std::string html =
      "<table>"
      "<tr><td class='team'>Lions</td><td class='w'>10</td><td class='l'>2</td><td class='ot'>1</td><td class='pts'>21</td></tr>"
      "<tr><td class='team'>Bears</td><td class='w'>8</td><td class='l'>4</td><td class='ot'>0</td><td class='pts'>16</td></tr>"
      "</table>";
  auto result = run_query(
      html,
      "WITH row_nodes AS ("
      "  SELECT tr.node_id AS row_id "
      "  FROM doc AS tr "
      "  WHERE tr.tag = 'tr'"
      "), "
      "cell_nodes AS ("
      "  SELECT c.parent_id AS row_id, c.class AS cls, TEXT(c) AS text_value "
      "  FROM doc AS c "
      "  WHERE c.tag = 'td'"
      "), "
      "row_values AS ("
      "  SELECT r.row_id, "
      "         team_cell.text_value AS team_value, "
      "         w_cell.text_value AS w_value, "
      "         l_cell.text_value AS l_value, "
      "         ot_cell.text_value AS ot_value, "
      "         pts_cell.text_value AS pts_value "
      "  FROM row_nodes AS r "
      "  LEFT JOIN cell_nodes AS team_cell ON team_cell.row_id = r.row_id AND team_cell.cls = 'team' "
      "  LEFT JOIN cell_nodes AS w_cell ON w_cell.row_id = r.row_id AND w_cell.cls = 'w' "
      "  LEFT JOIN cell_nodes AS l_cell ON l_cell.row_id = r.row_id AND l_cell.cls = 'l' "
      "  LEFT JOIN cell_nodes AS ot_cell ON ot_cell.row_id = r.row_id AND ot_cell.cls = 'ot' "
      "  LEFT JOIN cell_nodes AS pts_cell ON pts_cell.row_id = r.row_id AND pts_cell.cls = 'pts'"
      ") "
      "SELECT rv.row_id, rv.team_value, rv.w_value, rv.l_value, rv.ot_value, rv.pts_value "
      "FROM row_values AS rv "
      "ORDER BY rv.row_id");
  expect_eq(result.rows.size(), 2, "repeated joins row count");
  if (result.rows.size() != 2) return;
  expect_true(result.rows[0].computed_fields.at("team_value") == "Lions", "row1 team");
  expect_true(result.rows[0].computed_fields.at("w_value") == "10", "row1 w");
  expect_true(result.rows[0].computed_fields.at("l_value") == "2", "row1 l");
  expect_true(result.rows[0].computed_fields.at("ot_value") == "1", "row1 ot");
  expect_true(result.rows[0].computed_fields.at("pts_value") == "21", "row1 pts");
  expect_true(result.rows[1].computed_fields.at("team_value") == "Bears", "row2 team");
  expect_true(result.rows[1].computed_fields.at("w_value") == "8", "row2 w");
  expect_true(result.rows[1].computed_fields.at("l_value") == "4", "row2 l");
  expect_true(result.rows[1].computed_fields.at("ot_value") == "0", "row2 ot");
  expect_true(result.rows[1].computed_fields.at("pts_value") == "16", "row2 pts");
}

void test_with_non_equi_join_fallback_behavior() {
  std::string html =
      "<root>"
      "<x>a</x>"
      "<x>b</x>"
      "</root>";
  auto result = run_query(
      html,
      "WITH vals AS ("
      "  SELECT n.text AS v "
      "  FROM doc AS n "
      "  WHERE n.tag = 'x'"
      ") "
      "SELECT l.v AS lv, r.v AS rv "
      "FROM vals AS l "
      "JOIN vals AS r ON l.v <> r.v "
      "ORDER BY lv, rv");
  expect_eq(result.rows.size(), 2, "non-equi join fallback row count");
  if (result.rows.size() != 2) return;
  expect_true(result.rows[0].computed_fields["lv"] == "a", "fallback row1 left");
  expect_true(result.rows[0].computed_fields["rv"] == "b", "fallback row1 right");
  expect_true(result.rows[1].computed_fields["lv"] == "b", "fallback row2 left");
  expect_true(result.rows[1].computed_fields["rv"] == "a", "fallback row2 right");
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
  tests.push_back({"lateral_select_self_equivalent_to_select_alias",
                   test_lateral_select_self_equivalent_to_select_alias});
  tests.push_back({"with_qualified_parent_axis_and_case_projection",
                   test_with_qualified_parent_axis_and_case_projection});
  tests.push_back({"with_join_to_csv_sets_export_sink",
                   test_with_join_to_csv_sets_export_sink});
  tests.push_back({"with_inner_equi_join_by_cte_field",
                   test_with_inner_equi_join_by_cte_field});
  tests.push_back({"with_left_equi_join_preserves_unmatched_rows",
                   test_with_left_equi_join_preserves_unmatched_rows});
  tests.push_back({"with_hash_join_preserves_duplicate_multiplicity",
                   test_with_hash_join_preserves_duplicate_multiplicity});
  tests.push_back({"with_hash_join_null_keys_do_not_match",
                   test_with_hash_join_null_keys_do_not_match});
  tests.push_back({"with_repeated_cell_nodes_joins_correctness",
                   test_with_repeated_cell_nodes_joins_correctness});
  tests.push_back({"with_non_equi_join_fallback_behavior",
                   test_with_non_equi_join_fallback_behavior});
}
