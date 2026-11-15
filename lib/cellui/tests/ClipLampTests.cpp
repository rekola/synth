#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/CellBuffer.h>
#include <cellui/ClipLamp.h>

#include <algorithm>
#include <cstdlib>

using namespace cellui;

namespace {

struct Rgb { int r, g, b; };

Rgb at(const std::vector<uint32_t> & img, unsigned width, unsigned x, unsigned y) {
  uint32_t p = img[y * width + x];
  return {int(p & 0xff), int((p >> 8) & 0xff), int((p >> 16) & 0xff)};
}

// To a level: canvas and the tables map a colour to a byte a hair apart at times.
bool near(Rgb a, const Color & c) { return std::abs(a.r - c.getRed()) <= 1 && std::abs(a.g - c.getGreen()) <= 1 && std::abs(a.b - c.getBlue()) <= 1; }

float peak709(const Picture & pic, unsigned width, unsigned x, unsigned y) {
  const float * p = &pic.hdr[(y * width + x) * 4];
  auto v = Color::linear(p[0], p[1], p[2]).linearSrgb();
  return std::max({v[0], v[1], v[2]});
}

}  // namespace

TEST(clip_lamp_picture_is_a_round_disc_on_the_background) {
  StyleProvider styles;
  Color bg(10, 20, 30);
  // Two cells of 10 x 19 pixels.
  auto pic = clip_lamp_image::renderPicture(false, 20, 19, bg, styles, false);
  CHECK(!pic.isHdr() && pic.pixels.size() == 20u * 19u);
  CHECK(near(at(pic.pixels, 20, 10, 9), styles.clipLedColor(false, bg)));  // the middle is the dim lamp
  CHECK(near(at(pic.pixels, 20, 0, 0), bg) && near(at(pic.pixels, 20, 19, 18), bg));  // the corners are not
  CHECK(near(at(pic.pixels, 20, 0, 9), bg));  // nor the middle of the edge: the disc has a margin
  // Round, not square: a point well inside the disc along the diagonal is lamp, one at the corner of its
  // bounding square is background.
  CHECK(near(at(pic.pixels, 20, 5, 4), styles.clipLedColor(false, bg)));
  CHECK(near(at(pic.pixels, 20, 3, 2), bg));
}

TEST(clip_lamp_picture_lit_is_the_clip_colour_past_white_and_dim_is_not) {
  StyleProvider styles;
  Color bg(10, 20, 30);
  auto lit = clip_lamp_image::renderPicture(true, 20, 19, bg, styles, true);
  auto dim = clip_lamp_image::renderPicture(false, 20, 19, bg, styles, true);
  if (!lit.isHdr() || !dim.isHdr()) return;  // no float surfaces here; the 8-bit picture is the same colours mapped
  CHECK(lit.hdr.size() == 20u * 19u * 4u);
  CHECK(peak709(lit, 20, 10, 9) > 2.0f);
  CHECK(peak709(dim, 20, 10, 9) <= 1.0f);
  // The clip colour: red.
  const float * p = &lit.hdr[(9 * 20 + 10) * 4];
  auto v = Color::linear(p[0], p[1], p[2]).linearSrgb();
  CHECK(v[0] > v[1] * 5.0f && v[0] > v[2] * 2.0f);
  // The background around it is the plain one.
  CHECK(peak709(lit, 20, 0, 0) <= 1.0f);
}

TEST(clip_lamp_picture_of_no_size_is_blank) {
  StyleProvider styles;
  auto pic = clip_lamp_image::renderPicture(true, 0, 19, Color(0, 0, 0), styles, true);
  CHECK(!pic.isHdr() && pic.pixels.empty());
}

TEST(clip_lamp_without_pictures_is_a_circle_and_a_space_in_the_lamps_colour) {
  StyleProvider styles;
  CellBuffer b(3, 8);
  BufferPlane root(b, styles);
  Color bg(10, 20, 30);
  drawClipLamp(root, 1, 3, true, bg, 0, 3);
  drawClipLamp(root, 2, 3, false, bg, 0, 3);
  root.refresh();
  CHECK(b.at(1, 3).text == kClipLampGlyph && b.at(1, 4).text == " ");
  CHECK(b.at(2, 3).text == kClipLampGlyph);
  // Lit, the colour is past white (a window glows it, a terminal makes it bold); dim, it is not.
  auto peak = [&](int row) {
    Color c = b.at(row, 3).fg.value_or(Color());
    auto v = c.linearSrgb();
    return std::max({v[0], v[1], v[2]});
  };
  CHECK(peak(1) > 1.5f && peak(2) <= 1.0f);
  // Outside the rows it may draw in, nothing.
  CellBuffer c(3, 8);
  BufferPlane other(c, styles);
  drawClipLamp(other, 0, 3, true, bg, 1, 3);
  other.refresh();
  CHECK(c.at(0, 3).text != kClipLampGlyph);
}

TEST(clip_lamp_with_pictures_is_one_picture_two_cells_wide) {
  StyleProvider styles;
  CellBuffer b(3, 8);
  BufferPlane root(b, styles);
  root.setCellPixelSize(19, 10);
  Color bg(10, 20, 30);
  drawClipLamp(root, 1, 3, true, bg, 0, 3, 7);
  root.refresh();
  CHECK(root.images().size() == 1);
  const Image & image = *root.images()[0].image;
  CHECK(image.width == 20 && image.height == 19 && !image.isHdr());
  CHECK(b.at(1, 3).image == 1 && b.at(1, 4).image == 1 && !b.at(1, 5).image && !b.at(1, 2).image);
  // Where the display takes floats, the lit lamp is one.
  CellBuffer c(3, 8);
  BufferPlane hdr(c, styles);
  hdr.setCellPixelSize(19, 10);
  hdr.setHdrPictures(true);
  drawClipLamp(hdr, 1, 3, true, bg, 0, 3, 7);
  hdr.refresh();
  CHECK(hdr.images().size() == 1);
  const Image & lit = *hdr.images()[0].image;
  CHECK(lit.width == 20 && lit.height == 19);
  if (lit.isHdr()) CHECK(lit.hdr.size() == 20u * 19u * 4u);
}
