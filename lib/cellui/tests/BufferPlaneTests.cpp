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

TEST(bufferplane_reader_edits_a_field_and_places_the_cursor) {
  StyleProvider styles;
  CellBuffer b(2, 20);
  BufferPlane root(b, styles);
  auto line = root.createChild();
  line->resize(1, 20);
  line->move(1, 0);
  line->putstr(0, 0, "an old long message");
  line->showReader("M-x ");
  CHECK(line->readerActive());
  for (char c : std::string("sav")) line->offerInput(Input(c, -1, -1, false, false, false, false));
  line->offerInput(Input(cellui::Key::Left, -1, -1, false, false, false, false));
  line->offerInput(Input('X', -1, -1, false, true, false, false, Input::Kind::UNKNOWN, 'X'));
  root.refresh();
  CHECK(b.toPlainText() == "\nM-x saXv\n");
  auto & bp = static_cast<BufferPlane &>(*line);
  CHECK(root.cursorPosition() == std::optional(std::pair(1, 7)));
  CHECK(line->getReaderContents() == "saXv");
  line->setReaderContents("save-song");
  line->showReaderIndicator(9, " [Sole completion]");
  root.refresh();
  CHECK(b.toPlainText() == "\nM-x save-song [Sole\n");
  line->hideReaderIndicator();
  CHECK(bp.closeReader() == "save-song");
  CHECK(!line->readerActive() && !root.cursorPosition());
}

TEST(bufferplane_picker_lists_and_wraps) {
  StyleProvider styles;
  CellBuffer b(10, 20);
  BufferPlane root(b, styles);
  root.showPicker(1, 2, 8, 12, 3);
  root.addItem("alpha", "");
  root.addItem("beta", "");
  root.addItem("gamma", "");
  root.refresh();
  CHECK(b.toPlainText() == "\n  ╭────────╮\n  │        │\n  │ alpha  │\n  │  beta  │\n  │ gamma  │\n  │        │\n  ╰────────╯\n");
  CHECK(root.getPickerSelection() == "alpha");
  CHECK(root.offerInput(Input(cellui::Key::Up, -1, -1, false, false, false, false)));
  CHECK(root.getPickerSelection() == "gamma");
  root.selectPickerItem(1);
  CHECK(root.getPickerSelection() == "beta");
  // A click on a row selects it; Enter is the owner's.
  CHECK(root.offerInput(Input(cellui::Key::Button1, 3, 5, false, false, false, false, Input::Kind::RELEASE)));
  CHECK(root.getPickerSelection() == "alpha");
  CHECK(!root.offerInput(Input(cellui::Key::Enter, -1, -1, false, false, false, false)));
  root.closePicker();
  CHECK(!root.pickerActive());
}

TEST(bufferplane_translucent_cells_blend_with_what_is_below) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  BufferPlane root(b, styles);
  root.setBgColor(0x80, 0x80, 0x80);
  root.putstr(0, 0, "....");
  auto top = root.createChild();
  top->setBgColor(0xff, 0, 0);
  top->setFgColor(0xff, 0xff, 0xff);
  top->setBgBlend(true);
  top->putstr(0, 0, "x");
  top->setBgBlend(false);
  top->setFgContrast(true);
  top->setFgColor(0x40, 0x40, 0x40);
  top->setBgColor(0, 0, 0);
  top->putstr(0, 1, "y");
  root.refresh();
  CHECK(b.at(0, 0).text == "x" && b.at(0, 0).bg->getRed() == 0xbf && b.at(0, 0).bg->getGreen() == 0x40);
  CHECK(b.at(0, 1).fg->getRed() == (3 * 255 + 0x40) / 4);
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

TEST(bufferplane_display_scale_is_the_roots) {
  StyleProvider styles;
  CellBuffer b(2, 2);
  BufferPlane root(b, styles);
  auto child = root.createChild();
  CHECK(child->displayScale() == 1.0);
  root.setDisplayScale(2.0);
  CHECK(child->displayScale() == 2.0);
}

TEST(bufferplane_flat_children_are_the_roots) {
  StyleProvider styles;
  CellBuffer b(4, 10);
  BufferPlane root(b, styles);
  root.setFlatChildren(true);
  auto panel = root.createChild();
  panel->resize(4, 3);
  auto later = root.createChild();
  later->resize(1, 10);
  later->move(2, 0);
  later->putstr(0, 0, "..........");
  // A popup asked of the narrow panel is placed on the screen, unclipped,
  // above the plane created after the panel.
  auto popup = panel->createChild();
  popup->resize(1, 6);
  popup->move(2, 2);
  popup->putstr(0, 0, "popup!");
  root.refresh();
  CHECK(b.toPlainText() == "\n\n..popup!..\n");
}

TEST(bufferplane_themed_base_colours_what_a_child_leaves_unwritten) {
  StyleProvider styles;
  CellBuffer b(1, 6);
  BufferPlane root(b, styles);
  root.setThemedBases(true);
  root.putstr(0, 0, "abcdef");
  auto widget = root.createChild();
  widget->resize(1, 3);
  widget->putstr(0, 0, "X");
  auto overlay = root.createChild();
  overlay->resize(1, 2);
  overlay->move(0, 4);
  overlay->makeTransparent();
  root.refresh();
  // The glyph below shows through, on the widget's base.
  CHECK(b.toPlainText() == "Xbcdef\n");
  CHECK(b.at(0, 1).bg && b.at(0, 1).bg->getRed() == styles.plane_base_bg_color.getRed() && b.at(0, 1).bg->getBlue() == styles.plane_base_bg_color.getBlue());
  CHECK(!b.at(0, 3).bg && !b.at(0, 4).bg);
}

TEST(bufferplane_image_at_a_cell_covers_only_its_cells) {
  StyleProvider styles;
  CellBuffer b(3, 6);
  BufferPlane root(b, styles);
  root.setCellPixelSize(4, 2);
  root.putstr(0, 0, "abcdef");
  root.putstr(1, 0, "ghijkl");
  int h = 0, w = 0;
  CHECK(root.cellPixelSize(h, w) && h == 4 && w == 2);
  // Two cells wide, one tall, from (1, 2); then redrawn there.
  root.drawImageAt(1, 2, std::vector<uint32_t>(4 * 4, 0xff0000ffu), 4, 4);
  root.drawImageAt(1, 2, std::vector<uint32_t>(4 * 4, 0xff00ff00u), 4, 4);
  root.refresh();
  CHECK(root.images().size() == 1);
  CHECK(b.at(1, 2).image == 1 && b.at(1, 3).image == 1);
  CHECK(!b.at(1, 1).image && !b.at(1, 4).image && !b.at(0, 2).image);
  CHECK(b.at(0, 2).text == "c" && b.at(1, 4).text == "k");
  root.erase();
  root.refresh();
  CHECK(root.images().empty());
}

TEST(bufferplane_the_same_image_again_keeps_its_version) {
  StyleProvider styles;
  CellBuffer b(2, 2);
  BufferPlane root(b, styles);
  root.setCellPixelSize(4, 2);
  root.drawImage(std::vector<uint32_t>(4 * 8, 0xff0000ffu), 4, 8);
  root.refresh();
  auto version = root.images()[0].image->version;
  root.drawImage(std::vector<uint32_t>(4 * 8, 0xff0000ffu), 4, 8);
  root.refresh();
  CHECK(root.images()[0].image->version == version);
  root.drawImage(std::vector<uint32_t>(4 * 8, 0xff00ff00u), 4, 8);
  root.refresh();
  CHECK(root.images()[0].image->version != version);
}

TEST(bufferplane_an_erased_image_drawn_again_is_the_same_image) {
  StyleProvider styles;
  CellBuffer b(2, 2);
  BufferPlane root(b, styles);
  root.setCellPixelSize(4, 2);
  root.drawImageAt(1, 0, std::vector<uint32_t>(4 * 4, 0xff0000ffu), 4, 4);
  root.refresh();
  auto first = root.images()[0];
  root.erase();
  root.drawImageAt(1, 0, std::vector<uint32_t>(4 * 4, 0xff0000ffu), 4, 4);
  root.refresh();
  CHECK(root.images().size() == 1 && root.images()[0].key == first.key && root.images()[0].image->version == first.image->version);
}

TEST(bufferplane_describe_gives_the_tree_in_stacking_order) {
  StyleProvider styles;
  CellBuffer b(6, 12);
  BufferPlane root(b, styles);
  auto a = root.createChild();
  auto c = root.createChild();
  a->resize(2, 3);
  c->resize(1, 1);
  c->move(4, 5);
  c->setVisible(false);
  a->moveToTop();
  auto info = root.describe();
  CHECK(info.children.size() == 2);
  CHECK(info.children[0].y == 4 && info.children[0].x == 5);
  CHECK(!info.children[0].visible);
  CHECK(info.children[1].rows == 2 && info.children[1].cols == 3);
}
