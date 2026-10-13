#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/InfoDialog.h>

using namespace cellui;

TEST(infodialog_draws_a_titled_box_around_wrapped_markdown) {
  StyleProvider styles;
  CellBuffer b(8, 24);
  BufferPlane root(b, styles);
  InfoDialog d;
  CHECK(!d.isOpen());
  d.show(root, "About", "# Hi\n\nSome *words* here", 20, 6);
  d.move(1, 2);
  root.refresh();
  CHECK(d.isOpen());
  CHECK(d.rows() == 5 && d.width() == 20);
  CHECK(b.toPlainText() ==
        "\n"
        "  ┌─ About ──────────┐\n"
        "  │ Hi               │\n"
        "  │                  │\n"
        "  │ Some words here  │\n"
        "  └──────────────────┘\n");
}

TEST(infodialog_cuts_text_that_does_not_fit_the_rows) {
  StyleProvider styles;
  CellBuffer b(6, 20);
  BufferPlane root(b, styles);
  InfoDialog d;
  d.show(root, "T", "one\n\ntwo\n\nthree", 16, 4);
  d.move(0, 0);
  root.refresh();
  CHECK(d.rows() == 4);
  CHECK(b.toPlainText().find("three") == std::string::npos);
  CHECK(b.toPlainText().find("one") != std::string::npos);
}

TEST(infodialog_bold_and_italic_runs_keep_their_style) {
  StyleProvider styles;
  CellBuffer b(5, 30);
  BufferPlane root(b, styles);
  InfoDialog d;
  d.show(root, "T", "plain **bold** and *italic*", 28, 5);
  d.move(0, 0);
  root.refresh();
  // Row 1: "│ plain bold and italic" - the text starts in column 2.
  CHECK(!b.at(1, 2).bold);
  CHECK(b.at(1, 8).bold);
  CHECK(b.at(1, 17).italic);
}

TEST(infodialog_center_and_close) {
  StyleProvider styles;
  CellBuffer b(10, 30);
  BufferPlane root(b, styles);
  InfoDialog d;
  d.show(root, "T", "x", 10, 6);
  d.center(10, 30);
  root.refresh();
  CHECK(b.at(3, 10).text == "┌");
  d.close();
  root.refresh();
  CHECK(!d.isOpen());
  CHECK(b.toPlainText().empty());
}

TEST(infodialog_italic_ends_with_its_run) {
  StyleProvider styles;
  CellBuffer b(4, 30);
  BufferPlane root(b, styles);
  InfoDialog d;
  d.show(root, "T", "a *b* c", 28, 4);
  root.refresh();
  // Row 1: "│ a b c" - "a" at column 2, "b" at 4, "c" at 6.
  CHECK(!b.at(1, 2).italic);
  CHECK(b.at(1, 4).italic);
  CHECK(!b.at(1, 6).italic);
}
