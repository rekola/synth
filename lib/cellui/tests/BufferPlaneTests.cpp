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
  CHECK(b.toPlainText() == "\n   abcd\n     xy\n");
}

TEST(bufferplane_grandchild_adds_up_offsets) {
  StyleProvider styles;
  CellBuffer b(5, 10);
  BufferPlane root(b, styles);
  auto child = root.createChild();
  child->move(1, 2);
  auto grand = child->createChild();
  grand->resize(1, 3);
  grand->move(2, 1);
  grand->putstr(0, 0, "ok");
  CHECK(b.at(3, 3).text == "o");
  CHECK(b.at(3, 4).text == "k");
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
  CHECK(b.at(0, 0).fg && b.at(0, 0).fg->getGreen() == 2);
  CHECK(b.at(0, 0).bg && b.at(0, 0).bg->getBlue() == 6);
  CHECK(b.at(0, 0).bold && b.at(0, 0).underline && !b.at(0, 0).italic);
}

TEST(bufferplane_hidden_plane_and_its_children_draw_nothing) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  BufferPlane root(b, styles);
  auto child = root.createChild();
  auto grand = child->createChild();
  child->setVisible(false);
  grand->putstr(0, 0, "x");
  CHECK(b.toPlainText().empty());
  child->setVisible(true);
  grand->putstr(0, 0, "x");
  CHECK(b.toPlainText() == "x\n");
}

TEST(bufferplane_erase_clears_only_its_rectangle) {
  StyleProvider styles;
  CellBuffer b(2, 6);
  BufferPlane root(b, styles);
  root.putstr(0, 0, "aaaaaa");
  root.putstr(1, 0, "bbbbbb");
  auto child = root.createChild();
  child->resize(1, 2);
  child->move(1, 2);
  child->erase();
  CHECK(b.toPlainText() == "aaaaaa\nbb  bb\n");
}

TEST(bufferplane_draws_a_rounded_border) {
  StyleProvider styles;
  CellBuffer b(3, 5);
  BufferPlane root(b, styles);
  root.drawBorder();
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
  CHECK(b.toPlainText() == "\n    title\n    secon\n");
  CHECK(b.at(1, 4).bold);
}
