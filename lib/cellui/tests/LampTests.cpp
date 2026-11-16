#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/CellBuffer.h>
#include <cellui/Lamp.h>

#include <algorithm>
#include <cmath>

using namespace cellui;

namespace {

float peak709(const Color & c) {
  auto v = c.linearSrgb();
  return std::max({v[0], v[1], v[2]});
}

}  // namespace

TEST(lamp_is_a_circle_in_the_lamps_colour_on_the_background) {
  StyleProvider styles;
  CellBuffer b(3, 8);
  BufferPlane root(b, styles);
  Color bg(10, 20, 30);
  drawLamp(root, 1, 3, true, bg, 0, 3);
  drawLamp(root, 2, 3, false, bg, 0, 3);
  root.refresh();
  CHECK(b.at(1, 3).text == kLampGlyph && b.at(2, 3).text == kLampGlyph);
  CHECK(b.at(1, 4).text != kLampGlyph && b.at(1, 2).text != kLampGlyph);
  CHECK(b.at(1, 3).bg == bg && b.at(2, 3).bg == bg);
  // Lit, the colour is past white (a window glows it, a terminal makes it bold); off, it is not.
  Color lit = b.at(1, 3).fg.value_or(Color()), off = b.at(2, 3).fg.value_or(Color());
  CHECK(peak709(lit) > 1.5f && peak709(off) <= 1.0f);
  // Lit it is the alarm colour, red; off it is colourless, a grey, so it is plain which it is.
  auto v = lit.linearSrgb();
  CHECK(v[0] > v[1] * 5.0f && v[0] > v[2] * 2.0f);
  auto g = off.linearSrgb();
  CHECK(std::abs(g[0] - g[1]) < 0.02f && std::abs(g[1] - g[2]) < 0.02f);
  CHECK(lit == styles.lampColor(true, bg) && off == styles.lampColor(false, bg));
}

TEST(lamp_draws_only_in_the_rows_it_may) {
  StyleProvider styles;
  CellBuffer b(3, 8);
  BufferPlane root(b, styles);
  Color bg(10, 20, 30);
  drawLamp(root, 0, 3, true, bg, 1, 3);
  drawLamp(root, 2, 3, true, bg, 1, 2);
  root.refresh();
  CHECK(b.at(0, 3).text != kLampGlyph && b.at(2, 3).text != kLampGlyph);
}

TEST(lamp_is_the_glyph_where_there_are_pictures_too) {
  StyleProvider styles;
  CellBuffer b(3, 8);
  BufferPlane root(b, styles);
  root.setCellPixelSize(19, 10);
  root.setHdrPictures(true);
  drawLamp(root, 1, 3, true, Color(10, 20, 30), 0, 3);
  root.refresh();
  CHECK(b.at(1, 3).text == kLampGlyph);
  CHECK(root.images().empty());
}
