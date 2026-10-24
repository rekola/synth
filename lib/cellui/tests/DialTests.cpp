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
