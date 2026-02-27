#include "test_harness.h"
#include "test_utils.h"
#include "util/string_util.h"

namespace {

void test_text_requires_non_tag_filter() {
  bool threw = false;
  try {
    std::string html = "<div></div>";
    run_query(html, "SELECT TEXT(div) FROM document WHERE tag = 'div'");
  } catch (const std::exception&) {
    threw = true;
  }
  expect_true(threw, "text requires non-tag filter");
}

void test_inner_html_function() {
  std::string html = "<div id='root'><span>Hi</span><em>There</em></div>";
  auto result = run_query(html, "SELECT inner_html(div) FROM document WHERE attributes.id = 'root'");
  expect_eq(result.columns.size(), 1, "inner_html projection has one column");
  if (!result.columns.empty()) {
    expect_true(result.columns[0] == "inner_html", "inner_html column name");
  }
  expect_eq(result.rows.size(), 1, "inner_html row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].inner_html == "<span>Hi</span><em>There</em>",
                "inner_html content");
  }
}

void test_minify_html_basic() {
  std::string input = "<div>\n <span> hi </span>\n</div>";
  std::string got = xsql::util::minify_html(input);
  expect_true(got == "<div><span> hi </span></div>", "minify html basic");
}

void test_minify_html_preserves_attribute_quotes() {
  std::string input = "<a href=\"x y\" title='a b'>   link </a>";
  std::string got = xsql::util::minify_html(input);
  expect_true(got.find("href=\"x y\"") != std::string::npos, "minify keeps double-quoted attribute value");
  expect_true(got.find("title='a b'") != std::string::npos, "minify keeps single-quoted attribute value");
  expect_true(got == "<a href=\"x y\" title='a b'> link </a>", "minify normalizes text spacing");
}

void test_minify_html_preserves_protected_tags() {
  std::string input = "<pre>\n a  b\n</pre><div>\n x   y\n</div>";
  std::string got = xsql::util::minify_html(input);
  expect_true(got == "<pre>\n a  b\n</pre><div> x y </div>", "minify preserves pre and compacts div text");
}

void test_minify_html_preserves_script_style() {
  std::string input =
      "<script>var  x = \"a  b\";\nif (x) { y = 1; }</script>"
      "<style>.a {  color: red; }</style>"
      "<div>\n z   z\n</div>";
  std::string got = xsql::util::minify_html(input);
  expect_true(got.find("<script>var  x = \"a  b\";\nif (x) { y = 1; }</script>") != std::string::npos,
              "minify preserves script content");
  expect_true(got.find("<style>.a {  color: red; }</style>") != std::string::npos,
              "minify preserves style content");
  expect_true(got.find("<div> z z </div>") != std::string::npos,
              "minify compacts non-protected text");
}

void test_inner_html_depth() {
  std::string html = "<div id='root'><span><b>Hi</b></span><em>There</em></div>";
  auto result = run_query(html, "SELECT inner_html(div, 1) FROM document WHERE attributes.id = 'root'");
  expect_eq(result.rows.size(), 1, "inner_html depth row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].inner_html == "<span>Hi</span><em>There</em>",
                "inner_html depth content");
  }
}

void test_inner_html_max_depth_auto() {
  std::string html = "<div id='root'><span><b>Hi</b></span><em>There</em></div>";
  auto result = run_query(html, "SELECT inner_html(div, max_depth) FROM document WHERE attributes.id = 'root'");
  expect_eq(result.rows.size(), 1, "inner_html max_depth row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].inner_html == "<span><b>Hi</b></span><em>There</em>",
                "inner_html max_depth content");
  }
}

void test_trim_inner_html() {
  std::string html = "<li id='item'>\n  <a href=\"/x\">Link</a>\n</li>";
  auto result = run_query(html, "SELECT trim(inner_html(li)) FROM document WHERE attributes.id = 'item'");
  expect_eq(result.rows.size(), 1, "trim inner_html row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].inner_html == "<a href=\"/x\">Link</a>",
                "trim inner_html content");
  }
}

void test_trim_mixed_with_other_projection() {
  std::string html = "<table><tr><td class='keep'>  Value  </td></tr></table>";
  auto result =
      run_query(html, "SELECT trim(text(td)), td.node_id FROM document WHERE attributes.class = 'keep'");
  expect_eq(result.columns.size(), 2, "trim mixed projection column count");
  if (result.columns.size() == 2) {
    expect_true(result.columns[0] == "text", "trim mixed first column");
    expect_true(result.columns[1] == "node_id", "trim mixed second column");
  }
  expect_eq(result.rows.size(), 1, "trim mixed projection row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].text == "Value", "trim mixed projection trims text");
    expect_true(result.rows[0].node_id > 0, "trim mixed projection preserves node_id");
  }
}

void test_inner_html_minified_by_default() {
  std::string html = "<div id='root'><span>   hi   there  </span></div>";
  auto result = run_query(html, "SELECT inner_html(div) FROM document WHERE attributes.id = 'root'");
  expect_eq(result.rows.size(), 1, "inner_html minified row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].inner_html == "<span> hi there </span>",
                "inner_html minified by default");
  }
}

void test_raw_inner_html_opt_out() {
  std::string html = "<div id='root'><span>   hi   there  </span></div>";
  auto result = run_query(html, "SELECT raw_inner_html(div) FROM document WHERE attributes.id = 'root'");
  expect_eq(result.rows.size(), 1, "raw inner_html row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].inner_html == "<span>   hi   there  </span>",
                "raw_inner_html preserves original spacing");
  }
}

void test_count_aggregate() {
  std::string html = "<div></div><div></div>";
  auto result = run_query(html, "SELECT COUNT(div) FROM document");
  expect_eq(result.rows.size(), 1, "count aggregate row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].node_id == 2, "count aggregate value");
  }
}

void test_count_star() {
  std::string html = "<div></div><span></span>";
  auto result = run_query(html, "SELECT COUNT(*) FROM document");
  expect_eq(result.rows.size(), 1, "count star row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].node_id >= 2, "count star value");
  }
}

void test_summarize_star() {
  std::string html = "<div></div><div></div><span></span>";
  auto result = run_query(html, "SELECT summarize(*) FROM document");
  bool saw_div = false;
  for (const auto& row : result.rows) {
    if (row.tag == "div" && row.node_id == 2) {
      saw_div = true;
      break;
    }
  }
  expect_true(saw_div, "summarize star includes div count");
}

void test_summarize_limit() {
  std::string html = "<div></div><div></div><span></span>";
  auto result = run_query(html, "SELECT summarize(*) FROM document WHERE tag IN ('div','span') LIMIT 1");
  expect_eq(result.rows.size(), 1, "summarize limit row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "div", "summarize limit top tag");
    expect_true(result.rows[0].node_id == 1, "summarize limit top count");
  }
}

void test_summarize_order_by_count() {
  std::string html = "<div></div><div></div><span></span>";
  auto result = run_query(html, "SELECT summarize(*) FROM document ORDER BY count DESC");
  expect_true(!result.rows.empty(), "summarize order by count non-empty");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].tag == "div", "summarize order by count first");
    expect_true(result.rows[0].node_id == 2, "summarize order by count value");
  }
}

void test_to_table_flag() {
  std::string html = "<table><tr><td>A</td></tr></table>";
  auto result = run_query(html, "SELECT table FROM document TO TABLE()");
  expect_true(result.to_table, "to table flag set");
}

void test_to_table_header_on() {
  std::string html = "<table><tr><td>H1</td></tr><tr><td>A</td></tr></table>";
  auto result = run_query(html, "SELECT table FROM document TO TABLE(HEADER=ON)");
  expect_true(result.to_table, "to table header on flag set");
  expect_true(result.table_has_header, "to table header on");
}

void test_to_table_header_off() {
  std::string html = "<table><tr><td>H1</td></tr><tr><td>A</td></tr></table>";
  auto result = run_query(html, "SELECT table FROM document TO TABLE(HEADER=OFF)");
  expect_true(result.to_table, "to table header off flag set");
  expect_true(!result.table_has_header, "to table header off");
}

void test_to_list_flag() {
  std::string html = "<a href='x'></a>";
  auto result = run_query(html, "SELECT a.href FROM document TO LIST()");
  expect_true(result.to_list, "to list flag set");
}

void test_tfidf_output_shape() {
  std::string html =
      "<p class='keep'>Apple banana</p>"
      "<li class='keep'>Carrot banana</li>"
      "<p class='skip'>Skip</p>";
  auto result = run_query(
      html,
      "SELECT TFIDF(p, li, TOP_TERMS=2, STOPWORDS=NONE) FROM document "
      "WHERE attributes.class = 'keep'");
  expect_eq(result.columns.size(), 4, "tfidf columns size");
  if (result.columns.size() == 4) {
    expect_true(result.columns[0] == "node_id", "tfidf column node_id");
    expect_true(result.columns[1] == "parent_id", "tfidf column parent_id");
    expect_true(result.columns[2] == "tag", "tfidf column tag");
    expect_true(result.columns[3] == "terms_score", "tfidf column terms_score");
  }
  expect_eq(result.rows.size(), 2, "tfidf row count");
  if (result.rows.size() == 2) {
    expect_true(result.rows[0].tag == "p", "tfidf output first tag");
    expect_true(result.rows[1].tag == "li", "tfidf output second tag");
    expect_true(!result.rows[0].term_scores.empty(), "tfidf output first row has scores");
    expect_true(!result.rows[1].term_scores.empty(), "tfidf output second row has scores");
  }
}

void test_tfidf_scoring_top_term() {
  std::string html =
      "<p>apple apple banana</p>"
      "<p>banana banana carrot</p>";
  auto result = run_query(html, "SELECT TFIDF(p, TOP_TERMS=1, STOPWORDS=NONE) FROM document");
  expect_eq(result.rows.size(), 2, "tfidf scoring row count");
  if (result.rows.size() == 2) {
    expect_true(result.rows[0].term_scores.count("apple") == 1, "tfidf top term for row 1");
    expect_true(result.rows[1].term_scores.count("banana") == 1, "tfidf top term for row 2");
  }
}

void test_tfidf_stopwords() {
  std::string html = "<p>the the apple</p>";
  auto result = run_query(html, "SELECT TFIDF(p, TOP_TERMS=5, STOPWORDS=ENGLISH) FROM document");
  expect_eq(result.rows.size(), 1, "tfidf stopwords row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].term_scores.count("the") == 0, "tfidf stopwords removed");
    expect_true(result.rows[0].term_scores.count("apple") == 1, "tfidf keeps non-stopword");
  }
}

void test_tfidf_strips_html_markup() {
  std::string html =
      "<body>"
      "<script type='text/x-handlebars-template'><tr><th>Title</th></tr></script>"
      "<p>Visible text</p>"
      "</body>";
  auto result = run_query(html, "SELECT TFIDF(body, TOP_TERMS=10, STOPWORDS=NONE) FROM document");
  expect_eq(result.rows.size(), 1, "tfidf strip markup row count");
  if (!result.rows.empty()) {
    expect_true(result.rows[0].term_scores.count("th") == 0, "tfidf ignores markup tokens");
    expect_true(result.rows[0].term_scores.count("tr") == 0, "tfidf ignores markup tokens");
    expect_true(result.rows[0].term_scores.count("title") == 0, "tfidf ignores script html tokens");
    expect_true(result.rows[0].term_scores.count("visible") == 1, "tfidf keeps visible text");
  }
}

}  // namespace

void register_function_tests(std::vector<TestCase>& tests) {
  tests.push_back({"text_requires_non_tag_filter", test_text_requires_non_tag_filter});
  tests.push_back({"inner_html_function", test_inner_html_function});
  tests.push_back({"minify_html_basic", test_minify_html_basic});
  tests.push_back({"minify_html_preserves_attribute_quotes", test_minify_html_preserves_attribute_quotes});
  tests.push_back({"minify_html_preserves_protected_tags", test_minify_html_preserves_protected_tags});
  tests.push_back({"minify_html_preserves_script_style", test_minify_html_preserves_script_style});
  tests.push_back({"inner_html_depth", test_inner_html_depth});
  tests.push_back({"inner_html_max_depth_auto", test_inner_html_max_depth_auto});
  tests.push_back({"trim_inner_html", test_trim_inner_html});
  tests.push_back({"trim_mixed_with_other_projection", test_trim_mixed_with_other_projection});
  tests.push_back({"inner_html_minified_by_default", test_inner_html_minified_by_default});
  tests.push_back({"raw_inner_html_opt_out", test_raw_inner_html_opt_out});
  tests.push_back({"count_aggregate", test_count_aggregate});
  tests.push_back({"count_star", test_count_star});
  tests.push_back({"summarize_star", test_summarize_star});
  tests.push_back({"summarize_limit", test_summarize_limit});
  tests.push_back({"summarize_order_by_count", test_summarize_order_by_count});
  tests.push_back({"to_table_flag", test_to_table_flag});
  tests.push_back({"to_table_header_on", test_to_table_header_on});
  tests.push_back({"to_table_header_off", test_to_table_header_off});
  tests.push_back({"to_list_flag", test_to_list_flag});
  tests.push_back({"tfidf_output_shape", test_tfidf_output_shape});
  tests.push_back({"tfidf_scoring_top_term", test_tfidf_scoring_top_term});
  tests.push_back({"tfidf_stopwords", test_tfidf_stopwords});
  tests.push_back({"tfidf_strips_html_markup", test_tfidf_strips_html_markup});
}
