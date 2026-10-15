#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/InfoDialog.h>

using namespace cellui;

TEST(infodialog_draws_a_titled_panel_without_a_border_around_wrapped_markdown) {
  StyleProvider styles;
  CellBuffer b(10, 24);
  BufferPlane root(b, styles);
  InfoDialog d;
  CHECK(!d.isOpen());
  d.show(root, "About", "# Hi\n\nSome *words* here", 20, 8);
  d.move(1, 2);
  root.refresh();
  CHECK(d.isOpen());
  CHECK(d.rows() == 7 && d.width() == 20);
  CHECK(b.toPlainText() ==
        "\n\n"
        "    About\n"
        "\n"
        "    Hi\n"
        "\n"
        "    Some words here\n");
  CHECK(b.at(2, 4).bold);
}

TEST(infodialog_cuts_text_that_does_not_fit_the_rows) {
  StyleProvider styles;
  CellBuffer b(6, 20);
  BufferPlane root(b, styles);
  InfoDialog d;
  d.show(root, "T", "one\n\ntwo\n\nthree", 16, 6);
  d.move(0, 0);
  root.refresh();
  CHECK(d.rows() == 6);
  CHECK(b.toPlainText().find("three") == std::string::npos);
  CHECK(b.toPlainText().find("one") != std::string::npos);
}

TEST(infodialog_bold_and_italic_runs_keep_their_style) {
  StyleProvider styles;
  CellBuffer b(8, 30);
  BufferPlane root(b, styles);
  InfoDialog d;
  d.show(root, "T", "plain **bold** and *italic*", 28, 8);
  d.move(0, 0);
  root.refresh();
  // Row 3: "plain bold and italic" - the text starts in column 2.
  CHECK(!b.at(3, 2).bold);
  CHECK(b.at(3, 8).bold);
  CHECK(b.at(3, 17).italic);
}

TEST(infodialog_center_and_close) {
  StyleProvider styles;
  CellBuffer b(10, 30);
  BufferPlane root(b, styles);
  InfoDialog d;
  d.show(root, "T", "x", 10, 6);
  d.center(10, 30);
  root.refresh();
  CHECK(b.at(3, 12).text == "T");
  d.close();
  root.refresh();
  CHECK(!d.isOpen());
  CHECK(b.toPlainText().empty());
  CHECK(!b.at(9, 0).bg.has_value());
}
