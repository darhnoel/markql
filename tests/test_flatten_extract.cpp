#include <exception>

#include "test_harness.h"
#include "test_utils.h"

namespace {

void test_flatten_extract_basic() {
  std::string html =
      "<table><tbody>"
      "<tr>"
      "<td>2025</td>"
      "<td><a href='precond.pdf'>PDF</a></td>"
      "<td><a href='direct.pdf'>PDF</a></td>"
      "<td>N/A</td>"
      "<td><a href='direct.xlsx'>Excel</a></td>"
      "<td>Missing</td>"
      "</tr>"
      "<tr>"
      "<td>2024</td>"
      "<td><a href='precond2.pdf'>PDF</a></td>"
      "<td>Pending</td>"
      "<td><a href='layover2.pdf'>PDF</a></td>"
      "<td><a href='direct2.xlsx'>Excel</a></td>"
      "<td><a href='layover2.xlsx'>Excel</a></td>"
      "</tr>"
      "</tbody></table>";

  auto result = run_query(
      html,
      "SELECT tr.node_id, "
      "PROJECT(tr) AS ("
      "period: TEXT(td WHERE sibling_pos = 1),"
      "pdf_direct: COALESCE(ATTR(a, href WHERE parent.sibling_pos = 3 AND href CONTAINS '.pdf'), "
      "TEXT(td WHERE sibling_pos = 3)),"
      "pdf_layover: COALESCE(ATTR(a, href WHERE parent.sibling_pos = 4 AND href CONTAINS '.pdf'), "
      "TEXT(td WHERE sibling_pos = 4)),"
      "excel_direct: COALESCE(ATTR(a, href WHERE parent.sibling_pos = 5 AND href CONTAINS '.xlsx'), "
      "TEXT(td WHERE sibling_pos = 5)),"
      "excel_layover: COALESCE(ATTR(a, href WHERE parent.sibling_pos = 6 AND href CONTAINS '.xlsx'), "
      "TEXT(td WHERE sibling_pos = 6))"
      ") "
      "FROM document "
      "WHERE EXISTS(child WHERE tag = 'td')");

  expect_eq(result.columns.size(), 6, "flatten_extract column count");
  if (result.columns.size() == 6) {
    expect_true(result.columns[0] == "node_id", "flatten_extract col 1");
    expect_true(result.columns[1] == "period", "flatten_extract col 2");
    expect_true(result.columns[2] == "pdf_direct", "flatten_extract col 3");
    expect_true(result.columns[3] == "pdf_layover", "flatten_extract col 4");
    expect_true(result.columns[4] == "excel_direct", "flatten_extract col 5");
    expect_true(result.columns[5] == "excel_layover", "flatten_extract col 6");
  }

  expect_eq(result.rows.size(), 2, "flatten_extract row count");
  if (result.rows.size() >= 2) {
    expect_true(result.rows[0].computed_fields["period"] == "2025", "flatten_extract row1 period");
    expect_true(result.rows[0].computed_fields["pdf_direct"] == "direct.pdf", "flatten_extract row1 pdf_direct");
    expect_true(result.rows[0].computed_fields["pdf_layover"] == "N/A", "flatten_extract row1 pdf_layover fallback");
    expect_true(result.rows[0].computed_fields["excel_direct"] == "direct.xlsx", "flatten_extract row1 excel_direct");
    expect_true(result.rows[0].computed_fields["excel_layover"] == "Missing", "flatten_extract row1 excel_layover fallback");

    expect_true(result.rows[1].computed_fields["period"] == "2024", "flatten_extract row2 period");
    expect_true(result.rows[1].computed_fields["pdf_direct"] == "Pending", "flatten_extract row2 pdf_direct fallback");
    expect_true(result.rows[1].computed_fields["pdf_layover"] == "layover2.pdf", "flatten_extract row2 pdf_layover");
    expect_true(result.rows[1].computed_fields["excel_direct"] == "direct2.xlsx", "flatten_extract row2 excel_direct");
    expect_true(result.rows[1].computed_fields["excel_layover"] == "layover2.xlsx", "flatten_extract row2 excel_layover");
  }
}

void test_flatten_extract_has_direct_text_predicate() {
  std::string html =
      "<table><tbody>"
      "<tr><td>Period 2025</td><td><a href='a.pdf'>PDF</a></td></tr>"
      "<tr><td>Period 2024</td><td><a href='b.pdf'>PDF</a></td></tr>"
      "</tbody></table>";

  auto result = run_query(
      html,
      "SELECT tr.node_id, "
      "PROJECT(tr) AS ("
      "period: TEXT(td WHERE td HAS_DIRECT_TEXT '2025'),"
      "pdf: COALESCE(ATTR(a, href WHERE parent.sibling_pos = 2), TEXT(td WHERE sibling_pos = 2))"
      ") "
      "FROM document "
      "WHERE EXISTS(child WHERE tag = 'td')");

  expect_eq(result.rows.size(), 2, "flatten_extract has_direct_text row count");
  if (result.rows.size() >= 2) {
    expect_true(result.rows[0].computed_fields["period"] == "Period 2025",
                "flatten_extract has_direct_text row1 match");
    expect_true(result.rows[1].computed_fields.find("period") == result.rows[1].computed_fields.end(),
                "flatten_extract has_direct_text row2 no match");
  }
}

void test_flatten_extract_requires_as_pairs() {
  bool threw = false;
  try {
    std::string html = "<table><tr><td>x</td></tr></table>";
    run_query(html, "SELECT PROJECT(tr) FROM document");
  } catch (const std::exception&) {
    threw = true;
  }
  expect_true(threw, "flatten_extract requires AS(alias: expr)");
}

void test_flatten_extract_alias_compatibility() {
  std::string html = "<table><tr><td>2025</td></tr></table>";
  auto result = run_query(
      html,
      "SELECT FLATTEN_EXTRACT(tr) AS (period: TEXT(td WHERE sibling_pos = 1)) "
      "FROM document WHERE EXISTS(child WHERE tag = 'td')");
  expect_eq(result.rows.size(), 1, "flatten_extract alias compatibility row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].computed_fields["period"] == "2025",
                "flatten_extract alias compatibility value");
  }
}

}  // namespace

void register_flatten_extract_tests(std::vector<TestCase>& tests) {
  tests.push_back({"flatten_extract_basic", test_flatten_extract_basic});
  tests.push_back({"flatten_extract_has_direct_text_predicate", test_flatten_extract_has_direct_text_predicate});
  tests.push_back({"flatten_extract_requires_as_pairs", test_flatten_extract_requires_as_pairs});
  tests.push_back({"flatten_extract_alias_compatibility", test_flatten_extract_alias_compatibility});
}
