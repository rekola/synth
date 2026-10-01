#include "TestFramework.h"

#include "../src/ui/Markdown.h"

using namespace markdown;

TEST(markdown_parses_headings_and_paragraphs) {
  auto doc = parse("# Title\n\nfirst line\nsecond line\n\nlast");
  CHECK(doc.size() == 3);
  CHECK(doc[0].kind == Block::Kind::HEADING);
  CHECK(doc[0].level == 1);
  CHECK(doc[0].spans.size() == 1 && doc[0].spans[0].text == "Title");
  CHECK(doc[1].kind == Block::Kind::PARAGRAPH);
  CHECK(doc[1].spans[0].text == "first line second line");
  CHECK(doc[2].spans[0].text == "last");
}

TEST(markdown_heading_interrupts_paragraph) {
  auto doc = parse("text\n## Sub\nmore");
  CHECK(doc.size() == 3);
  CHECK(doc[1].kind == Block::Kind::HEADING && doc[1].level == 2);
}

TEST(markdown_parses_emphasis) {
  auto doc = parse("a *b* **c** d");
  CHECK(doc.size() == 1);
  auto & spans = doc[0].spans;
  CHECK(spans.size() == 5);
  CHECK(spans[0].text == "a " && !spans[0].bold && !spans[0].italic);
  CHECK(spans[1].text == "b" && spans[1].italic && !spans[1].bold);
  CHECK(spans[3].text == "c" && spans[3].bold && !spans[3].italic);
  CHECK(spans[4].text == " d");
}

TEST(markdown_unmatched_marker_is_literal) {
  auto doc = parse("2 * 3");
  CHECK(doc[0].spans.size() == 1 && doc[0].spans[0].text == "2 * 3");
}

TEST(markdown_escape_round_trips) {
  std::string raw = "# a *b* \\c";
  auto doc = parse(escape(raw));
  CHECK(doc.size() == 1);
  CHECK(doc[0].kind == Block::Kind::PARAGRAPH);
  CHECK(doc[0].spans.size() == 1 && doc[0].spans[0].text == raw);
}

TEST(markdown_layout_wraps_and_separates_blocks) {
  auto lines = layout(parse("# Hi\n\naaa bbb ccc"), 7);
  CHECK(lines.size() == 4);
  CHECK(lines[0][0].text == "Hi" && lines[0][0].bold);
  CHECK(lines[1].empty());
  CHECK(lines[2][0].text == "aaa bbb");
  CHECK(lines[3][0].text == "ccc");
}

TEST(markdown_layout_keeps_styles_and_glues_adjacent_runs) {
  auto lines = layout(parse("**ab**cd *e*"), 20);
  CHECK(lines.size() == 1);
  CHECK(lines[0].size() == 3);
  CHECK(lines[0][0].text == "ab" && lines[0][0].bold);
  CHECK(lines[0][1].text == "cd " && !lines[0][1].bold);
  CHECK(lines[0][2].text == "e" && lines[0][2].italic);
}
