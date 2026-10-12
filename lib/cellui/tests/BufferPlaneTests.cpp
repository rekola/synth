#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/Element.h>

using namespace cellui;

TEST(bufferplane_root_covers_the_buffer) {
  StyleProvider styles;
  CellBuffer b(3, 8);
  BufferPlane root(b, styles);
  CHECK(root.getDim() == std::pair(3, 8));
  root.putstr(1, 1, "hi");
  CHECK(b.toPlainText().empty());  // nothing is shown until refresh()
  root.refresh();
  CHECK(b.toPlainText() == "\n hi\n");
}

TEST(bufferplane_child_is_positioned_and_clipped) {
  StyleProvider styles;
  CellBuffer b(4, 10);
  BufferPlane root(b, styles);
  auto child = root.createChild();
  child->resize(2, 4);
  child->move(1, 3);
  child->putstr(0, 0, "abcdefgh");
  child->putstr(1, 2, "xyz");
  child->putstr(2, 0, "out");
  root.refresh();
  CHECK(b.toPlainText() == "\n   abcd\n     xy\n");
}

TEST(bufferplane_moving_a_drawn_plane_moves_its_content) {
  StyleProvider styles;
  CellBuffer b(4, 10);
  BufferPlane root(b, styles);
  auto child = root.createChild();
  child->resize(1, 3);
  child->putstr(0, 0, "abc");
  child->move(2, 5);
  root.refresh();
  CHECK(b.toPlainText() == "\n\n     abc\n");
  child->move(0, 0);
  root.refresh();
  CHECK(b.toPlainText() == "abc\n");
}

TEST(bufferplane_grandchild_adds_up_offsets_and_clips_to_the_parent) {
  StyleProvider styles;
  CellBuffer b(5, 10);
  BufferPlane root(b, styles);
  auto child = root.createChild();
  child->resize(3, 3);
  child->move(1, 2);
  auto grand = child->createChild();
  grand->resize(1, 5);
  grand->move(2, 1);
  grand->putstr(0, 0, "okay!");
  root.refresh();
  CHECK(b.at(3, 3).text == "o");
  CHECK(b.at(3, 4).text == "k");
  CHECK(b.at(3, 5).text == " ");  // the child ends at column 4
}

TEST(bufferplane_carries_style_and_app_context) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  auto context = std::make_shared<int>(42);
  BufferPlane root(b, styles, context);
  auto child = root.createChild();
  CHECK(child->getAppContext() == std::shared_ptr<void>(context));
  CHECK(&child->getStyles() == &styles);
  child->setFgColor(1, 2, 3);
  child->setBgColor(4, 5, 6);
  child->setBold(true);
  child->setUnderline(true);
  child->putstr(0, 0, "a");
  root.refresh();
  CHECK(b.at(0, 0).fg && b.at(0, 0).fg->getGreen() == 2);
  CHECK(b.at(0, 0).bg && b.at(0, 0).bg->getBlue() == 6);
  CHECK(b.at(0, 0).bold && b.at(0, 0).underline && !b.at(0, 0).italic);
}

TEST(bufferplane_hidden_plane_and_its_children_are_not_shown) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  BufferPlane root(b, styles);
  auto child = root.createChild();
  auto grand = child->createChild();
  grand->putstr(0, 0, "x");
  child->setVisible(false);
  root.refresh();
  CHECK(b.toPlainText().empty());
  child->setVisible(true);
  root.refresh();
  CHECK(b.toPlainText() == "x\n");
}

TEST(bufferplane_unwritten_cells_show_what_is_below) {
  StyleProvider styles;
  CellBuffer b(1, 6);
  BufferPlane root(b, styles);
  root.putstr(0, 0, "aaaaaa");
  auto child = root.createChild();
  child->resize(1, 4);
  child->move(0, 1);
  child->putstr(0, 1, "XY");
  root.refresh();
  CHECK(b.toPlainText() == "aaXYaa\n");
}

TEST(bufferplane_raising_a_plane_draws_it_over_its_siblings) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  BufferPlane root(b, styles);
  auto first = root.createChild();
  auto second = root.createChild();
  first->putstr(0, 0, "1111");
  second->putstr(0, 0, "2222");
  root.refresh();
  CHECK(b.toPlainText() == "2222\n");
  first->moveToTop();
  root.refresh();
  CHECK(b.toPlainText() == "1111\n");
}

TEST(bufferplane_destroying_a_plane_removes_it) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  BufferPlane root(b, styles);
  auto child = root.createChild();
  child->putstr(0, 0, "gone");
  root.refresh();
  CHECK(b.toPlainText() == "gone\n");
  child.reset();
  root.refresh();
  CHECK(b.toPlainText().empty());
}

TEST(bufferplane_children_may_outlive_their_parent) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  auto root = std::make_unique<BufferPlane>(b, styles);
  auto child = root->createChild();
  root.reset();
  child.reset();  // must not touch the destroyed parent
  CHECK(true);
}

TEST(bufferplane_erase_clears_only_its_own_plane) {
  StyleProvider styles;
  CellBuffer b(2, 6);
  BufferPlane root(b, styles);
  root.putstr(0, 0, "aaaaaa");
  root.putstr(1, 0, "bbbbbb");
  auto child = root.createChild();
  child->resize(1, 2);
  child->move(1, 2);
  child->putstr(0, 0, "zz");
  child->erase();
  root.refresh();
  CHECK(b.toPlainText() == "aaaaaa\nbbbbbb\n");
}

TEST(bufferplane_resize_keeps_the_overlap) {
  StyleProvider styles;
  CellBuffer b(2, 6);
  BufferPlane root(b, styles);
  auto child = root.createChild();
  child->resize(1, 4);
  child->putstr(0, 0, "abcd");
  child->resize(2, 2);
  root.refresh();
  CHECK(b.toPlainText() == "ab\n");
  child->resize(1, 4);
  root.refresh();
  CHECK(b.toPlainText() == "ab\n");  // the cut cells are gone
}

TEST(bufferplane_wide_character_is_clipped_whole) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  BufferPlane root(b, styles);
  auto child = root.createChild();
  child->resize(1, 2);
  child->putstr(0, 0, "a\xe6\xbc\xa2");  // the wide glyph does not fit in column 1
  root.refresh();
  CHECK(b.toPlainText() == "a\n");
}

TEST(bufferplane_draws_a_rounded_border) {
  StyleProvider styles;
  CellBuffer b(3, 5);
  BufferPlane root(b, styles);
  root.drawBorder();
  root.refresh();
  CHECK(b.toPlainText() == "╭───╮\n│   │\n╰───╯\n");
}

TEST(bufferplane_reader_and_picker_stay_inactive) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  BufferPlane root(b, styles);
  UIPlane & plane = root;
  plane.showReader("prompt");
  root.showPicker(0, 0, 1, 4, 1);
  CHECK(!root.readerActive());
  CHECK(!root.pickerActive());
  CHECK(!root.offerInput(Input('a', 0, 0, false, false, false, false)));
}

TEST(element_drawn_through_a_buffer_plane) {
  StyleProvider styles;
  CellBuffer b(3, 12);
  BufferPlane root(b, styles);
  Element e(root);
  e.resize(2, 5);
  e.move(1, 4);
  e.setBold(true).putstr(0, 0, "title").putstr(1, 0, "second line");
  root.refresh();
  CHECK(b.toPlainText() == "\n    title\n    secon\n");
  CHECK(b.at(1, 4).bold);
}
