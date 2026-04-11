#include <vector>

#include "test_harness.h"
#include "test_utils.h"

#include "dom/backend/parser_impl.h"
#include "dom/html_parser.h"

namespace {

const std::string kLegacyFramesHtml =
    "<html><head></head>"
    "<frameset cols='50%,50%'>"
    "<frame src='left.html'>"
    "<frame src='right.html'>"
    "<noframes><div id='fallback'>Fallback</div></noframes>"
    "</frameset>"
    "</html>";

std::vector<const markql::HtmlNode*> find_nodes_by_tag(const markql::HtmlDocument& doc,
                                                       const std::string& tag) {
  std::vector<const markql::HtmlNode*> matches;
  for (const auto& node : doc.nodes) {
    if (node.tag == tag) {
      matches.push_back(&node);
    }
  }
  return matches;
}

void test_naive_parser_keeps_frame_siblings_under_frameset() {
  markql::HtmlDocument doc = markql::parse_html_naive(kLegacyFramesHtml);
  auto framesets = find_nodes_by_tag(doc, "frameset");
  auto frames = find_nodes_by_tag(doc, "frame");
  auto noframes = find_nodes_by_tag(doc, "noframes");

  expect_eq(framesets.size(), 1, "naive frameset count");
  expect_eq(frames.size(), 2, "naive frame count");
  expect_eq(noframes.size(), 1, "naive noframes count");
  if (framesets.size() != 1 || frames.size() != 2 || noframes.size() != 1) return;

  expect_true(frames[0]->parent_id.has_value(), "naive first frame has parent");
  expect_true(frames[1]->parent_id.has_value(), "naive second frame has parent");
  expect_true(noframes[0]->parent_id.has_value(), "naive noframes has parent");
  if (!frames[0]->parent_id.has_value() || !frames[1]->parent_id.has_value() ||
      !noframes[0]->parent_id.has_value()) {
    return;
  }

  expect_true(*frames[0]->parent_id == framesets[0]->id, "naive first frame parent is frameset");
  expect_true(*frames[1]->parent_id == framesets[0]->id, "naive second frame parent is frameset");
  expect_true(*noframes[0]->parent_id == framesets[0]->id, "naive noframes parent is frameset");
}

void test_count_html_nodes_fast_counts_legacy_frames_sample() {
  expect_true(markql::count_html_nodes_naive(kLegacyFramesHtml) == 7,
              "naive node count matches legacy frames sample");
  markql::HtmlDocument parsed = markql::parse_html(kLegacyFramesHtml);
  expect_true(markql::count_html_nodes_fast(kLegacyFramesHtml) ==
                  static_cast<int64_t>(parsed.nodes.size()),
              "fast node count matches parsed document node count");
}

void test_query_frame_rows_remain_siblings() {
  auto result = run_query(kLegacyFramesHtml, "SELECT frame FROM document ORDER BY doc_order");
  expect_eq(result.rows.size(), 2, "query frame row count");
  if (result.rows.size() != 2) return;

  expect_true(result.rows[0].parent_id.has_value(), "query first frame has parent");
  expect_true(result.rows[1].parent_id.has_value(), "query second frame has parent");
  if (!result.rows[0].parent_id.has_value() || !result.rows[1].parent_id.has_value()) return;

  expect_true(*result.rows[0].parent_id == *result.rows[1].parent_id,
              "query frame rows stay siblings");
}

void test_query_frame_parent_tag_is_frameset() {
  auto result = run_query(kLegacyFramesHtml, "SELECT frame FROM document WHERE parent.tag = 'frameset'");
  expect_eq(result.rows.size(), 2, "query frame parent tag count");
}

void test_query_frameset_sees_frame_children() {
  auto result =
      run_query(kLegacyFramesHtml,
                "SELECT frameset FROM document WHERE EXISTS(child WHERE tag = 'frame')");
  expect_eq(result.rows.size(), 1, "query frameset exists child frame");
}

void test_query_noframes_stays_under_frameset() {
  auto result =
      run_query(kLegacyFramesHtml, "SELECT noframes FROM document WHERE parent.tag = 'frameset'");
  expect_eq(result.rows.size(), 1, "query noframes parent tag count");
}

}  // namespace

void register_legacy_frames_tests(std::vector<TestCase>& tests) {
  tests.push_back({"legacy_frames_naive_siblings_under_frameset",
                   test_naive_parser_keeps_frame_siblings_under_frameset});
  tests.push_back({"legacy_frames_fast_node_count", test_count_html_nodes_fast_counts_legacy_frames_sample});
  tests.push_back({"legacy_frames_query_frame_rows_are_siblings", test_query_frame_rows_remain_siblings});
  tests.push_back({"legacy_frames_query_frame_parent_is_frameset", test_query_frame_parent_tag_is_frameset});
  tests.push_back({"legacy_frames_query_frameset_exists_child_frame",
                   test_query_frameset_sees_frame_children});
  tests.push_back({"legacy_frames_query_noframes_parent_is_frameset",
                   test_query_noframes_stays_under_frameset});
}
