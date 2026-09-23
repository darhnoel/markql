#include "markql/inspection.h"
#include "test_harness.h"

namespace {

void test_inspection_records() {
  const auto evidence = markql::inspect_html(
      "<ul><li class='card a'><b>A</b></li><li class='card b'><b>B</b></li>"
      "<li class='card'><b>C</b></li></ul>");
  expect_eq(evidence.record_candidates.size(), 1, "one repeated sibling group");
  const auto& record = evidence.record_candidates.at(0);
  expect_eq(record.count, 3, "record count");
  expect_true(record.recipe.classes == std::vector<std::string>{"card"}, "common classes only");
  expect_true(!evidence.truncated, "small evidence is complete");
  const auto& field = record.field_candidates.at(1);
  expect_true(field.path == std::vector<size_t>{1}, "relative element path");
  expect_true(field.samples == std::vector<std::string>{"A", "B", "C"}, "document-order samples");
  expect_true(markql::inspect_html("").record_candidates.empty(), "empty input");
}

void test_inspection_limits() {
  std::string html;
  for (int i = 0; i < 70; ++i) {
    html += "<section><ul><li><b>A</b></li><li><b>B</b></li><li><b>C</b></li></ul></section>";
  }
  const auto many = markql::inspect_html(html);
  expect_eq(many.record_candidates.size(), 64, "record limit");
  expect_true(many.truncated, "record overflow is explicit");

  std::string row = "<tr>";
  for (int i = 0; i < 80; ++i) row += "<td>value</td>";
  row += "</tr>";
  const auto wide = markql::inspect_html("<table>" + row + row + row + "</table>");
  expect_eq(wide.record_candidates.at(0).field_candidates.size(), 64, "field limit");
  expect_true(wide.truncated, "field overflow is explicit");

  std::string deep = "<li>";
  for (int i = 0; i < 40; ++i) deep += "<div>";
  deep += "value";
  for (int i = 0; i < 40; ++i) deep += "</div>";
  deep += "</li>";
  const auto nested = markql::inspect_html("<ul>" + deep + deep + deep + "</ul>");
  expect_true(nested.truncated, "depth overflow is explicit");
  for (const auto& field : nested.record_candidates.at(0).field_candidates) {
    expect_true(field.path.size() <= 32, "bounded relative paths");
  }
}

}  // namespace

void register_inspection_tests(std::vector<TestCase>& tests) {
  tests.push_back({"inspection_records", test_inspection_records});
  tests.push_back({"inspection_limits", test_inspection_limits});
}
