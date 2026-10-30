#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/Widget.h>

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
  CHECK(b.toPlainText() == "\n\n    alpha\n     beta\n    gamma\n");
  CHECK(b.at(2, 2).bg && b.at(1, 2).bg);  // opaque, padding rows included
  CHECK(root.getPickerSelection() == "alpha");
  CHECK(root.offerInput(Input(cellui::Key::Up, -1, -1, false, false, false, false)));
  CHECK(root.getPickerSelection() == "gamma");
  root.selectPickerItem(1);
  CHECK(root.getPickerSelection() == "beta");
  // A click on a row selects it; Enter is the owner's.
  CHECK(root.offerInput(Input(cellui::Key::Button1, 2, 5, false, false, false, false, Input::Kind::RELEASE)));
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
  // Half and half in linear light, and three parts white to one of the text.
  CHECK(b.at(0, 0).text == "x" && b.at(0, 0).bg->toSrgb8() == Color(0xff, 0, 0).mix(Color(0x80, 0x80, 0x80), 0.5f).toSrgb8());
  CHECK(b.at(0, 1).fg->toSrgb8() == Color(255, 255, 255).mix(Color(0x40, 0x40, 0x40), 0.25f).toSrgb8());
}

TEST(element_drawn_through_a_buffer_plane) {
  StyleProvider styles;
  CellBuffer b(3, 12);
  BufferPlane root(b, styles);
  Widget e(root);
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

TEST(bufferplane_an_image_that_moved_keeps_its_key_and_version) {
  StyleProvider styles;
  CellBuffer b(4, 4);
  BufferPlane root(b, styles);
  root.setCellPixelSize(4, 2);
  root.drawImageAt(0, 0, std::vector<uint32_t>(4 * 4, 0xff0000ffu), 4, 4);
  root.refresh();
  auto first = root.images()[0];
  root.erase();
  root.drawImageAt(2, 1, std::vector<uint32_t>(4 * 4, 0xff0000ffu), 4, 4);
  root.refresh();
  CHECK(root.images().size() == 1 && root.images()[0].row == 2 && root.images()[0].col == 1);
  CHECK(root.images()[0].key == first.key && root.images()[0].image->version == first.image->version);
}

TEST(bufferplane_image_ids_name_the_picture_whatever_its_pixels) {
  StyleProvider styles;
  CellBuffer b(4, 4);
  BufferPlane root(b, styles);
  root.setCellPixelSize(4, 2);
  root.drawImageAt(0, 0, std::vector<uint32_t>(4 * 4, 0xff0000ffu), 4, 4, 7);
  root.refresh();
  auto first = root.images()[0];
  // The same id with other pixels, moved: the same image, changed.
  root.erase();
  root.drawImageAt(2, 0, std::vector<uint32_t>(4 * 4, 0xff00ff00u), 4, 4, 7);
  root.refresh();
  CHECK(root.images()[0].key == first.key && root.images()[0].image->version != first.image->version);
  // Another id with the same pixels is another image.
  root.erase();
  root.drawImageAt(0, 0, std::vector<uint32_t>(4 * 4, 0xff00ff00u), 4, 4, 8);
  root.refresh();
  CHECK(root.images()[0].key != first.key);
}

TEST(bufferplane_two_identical_pictures_stay_two_images) {
  StyleProvider styles;
  CellBuffer b(4, 4);
  BufferPlane root(b, styles);
  root.setCellPixelSize(4, 2);
  for (int i = 0; i < 2; i++) {
    root.drawImageAt(0, 0, std::vector<uint32_t>(4 * 4, 0xff0000ffu), 4, 4);
    root.drawImageAt(2, 0, std::vector<uint32_t>(4 * 4, 0xff0000ffu), 4, 4);
    root.refresh();
    CHECK(root.images().size() == 2 && root.images()[0].key != root.images()[1].key);
    root.erase();
  }
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

namespace {

// A plane over the whole screen made of nothing but backdrop cells.
std::unique_ptr<Plane> scrimOver(BufferPlane & root, const Backdrop & backdrop) {
  auto scrim = root.createChild();
  auto [rows, cols] = root.getDim();
  scrim->setBackdrop(backdrop);
  for (int r = 0; r < rows; r++) scrim->putstr(r, 0, std::string(static_cast<size_t>(cols), ' '));
  return scrim;
}

}  // namespace

// What lies below a backdrop is set aside as a layer for the display to
// apply, and the frame holds only what is above.
TEST(bufferplane_backdrop_sets_the_layer_below_aside) {
  StyleProvider styles;
  CellBuffer b(2, 6);
  BufferPlane root(b, styles);
  root.putstr(0, 0, "below");
  auto scrim = scrimOver(root, Backdrop{2, 0.3f});
  auto dialog = root.createChild();
  dialog->resize(1, 3);
  dialog->move(1, 1);
  dialog->putstr(0, 0, "top");
  root.refresh();

  auto layers = root.takeBackdropLayers();
  CHECK(layers.size() == 1);
  CHECK(layers[0].cells.toPlainText() == "below\n");
  CHECK(layers[0].backdrop.blur == 2 && layers[0].backdrop.dim == 0.3f);
  CHECK(layers[0].row == 0 && layers[0].col == 0 && layers[0].rows == 2 && layers[0].cols == 6);
  // The frame has the dialog alone, and nothing of the backdrop's own cells.
  CHECK(b.toPlainText() == "\n top\n");
  CHECK(!b.at(0, 0).backdrop.active() && CellBuffer::isBlank(b.at(0, 0)));
  // Taken: a refresh without a backdrop sets nothing aside.
  scrim.reset();
  root.refresh();
  CHECK(root.takeBackdropLayers().empty());
}



TEST(bufferplane_a_plane_shifted_by_part_of_a_cell_is_a_scroll_layer) {
  StyleProvider styles;
  CellBuffer b(6, 10);
  BufferPlane root(b, styles);
  root.setSmoothScroll(true);
  root.putstr(0, 0, "below");
  auto list = root.createChild();
  list->resize(3, 6);
  list->move(2, 1);
  list->setScrollOffset(0.25, 0);
  // One row and column more than it shows.
  list->putstr(0, 0, "aaaaaa");
  list->putstr(3, 0, "dddddd");
  list->putstr(1, 6, "x");
  auto above = root.createChild();
  above->resize(1, 3);
  above->move(3, 2);
  above->putstr(0, 0, "top");
  root.refresh();
  auto layers = root.takeBackdropLayers();
  CHECK(layers.size() == 1 && layers[0].scroll);
  const auto & scroll = *layers[0].scroll;
  CHECK(scroll.row == 2 && scroll.col == 1 && scroll.rows == 3 && scroll.cols == 6);
  CHECK(scroll.shift_rows == 0.25 && scroll.shift_cols == 0.0);
  CHECK(scroll.cells.rows() == 4 && scroll.cells.cols() == 7);
  CHECK(scroll.cells.at(0, 0).text == "a" && scroll.cells.at(3, 0).text == "d" && scroll.cells.at(1, 6).text == "x");
  CHECK(!layers[0].backdrop.active());
  // What was below is the layer's; the frame has only what lies above, and
  // nothing of the shifted plane.
  CHECK(layers[0].cells.at(0, 0).text == "b");
  CHECK(b.at(2, 1).text == " " && b.at(3, 2).text == "t" && b.at(0, 0).text == " ");
}

TEST(bufferplane_a_plane_is_not_shifted_where_the_display_cannot) {
  StyleProvider styles;
  CellBuffer b(4, 8);
  BufferPlane root(b, styles);
  auto list = root.createChild();
  list->resize(2, 4);
  list->setScrollOffset(0.5, 0);
  list->putstr(0, 0, "abcd");
  root.refresh();
  CHECK(root.takeBackdropLayers().empty());
  CHECK(b.at(0, 0).text == "a");
}

TEST(bufferplane_a_plane_with_no_shift_left_composes_in_place_again) {
  StyleProvider styles;
  CellBuffer b(4, 8);
  BufferPlane root(b, styles);
  root.setSmoothScroll(true);
  auto list = root.createChild();
  list->resize(2, 4);
  list->setScrollOffset(0.5, 0);
  list->putstr(0, 0, "abcd");
  list->putstr(2, 0, "over");  // the overscan row
  root.refresh();
  CHECK(root.takeBackdropLayers().size() == 1);
  list->setScrollOffset(0, 0);
  root.refresh();
  CHECK(root.takeBackdropLayers().empty());
  CHECK(b.at(0, 0).text == "a" && b.at(1, 0).text == " " && b.at(2, 0).text == " ");
}

TEST(bufferplane_a_band_over_a_plane_goes_with_its_scroll_layer) {
  StyleProvider styles;
  CellBuffer b(6, 10);
  BufferPlane root(b, styles);
  auto list = root.createChild();
  list->resize(3, 6);
  list->move(2, 1);
  list->putstr(0, 0, "abcdef");
  // Not where the display cannot show it.
  list->setBand(Band{ 1.5, 0.1, 0, 6, Color(255, 0, 0), 1.0f });
  root.refresh();
  CHECK(root.takeBackdropLayers().empty());
  root.setSmoothScroll(true);
  list->setBand(Band{ 1.5, 0.1, 0, 6, Color(255, 0, 0), 0.5f });
  root.refresh();
  auto layers = root.takeBackdropLayers();
  // A scroll layer with no shift, only for the band.
  CHECK(layers.size() == 1 && layers[0].scroll && layers[0].scroll->shift_rows == 0.0);
  CHECK(layers[0].scroll->band && layers[0].scroll->band->row == 1.5 && layers[0].scroll->band->alpha == 0.5f);
  CHECK(layers[0].scroll->cells.at(0, 0).text == "a");
  list->clearBand();
  root.refresh();
  CHECK(root.takeBackdropLayers().empty() && b.at(2, 1).text == "a");
}

// COLOR gives what is below the top's hue and keeps its brightness: an inverted
// cell takes the highlight's hue back (the separable modes cannot do it).
TEST(blend_color_keeps_the_brightness_of_what_is_below) {
  auto lum = [](const Color & c) { return 0.3 * c.getRed() + 0.59 * c.getGreen() + 0.11 * c.getBlue(); };
  Color below(40, 30, 20), top(60, 160, 200);
  Color mixed = blendColors(BlendMode::COLOR, below, top, 1.0f);
  CHECK(std::abs(lum(mixed) - lum(below)) < 3.0);
  CHECK(mixed.getBlue() > mixed.getRed());  // the top's blue-ish hue
  // LUMINOSITY is the other way round: the top's brightness, the hue below.
  Color bright = blendColors(BlendMode::LUMINOSITY, below, top, 1.0f);
  CHECK(std::abs(lum(bright) - lum(top)) < 3.0);
  CHECK(bright.getRed() > bright.getBlue());
  // Difference inverts each channel: a cyan-tinted dark cell comes out warm.
  Color inverted = blendColors(BlendMode::DIFFERENCE, Color(10, 40, 50), Color(200, 200, 200), 1.0f);
  CHECK(inverted.getRed() > inverted.getBlue());
}

TEST(bufferplane_a_band_keeps_its_blend_mode) {
  StyleProvider styles;
  CellBuffer b(6, 10);
  BufferPlane root(b, styles);
  root.setSmoothScroll(true);
  auto list = root.createChild();
  list->resize(3, 6);
  list->move(2, 1);
  list->putstr(0, 0, "abcdef");
  list->setBand(Band{ 1.0, 1.0, 0, 6, Color(255, 255, 255), 1.0f, BlendMode::DIFFERENCE });
  root.refresh();
  auto layers = root.takeBackdropLayers();
  CHECK(layers.size() == 1 && layers[0].scroll && layers[0].scroll->band);
  CHECK(layers[0].scroll->band->blend == BlendMode::DIFFERENCE);
  list->setBand(Band{ 1.0, 1.0, 0, 6, Color(255, 255, 255), 1.0f });
  root.refresh();
  layers = root.takeBackdropLayers();
  CHECK(layers.size() == 1 && layers[0].scroll->band->blend == BlendMode::NORMAL);
}

// A border needs a display that draws bands by parts of a cell: it reaches
// the layer there, and leaves the cells alone otherwise.
TEST(bufferplane_a_band_border_is_for_smooth_displays_only) {
  StyleProvider styles;
  CellBuffer b(4, 8);
  BufferPlane root(b, styles);
  auto list = root.createChild();
  list->resize(2, 4);
  list->move(1, 2);
  list->setBgColor(10, 20, 30);
  list->putstr(0, 0, "abcd");
  Band bordered{ 0.0, 1.0, 0, 4, Color(255, 0, 0), 0.0f, BlendMode::NORMAL, 0.0f, Color(0, 255, 0), 1.0f, 2.0 };
  root.refresh();
  auto plain = b.at(1, 2);
  list->setBand(bordered);
  root.refresh();
  CHECK(root.takeBackdropLayers().empty());
  CHECK(b.at(1, 2).bg == plain.bg && b.at(1, 2).fg == plain.fg);
  root.setSmoothScroll(true);
  root.refresh();
  auto layers = root.takeBackdropLayers();
  CHECK(layers.size() == 1 && layers[0].scroll && layers[0].scroll->band);
  CHECK(layers[0].scroll->band->border_px == 2.0 && layers[0].scroll->band->border_color.getGreen() == 255);
}

// The border leaves out the span of something drawn inside it: along the top
// and bottom, and at a side the span reaches.
TEST(band_border_leaves_out_a_skipped_span) {
  Band band{ 0.0, 1.0, 2, 10, Color(255, 0, 0), 0.0f, BlendMode::NORMAL, 0.0f, Color(0, 255, 0), 1.0f, 1.0 };
  auto whole = bandBorderRects(band, 20, 0, 0, 10, 20);
  CHECK(whole.size() == 4);
  // The span starts at the left edge: no left side, and the top and bottom start after it.
  band.border_skip_col = 2;
  band.border_skip_cols = 3;
  auto left = bandBorderRects(band, 20, 0, 20, 10, 20);
  CHECK(left.size() == 3);
  for (const auto & r : left) CHECK(r.x >= 20 + 30 - 0.001 || r.x == 20 + 100 - 1);
  // In the middle: both sides stay and the top and bottom are cut in two.
  band.border_skip_col = 5;
  auto middle = bandBorderRects(band, 20, 0, 50, 10, 20);
  CHECK(middle.size() == 6);
}

static double coveredArea(const BandBorderShape & shape) {
  double area = 0;
  for (const auto & r : shape.rects) area += r.w * r.h;
  return area;
}

// A round corner takes over the square the straight pieces stop short of, so
// together they cover the outline once: the pieces shrink by the radius, and
// with no radius nothing changes.
TEST(band_border_rounds_its_corners) {
  Band band{ 0.0, 1.0, 2, 10, Color(255, 0, 0), 0.0f, BlendMode::NORMAL, 0.0f, Color(0, 255, 0), 1.0f, 1.0 };
  // 100 x 20 pixels, a pixel wide, at (20, 0).
  auto square = bandBorder(band, 20, 0, 0, 10, 20);
  CHECK(square.corners.empty() && square.rects.size() == 4);
  band.border_radius = 3;
  auto round = bandBorder(band, 20, 0, 0, 10, 20);
  CHECK(round.corners.size() == 4 && round.rects.size() == 4);
  CHECK(round.thickness == 1.0);
  for (const auto & c : round.corners) CHECK(c.size == 3);
  // Top and bottom lose 3 at each end, the sides 3 at each end instead of the 1 the edges took.
  CHECK(std::abs(coveredArea(round) - (2 * (100 - 6) * 1.0 + 2 * (20 - 6) * 1.0)) < 1e-9);
  CHECK(std::abs(coveredArea(square) - (2 * 100 * 1.0 + 2 * (20 - 2) * 1.0)) < 1e-9);
  // No more than half the height, so the two corners of a side do not overlap.
  band.border_radius = 50;
  auto clamped = bandBorder(band, 20, 0, 0, 10, 20);
  CHECK(clamped.corners.size() == 4);
  for (const auto & c : clamped.corners) CHECK(c.size == 10);
  for (const auto & r : clamped.rects) CHECK(r.w > 0 && r.h > 0);
  // The square version never has corners, whatever the radius.
  CHECK(bandBorderRects(band, 20, 0, 0, 10, 20).size() == 4);
}

// A corner is round only where the border runs on both sides of it.
TEST(band_border_rounds_only_corners_it_has) {
  Band band{ 0.0, 1.0, 2, 10, Color(255, 0, 0), 0.0f, BlendMode::NORMAL, 0.0f, Color(0, 255, 0), 1.0f, 1.0 };
  band.border_radius = 3;
  // A span from the left edge: no left side, so only the right corners.
  band.border_skip_col = 2;
  band.border_skip_cols = 3;
  auto left = bandBorder(band, 20, 0, 20, 10, 20);
  CHECK(left.corners.size() == 2);
  for (const auto & c : left.corners) CHECK(c.which == BandCorner::TOP_RIGHT || c.which == BandCorner::BOTTOM_RIGHT);
  // In the middle: both sides, and each run of the top and bottom rounds its outer end only.
  band.border_skip_col = 5;
  auto middle = bandBorder(band, 20, 0, 50, 10, 20);
  CHECK(middle.corners.size() == 4 && middle.rects.size() == 6);
  // The ends beside the span stay square: no piece comes within the span.
  for (const auto & r : middle.rects) CHECK(r.x + r.w <= 50 + 1e-9 || r.x >= 80 - 1e-9);
  // A run shorter than the arc (here 8 pixels against 10) stays square.
  band.border_radius = 40;
  auto tight = bandBorder(band, 20, 0, 28, 10, 20);
  CHECK(tight.corners.size() == 2);
  for (const auto & c : tight.corners) CHECK(c.which == BandCorner::TOP_RIGHT || c.which == BandCorner::BOTTOM_RIGHT);
  // Where the border does not reach, nothing is rounded.
  band.border_px = 0;
  auto none = bandBorder(band, 20, 0, 50, 10, 20);
  CHECK(none.rects.empty() && none.corners.empty());
}

// Where the display cannot draw a band by parts of a cell, it is laid over the
// cells it reaches, blended: a difference with white inverts text and background.
TEST(bufferplane_a_band_on_a_display_without_smooth_scrolling_blends_the_cells) {
  StyleProvider styles;
  CellBuffer b(4, 8);
  BufferPlane root(b, styles);
  root.setDefaultColors(Color(255, 255, 255), Color(0, 0, 0));
  auto list = root.createChild();
  list->resize(2, 4);
  list->move(1, 2);
  list->setFgColor(200, 100, 50);
  list->setBgColor(10, 20, 30);
  list->putstr(0, 0, "abcd");
  list->putstr(1, 0, "efgh");
  auto is = [](const std::optional<Color> & c, int r, int g, int bl) { return c && c->getRed() == r && c->getGreen() == g && c->getBlue() == bl; };
  list->setBand(Band{ 0.0, 1.0, 1.0, 2.0, Color(255, 255, 255), 1.0f, BlendMode::DIFFERENCE });
  root.refresh();
  CHECK(root.takeBackdropLayers().empty());
  // Inside the band: inverted; beside it and below it: as drawn.
  CHECK(b.at(1, 3).text == "b" && is(b.at(1, 3).bg, 245, 235, 225) && is(b.at(1, 3).fg, 55, 155, 205));
  CHECK(is(b.at(1, 4).bg, 245, 235, 225));
  CHECK(is(b.at(1, 2).bg, 10, 20, 30) && is(b.at(1, 5).bg, 10, 20, 30));
  CHECK(is(b.at(2, 3).bg, 10, 20, 30));
  // A half-transparent normal band tints; the fractional position snaps to the nearest cell.
  list->setBand(Band{ 0.6, 1.0, 0.0, 1.0, Color(255, 0, 0), 1.0f, BlendMode::NORMAL });
  root.refresh();
  root.takeBackdropLayers();
  CHECK(is(b.at(2, 2).bg, 255, 0, 0) && is(b.at(1, 2).bg, 10, 20, 30));
}

// Any plane can lay itself over the planes below with a blend: the colours its
// cells set tint what is there, text below included; its own text is not drawn.
TEST(bufferplane_a_blended_plane_tints_what_is_below) {
  StyleProvider styles;
  CellBuffer b(3, 6);
  BufferPlane root(b, styles);
  root.setDefaultColors(Color(255, 255, 255), Color(0, 0, 0));
  auto below = root.createChild();
  below->resize(3, 6);
  below->setFgColor(200, 100, 50);
  below->setBgColor(10, 20, 30);
  below->putstr(1, 0, "abcdef");
  auto over = root.createChild();
  over->resize(1, 2);
  over->move(1, 2);
  over->makeTransparent();
  over->setBgColor(255, 255, 255);
  over->setFgColor(0, 255, 0);
  over->putstr(0, 0, "xy");
  over->setBlend(BlendMode::DIFFERENCE);
  root.refresh();
  auto is = [](const std::optional<Color> & c, int r, int g, int bl) { return c && c->getRed() == r && c->getGreen() == g && c->getBlue() == bl; };
  CHECK(b.at(1, 2).text == "c" && is(b.at(1, 2).bg, 245, 235, 225) && is(b.at(1, 2).fg, 55, 155, 205));
  CHECK(is(b.at(1, 3).bg, 245, 235, 225));
  CHECK(is(b.at(1, 1).bg, 10, 20, 30) && is(b.at(1, 4).bg, 10, 20, 30));
  over->setBlend(BlendMode::NORMAL, 1.0f);
  root.refresh();
  CHECK(b.at(1, 2).text == "x");
}

// On a display that scrolls by parts of a cell, a blended plane is a layer for
// the display to blend.
TEST(bufferplane_a_blended_plane_is_a_layer_on_a_smooth_display) {
  StyleProvider styles;
  CellBuffer b(3, 6);
  BufferPlane root(b, styles);
  root.setSmoothScroll(true);
  auto over = root.createChild();
  over->resize(1, 3);
  over->move(1, 1);
  over->makeTransparent();
  over->setBgColor(255, 255, 255);
  over->putstr(0, 0, "   ");
  over->setBlend(BlendMode::MULTIPLY, 0.5f);
  root.refresh();
  auto layers = root.takeBackdropLayers();
  CHECK(layers.size() == 1 && layers[0].scroll && layers[0].scroll->blended());
  CHECK(layers[0].scroll->blend == BlendMode::MULTIPLY && layers[0].scroll->blend_alpha == 0.5f);
}

TEST(bufferplane_higher_planes_draw_over_lower_ones_wherever_they_are_in_the_tree) {
  StyleProvider styles;
  CellBuffer b(1, 6);
  BufferPlane root(b, styles);
  auto high = root.createChild();
  high->setName("high");
  high->resize(1, 3);
  high->setElevation(8);
  high->putstr(0, 0, "HHH");
  // Created later, so above in tree order, but lower.
  auto low = root.createChild();
  low->resize(1, 6);
  low->putstr(0, 0, "llllll");
  root.refresh();
  CHECK(b.toPlainText() == "HHHlll\n");

  // One height: tree order, as before elevation.
  high->setElevation(0);
  root.refresh();
  CHECK(b.toPlainText() == "llllll\n");

  // A child adds its elevation to its parent's, and stays clipped to it.
  auto parent = root.createChild();
  parent->resize(1, 2);
  parent->move(0, 4);
  parent->setElevation(2);
  parent->putstr(0, 0, "pp");
  low->setElevation(4);
  root.refresh();
  CHECK(b.toPlainText() == "llllll\n");
  auto child = parent->createChild();
  child->resize(1, 4);
  child->setElevation(3);
  child->putstr(0, 0, "cccc");
  root.refresh();
  CHECK(b.toPlainText() == "llllcc\n");

  // A negative elevation sinks a plane below the base.
  low->setElevation(-1);
  child->setElevation(0);
  root.refresh();
  CHECK(b.toPlainText() == "HHHlcc\n");
}

TEST(bufferplane_reports_the_areas_above_the_base) {
  StyleProvider styles;
  CellBuffer b(5, 10);
  BufferPlane root(b, styles);
  auto panel = root.createChild();
  panel->setName("panel");
  panel->resize(3, 6);
  panel->move(1, 1);
  panel->setElevation(2);
  auto popup = panel->createChild();
  popup->setName("popup");
  popup->resize(4, 4);  // reaches past the panel: only the part inside counts
  popup->move(1, 3);
  popup->setElevation(8);
  auto flat = root.createChild();
  flat->resize(1, 1);
  root.refresh();
  auto areas = root.takeElevations();
  CHECK(areas.size() == 2);
  CHECK(areas[0].name == "panel" && areas[0].row == 1 && areas[0].col == 1 && areas[0].rows == 3 && areas[0].cols == 6);
  CHECK(areas[0].elevation == 2);
  CHECK(areas[1].name == "popup" && areas[1].row == 2 && areas[1].col == 4 && areas[1].rows == 2 && areas[1].cols == 3);
  CHECK(areas[1].elevation == 10);
  CHECK(root.describe().children[0].elevation == 2);
  panel->setVisible(false);
  root.refresh();
  CHECK(root.takeElevations().empty());
}

// A plane asks for a pointer shape over its rectangle; a plane that does not
// shows what is below it, and a later plane wins where they overlap.
TEST(plane_mouse_cursor_covers_its_rectangle_and_the_last_plane_wins) {
  CellBuffer buffer(10, 20);
  StyleProvider styles;
  BufferPlane root(buffer, styles);
  auto grid = root.createChild();
  grid->resize(6, 10);
  grid->move(1, 1);
  grid->setMouseCursor(MouseCursor::POINTER);
  auto plain = root.createChild();
  plain->resize(2, 2);
  plain->move(2, 2);  // asks for nothing: the grid's shape shows through
  auto dialog = root.createChild();
  dialog->resize(2, 3);
  dialog->move(4, 4);
  dialog->setMouseCursor(MouseCursor::DEFAULT);
  root.refresh();
  auto areas = root.takeMouseCursors();
  CHECK(areas.size() == 2);
  CHECK(mouseCursorAt(areas, 0, 0) == MouseCursor::DEFAULT);
  CHECK(mouseCursorAt(areas, 1, 1) == MouseCursor::POINTER);
  CHECK(mouseCursorAt(areas, 2, 2) == MouseCursor::POINTER);
  CHECK(mouseCursorAt(areas, 4, 5) == MouseCursor::DEFAULT);
  CHECK(mouseCursorAt(areas, 6, 5) == MouseCursor::POINTER);
  CHECK(mouseCursorAt(areas, 7, 5) == MouseCursor::DEFAULT);
  CHECK(root.describe().children[0].mouse_cursor == MouseCursor::POINTER);
  grid->setVisible(false);
  root.refresh();
  CHECK(root.takeMouseCursors().size() == 1);
}

// Lifted cells stand above their plane: each run in a row is a piece, joined
// to the run above only when asked, and covered cells cast nothing.
TEST(bufferplane_reports_lifted_cells_as_pieces) {
  StyleProvider styles;
  CellBuffer b(5, 10);
  BufferPlane root(b, styles);
  auto plane = root.createChild();
  plane->setName("plane");
  plane->resize(5, 10);
  plane->setElevation(1);
  plane->setLift(0.5f);
  plane->putstr(0, 0, "aaa");
  plane->setLift(0.5f, true);
  plane->putstr(1, 0, "bbb");  // one piece with the row above
  plane->setLift(0.5f);
  plane->putstr(2, 0, "ccc");  // a piece of its own
  plane->setLift(0);
  plane->putstr(2, 1, "x");    // splits it
  plane->setLift(1);
  plane->putstr(4, 0, "dd");
  plane->setLift(0);
  auto cover = root.createChild();
  cover->setName("cover");
  cover->resize(1, 1);
  cover->move(4, 1);
  cover->setElevation(1);
  cover->putstr(0, 0, "#");
  root.refresh();
  std::vector<ElevatedArea> lifted;
  for (const auto & area : root.takeElevations()) {
    if (area.name.empty()) lifted.push_back(area);
  }
  CHECK(lifted.size() == 4);
  auto at = [&](int row, int col, int rows, int cols, float elevation) {
    return std::any_of(lifted.begin(), lifted.end(), [&](const ElevatedArea & a) {
      return a.row == row && a.col == col && a.rows == rows && a.cols == cols && a.elevation == elevation;
    });
  };
  CHECK(at(0, 0, 2, 3, 1.5f));
  CHECK(at(2, 0, 1, 1, 1.5f));
  CHECK(at(2, 2, 1, 1, 1.5f));
  CHECK(at(4, 0, 1, 1, 2.0f));
}

// An elevated plane with a corner radius reports it, and what lay under each
// corner before the plane covered it.
TEST(bufferplane_reports_corner_radius_and_what_lies_under_corners) {
  StyleProvider styles;
  CellBuffer b(6, 10);
  BufferPlane root(b, styles);
  root.setBgColor(0x11, 0x22, 0x33);
  for (int r = 0; r < 6; r++) root.putstr(r, 0, "          ");
  auto dialog = root.createChild();
  dialog->setName("dialog");
  dialog->resize(3, 4);
  dialog->move(1, 2);
  dialog->setElevation(2);
  dialog->setCornerRadius(6);
  dialog->setBgColor(0xff, 0xff, 0xff);
  for (int r = 0; r < 3; r++) dialog->putstr(r, 0, "    ");
  auto square = root.createChild();
  square->setName("square");
  square->resize(1, 1);
  square->move(0, 0);
  square->setElevation(2);
  root.refresh();
  const ElevatedArea * round = nullptr;
  const ElevatedArea * flat = nullptr;
  auto areas = root.takeElevations();
  for (const auto & a : areas) {
    if (a.name == "dialog") round = &a;
    if (a.name == "square") flat = &a;
  }
  CHECK(round != nullptr);
  CHECK(flat != nullptr);
  if (!round || !flat) return;
  CHECK(round->radius == 6);
  CHECK(flat->radius == 0);
  for (const auto & under : round->corner_bg) {
    CHECK(under.has_value());
    if (under) CHECK(under->toSrgb8() == Color("#112233").toSrgb8());
  }
}

// A lifted cell that casts no shadow is a piece of its own, and says so.
TEST(bufferplane_reports_shadowless_lifted_cells) {
  StyleProvider styles;
  CellBuffer b(2, 10);
  BufferPlane root(b, styles);
  auto plane = root.createChild();
  plane->setName("plane");
  plane->resize(2, 10);
  plane->setLift(1);
  plane->putstr(0, 0, "aa");
  plane->setLift(1, false, false);
  plane->putstr(0, 2, "bb");
  plane->setLift(0);
  root.refresh();
  std::vector<ElevatedArea> lifted;
  for (const auto & area : root.takeElevations()) lifted.push_back(area);
  CHECK(lifted.size() == 2);
  CHECK(std::any_of(lifted.begin(), lifted.end(), [](const ElevatedArea & a) { return a.col == 0 && a.cols == 2 && a.shadow; }));
  CHECK(std::any_of(lifted.begin(), lifted.end(), [](const ElevatedArea & a) { return a.col == 2 && a.cols == 2 && !a.shadow; }));
}

TEST(bufferplane_keeps_wide_colours_in_its_cells) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  BufferPlane root(b, styles);
  Color led = Color("oklch(0.8 0.2 25)") * 6.0f;
  Color green("color(rec2020 0 1 0)");
  root.setFgColor(green);
  root.setBgColor(led);
  root.putstr(0, 0, "x");
  root.refresh();
  // Nothing clipped them on the way: the cell holds exactly what was set.
  CHECK(*b.at(0, 0).fg == green);
  CHECK(*b.at(0, 0).bg == led);
  CHECK(b.at(0, 0).bg->luminance() > 1.0f);
  // Changing the colour changes what the cell holds.
  root.setBgColor(led * 0.5f);
  root.putstr(0, 1, "y");
  root.refresh();
  CHECK(*b.at(0, 1).bg == led * 0.5f);
}

TEST(bufferplane_blends_a_wide_colour_in_linear_light) {
  StyleProvider styles;
  CellBuffer b(1, 4);
  BufferPlane root(b, styles);
  Color lit = Color(255, 255, 255) * 4.0f;
  root.setBgColor(lit);
  root.putstr(0, 0, "....");
  auto top = root.createChild();
  top->setBgColor(Color(0, 0, 0));
  top->setBgBlend(true);
  top->putstr(0, 0, "x");
  root.refresh();
  // Half of four times SDR white is twice SDR white.
  CHECK_NEAR(b.at(0, 0).bg->luminance(), 2.0f, 1e-4f);
}

TEST(cellbuffer_ansi_shows_a_wide_colour_clipped_to_srgb) {
  CellBuffer b(1, 2);
  CellBuffer::Style style;
  style.fg = Color("color(rec2020 0 1 0)");
  style.bg = Color(255, 255, 255) * 3.0f;
  b.write(0, 0, "x", style);
  std::string ansi = b.toAnsi();
  auto fg = style.fg->toSrgb8(), bg = style.bg->toSrgb8();
  CHECK(ansi.find(";38;2;" + std::to_string(int{fg[0]}) + ";" + std::to_string(int{fg[1]}) + ";" + std::to_string(int{fg[2]})) != std::string::npos);
  CHECK(ansi.find(";48;2;255;255;255") != std::string::npos);
  CHECK(bg[0] == 255 && bg[1] == 255 && bg[2] == 255);
}

// Lifted cells composed before a plane that scrolls by part of a cell are
// moved into that plane's layer; they still stand above the base, and a later
// backdrop is the only thing that covers them.
TEST(bufferplane_lifted_cells_below_a_scroll_layer_are_still_reported) {
  StyleProvider styles;
  CellBuffer b(8, 10);
  BufferPlane root(b, styles);
  root.setSmoothScroll(true);
  auto cards = root.createChild();
  cards->resize(2, 10);
  cards->setLift(0.5f);
  cards->putstr(0, 0, "card");
  cards->setLift(0);
  auto list = root.createChild();
  list->resize(3, 6);
  list->move(4, 0);
  list->setScrollOffset(0.25, 0);
  list->putstr(0, 0, "item");
  root.refresh();
  auto layers = root.takeBackdropLayers();
  CHECK(layers.size() == 1 && layers[0].scroll);
  auto areas = root.takeElevations();
  CHECK(areas.size() == 1);
  CHECK(areas[0].row == 0 && areas[0].col == 0 && areas[0].rows == 1 && areas[0].cols == 4 && areas[0].elevation == 0.5f);
  // Composed into the layer, not after it.
  CHECK(areas[0].layer == 0);
}

// A display that cannot lay text out sets it as cells, cut to the box, with an
// ellipsis when asked.
TEST(bufferplane_put_text_without_text_layout_cuts_to_the_box) {
  StyleProvider styles;
  CellBuffer b(2, 12);
  BufferPlane root(b, styles);
  root.putText(0, 0, 6, "a long clip name", TextStyle{ TextFont::PROPORTIONAL, TextOverflow::CLIP });
  root.putText(1, 0, 6, "a long clip name", TextStyle{ TextFont::PROPORTIONAL, TextOverflow::ELLIPSIS });
  root.refresh();
  CHECK(b.at(0, 0).text == "a" && b.at(0, 5).text == "g" && b.at(0, 6).text == " ");
  CHECK(b.at(1, 4).text == "n" && b.at(1, 5).text == "…");
  // Short enough text is left as it is.
  root.putText(0, 7, 5, "ab", TextStyle{ TextFont::PROPORTIONAL, TextOverflow::ELLIPSIS });
  root.refresh();
  CHECK(b.at(0, 7).text == "a" && b.at(0, 8).text == "b" && b.at(0, 9).text == " ");
  // To the plane's right edge when no width is given.
  root.putText(1, 6, -1, "abcdefgh", TextStyle{ TextFont::MONOSPACE, TextOverflow::CLIP });
  root.refresh();
  CHECK(b.at(1, 11).text == "f");
}

// A display that lays text out gets the run, and the cells of its box marked
// with it; writing over a cell takes the mark back.
TEST(bufferplane_put_text_with_text_layout_marks_the_box) {
  StyleProvider styles;
  CellBuffer b(1, 12);
  BufferPlane root(b, styles);
  root.setProportionalText(true);
  root.setFgColor(Color(200, 100, 50));
  root.setBgColor(Color(10, 20, 30));
  root.putText(0, 2, 6, "Pizzicato strings", TextStyle{ TextFont::PROPORTIONAL, TextOverflow::ELLIPSIS });
  root.putstr(0, 4, "x");
  root.refresh();
  auto runs = root.takeTextRuns();
  CHECK(runs.size() == 1);
  CHECK(runs[0].text == "Pizzicato strings" && runs[0].overflow == TextOverflow::ELLIPSIS && runs[0].fg);
  CHECK(b.at(0, 1).text_run == 0 && b.at(0, 2).text_run == 1 && b.at(0, 3).text_run == 1);
  CHECK(b.at(0, 4).text_run == 0 && b.at(0, 4).text == "x");
  CHECK(b.at(0, 7).text_run == 1 && b.at(0, 8).text_run == 0);
  // The box keeps the plane's colours.
  CHECK(b.at(0, 2).bg && b.at(0, 2).text == " ");
  // A monospace request is just cells, on any display.
  root.putText(0, 8, 3, "abcdef", TextStyle{ TextFont::MONOSPACE, TextOverflow::CLIP });
  root.refresh();
  CHECK(b.at(0, 8).text == "a" && b.at(0, 8).text_run == 0);
}

// The runs of a plane scrolling by part of a cell are the frame's too.
TEST(bufferplane_text_runs_survive_a_scroll_layer) {
  StyleProvider styles;
  CellBuffer b(6, 10);
  BufferPlane root(b, styles);
  root.setSmoothScroll(true);
  root.setProportionalText(true);
  auto list = root.createChild();
  list->resize(3, 8);
  list->move(1, 1);
  list->setScrollOffset(0.5, 0);
  list->putText(0, 0, 5, "name", TextStyle{});
  root.refresh();
  auto layers = root.takeBackdropLayers();
  auto runs = root.takeTextRuns();
  CHECK(layers.size() == 1 && layers[0].scroll && runs.size() == 1 && runs[0].text == "name");
  CHECK(layers[0].scroll->cells.at(0, 0).text_run == 1);
}
