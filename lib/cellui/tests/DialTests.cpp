#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/Dial.h>

using namespace cellui;

namespace {

struct Rgb { int r, g, b; };

Rgb at(const std::vector<uint32_t> & img, unsigned width, unsigned x, unsigned y) {
  uint32_t p = img[y * width + x];
  return {int(p & 0xff), int((p >> 8) & 0xff), int((p >> 16) & 0xff)};
}

bool same(Rgb a, const Color & c) { return a.r == c.getRed() && a.g == c.getGreen() && a.b == c.getBlue(); }

}  // namespace

TEST(dial_image_has_a_ring_open_at_the_bottom_and_a_pointer_that_follows_the_value) {
  DialLook look{Color(10, 20, 30), Color(250, 250, 250)};
  const unsigned size = 40;
  // Without a letter, so that nothing but the ring and pointer is drawn.
  auto img = dial_image::render(DialReading{"", 0.5f, ""}, look, size, size);
  if (img.empty()) return;  // no pixel backend in this build
  CHECK(img.size() == size * size);
  // The ring: its left, top and right are foreground; the bottom is open.
  CHECK(same(at(img, size, 5, 20), look.foreground));
  CHECK(same(at(img, size, 34, 20), look.foreground));
  CHECK(same(at(img, size, 20, 34), look.background));
  // Half way, the pointer points straight up: foreground above the centre,
  // background beside it (its outline) and below.
  CHECK(same(at(img, size, 20, 10), look.foreground));
  CHECK(same(at(img, size, 24, 10), look.background));
  CHECK(same(at(img, size, 20, 30), look.background));
  // At 0 it points to the bottom left, at 1 to the bottom right.
  auto low = dial_image::render(DialReading{"", 0.0f, ""}, look, size, size);
  auto high = dial_image::render(DialReading{"", 1.0f, ""}, look, size, size);
  CHECK(same(at(low, size, 13, 29), look.foreground));
  CHECK(same(at(high, size, 27, 29), look.foreground));
  CHECK(same(at(low, size, 20, 10), look.background));
}

TEST(dial_draws_a_picture_where_the_plane_shows_pictures_and_text_otherwise) {
  StyleProvider styles;
  DialLook look{styles.grid_bg_color, styles.window_accent_fg_color};
  DialReading reading{"A", 0.5f, "-12"};
  CellBuffer pictured(4, 9);
  BufferPlane with_pictures(pictured, styles);
  with_pictures.setCellPixelSize(16, 8);
  drawDial(with_pictures, 0, 2, 3, 6, reading, look, 0, 4, 7);
  with_pictures.refresh();
  if (with_pictures.images().empty()) return;  // no pixel backend in this build
  CHECK(with_pictures.images().size() == 1);
  CHECK(with_pictures.images()[0].row == 0 && with_pictures.images()[0].col == 2);
  CHECK(with_pictures.images()[0].image->width == 6 * 8 && with_pictures.images()[0].image->height == 3 * 16);

  // The same cells in text: a rounded box with the value, the letter in its bottom border.
  CellBuffer plain(4, 9);
  BufferPlane text(plain, styles);
  drawDial(text, 0, 2, 3, 6, reading, look, 0, 4, 7);
  text.refresh();
  CHECK(text.images().empty());
  CHECK(plain.at(0, 2).text == "╭" && plain.at(0, 7).text == "╮");
  CHECK(plain.at(1, 2).text == "│" && plain.at(1, 3).text == "-" && plain.at(1, 4).text == "1" && plain.at(1, 5).text == "2");
  CHECK(plain.at(2, 2).text == "╰" && plain.at(2, 4).text == "A" && plain.at(2, 7).text == "╯");
}

TEST(dial_text_version_is_clipped_to_the_plane_rows_it_is_given) {
  StyleProvider styles;
  DialLook look{styles.grid_bg_color, styles.window_accent_fg_color};
  CellBuffer plain(3, 6);
  BufferPlane text(plain, styles);
  drawDial(text, 0, 0, 3, 6, DialReading{"B", 0.0f, "off"}, look, 1, 3);
  text.refresh();
  CHECK(plain.at(0, 0).text != "╭"); // above the rows it is given
  CHECK(plain.at(1, 1).text == "o");
  CHECK(plain.at(2, 2).text == "B" || plain.at(2, 3).text == "B");
}

namespace {

Input mouse(int id, int y, int x, Input::Kind kind = Input::Kind::PRESS, bool shift = false) {
  return Input(id, y, x, false, shift, false, false, kind);
}

struct DialFixture {
  StyleProvider styles;
  CellBuffer buffer{12, 30};
  BufferPlane plane{buffer, styles};
  Dial dial{plane, DialSpec{"A"}};
  std::vector<float> changes;
  int begun = 0, ended = 0, clicks = 0;
  DialFixture() {
    dial.setPosition(2, 3);
    dial.setValue(-20.0f);
    dial.on_change = [this](float v) { changes.push_back(v); };
    dial.on_turn_begin = [this] { begun++; };
    dial.on_turn_end = [this] { ended++; };
    dial.on_click = [this] { clicks++; };
  }
};

}  // namespace

TEST(dial_widget_drag_turns_it_up_and_down_and_ends_without_opening_the_editor) {
  DialFixture f;
  CHECK(!f.dial.offerMouse(mouse(Key::Button1, 0, 0)));  // a press elsewhere is not its own
  CHECK(f.dial.offerMouse(mouse(Key::Button1, 3, 5)));
  CHECK(f.dial.isDragging() && f.dial.isPressed());
  // Two rows up, a drag that moves with the pointer anywhere, outside the dial too.
  CHECK(f.dial.offerMouse(mouse(Key::Button1, 1, 25)));
  CHECK(f.dial.value() > -20.0f && f.begun == 1);
  CHECK(f.dial.offerMouse(mouse(Key::Button1, 9, 5)));
  CHECK(f.dial.value() < -20.0f);
  CHECK(f.dial.offerMouse(mouse(Key::Button1, 3, 5, Input::Kind::RELEASE)));
  CHECK(f.ended == 1 && f.clicks == 0 && !f.dial.isEditing() && !f.dial.isPressed() && !f.dial.isDragging());
}

TEST(dial_widget_drag_below_its_lowest_step_is_off_and_shift_is_fine) {
  DialFixture f;
  f.dial.offerMouse(mouse(Key::Button1, 3, 5));
  f.dial.offerMouse(mouse(Key::Button1, 11, 5));
  CHECK(f.dial.value() < -99.0f);
  f.dial.offerMouse(mouse(Key::Button1, 3, 5, Input::Kind::RELEASE));
  DialFixture g;
  g.dial.offerMouse(mouse(Key::Button1, 3, 5));
  g.dial.offerMouse(mouse(Key::Button1, 1, 5, Input::Kind::PRESS, true));
  CHECK(g.dial.value() > -20.0f && g.dial.value() < -19.0f);
}

TEST(dial_widget_wheel_turns_it_only_over_it) {
  DialFixture f;
  CHECK(!f.dial.offerMouse(mouse(Key::Button4, 0, 0)));
  CHECK(f.dial.offerMouse(mouse(Key::Button4, 3, 5)));
  CHECK_NEAR(f.dial.value(), -19.0f, 0.01f);
  CHECK(f.dial.offerMouse(mouse(Key::Button5, 3, 5)));
  CHECK(f.dial.offerMouse(mouse(Key::Button5, 3, 5)));
  CHECK_NEAR(f.dial.value(), -21.0f, 0.01f);
}

TEST(dial_widget_click_opens_the_editor_on_its_value_and_typing_sets_it) {
  DialFixture f;
  f.dial.offerMouse(mouse(Key::Button1, 3, 5));
  f.dial.offerMouse(mouse(Key::Button1, 3, 5, Input::Kind::RELEASE));
  CHECK(f.clicks == 1 && f.dial.isEditing());
  f.dial.offerInput(keyInput('-', 0));
  f.dial.offerInput(keyInput('6', 0));
  f.dial.offerInput(keyInput(Key::Enter, 0));
  CHECK(!f.dial.isEditing());
  CHECK_NEAR(f.dial.value(), -6.0f, 0.001f);
}

TEST(dial_widget_escape_closes_the_editor_and_keeps_the_value) {
  DialFixture f;
  f.dial.startEdit();
  CHECK(f.dial.isEditing());
  f.dial.offerInput(keyInput('9', 0));
  f.dial.offerInput(keyInput(Key::Escape, 0));
  CHECK(!f.dial.isEditing());
  CHECK_NEAR(f.dial.value(), -20.0f, 0.001f);
  CHECK(f.changes.empty());
}

TEST(dial_widget_pressed_and_selected_look_different_from_idle) {
  DialFixture f;
  f.dial.setLook(Color(255, 255, 255), Color(10, 10, 10));
  f.dial.draw();
  f.plane.refresh();
  auto idle = f.buffer.at(2, 3);
  f.dial.setSelected(1.0f);
  f.dial.draw();
  f.plane.refresh();
  auto selected = f.buffer.at(2, 3);
  f.dial.setSelected(0.0f);
  f.dial.setPressed(true);
  f.dial.draw();
  f.plane.refresh();
  auto pressed = f.buffer.at(2, 3);
  // Each changes the lines, not the slot, and differently.
  CHECK(!(idle.fg == selected.fg) && idle.bg == selected.bg);
  CHECK(!(pressed.fg == idle.fg) && !(pressed.fg == selected.fg) && pressed.bg == idle.bg);
}
