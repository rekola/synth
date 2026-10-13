#include "TestFramework.h"

#include <cellui/BufferPlane.h>
#include <cellui/LevelMeter.h>
#include <cellui/Scopes.h>
#include <cellui/VuMeterImage.h>

using namespace cellui;

namespace {

struct Rgb { int r, g, b; };

Rgb at(const std::vector<uint32_t> & img, unsigned width, unsigned x, unsigned y) {
  uint32_t p = img[y * width + x];
  return {int(p & 0xff), int((p >> 8) & 0xff), int((p >> 16) & 0xff)};
}

bool same(Rgb a, const Color & c) { return a.r == c.getRed() && a.g == c.getGreen() && a.b == c.getBlue(); }

}  // namespace

TEST(vu_meter_image_fills_its_slot_from_the_bottom) {
  StyleProvider styles;
  VuMeterLook look{Color(10, 20, 30), Color(200, 100, 50)};
  // One cell's two dot columns, the bar in the right one, half full.
  auto img = vu_meter_image::render({std::nullopt, VuReading{0.5f, 0.0f, false}}, look, 20, 40, styles);
  CHECK(img.size() == 800u);
  CHECK(same(at(img, 20, 15, 39), *look.color));   // low in the right slot
  CHECK(same(at(img, 20, 15, 5), look.background)); // above the level
  CHECK(same(at(img, 20, 5, 39), look.background));  // the empty left slot
  // A peak line, and the clip colour while clipping.
  img = vu_meter_image::render({VuReading{0.0f, 0.75f, false}}, look, 10, 40, styles);
  CHECK(same(at(img, 10, 5, 10), *look.color) && same(at(img, 10, 5, 30), look.background));
  img = vu_meter_image::render({VuReading{1.0f, 0.0f, true}}, look, 10, 40, styles);
  CHECK(same(at(img, 10, 5, 20), styles.meter_clip_color));
}

TEST(vu_meter_cell_version_is_the_braille_bar_clipped_to_its_rows) {
  StyleProvider styles;
  CellBuffer b(6, 2);
  BufferPlane root(b, styles);
  VuReading reading{0.6f, 0.9f, false};
  // Five rows from row 0, the top one clipped away.
  drawCellVuMeter(root, 0, 1, 5, reading, VuMeterLook{styles.window_bg_color, std::nullopt}, 1, 6);
  root.refresh();
  auto bar = level_meter::verticalBar(reading.fraction, 5, reading.peak);
  CHECK(b.at(0, 1).text == " ");
  for (int i = 1; i < 5; i++) CHECK(b.at(i, 1).text == bar[static_cast<size_t>(i)]);
}

TEST(vu_meter_pixel_version_shows_only_its_visible_rows) {
  auto scopes = pixelScopes();
  if (!scopes) return;  // no pixel backend in this build
  StyleProvider styles;
  CellBuffer b(6, 3);
  BufferPlane root(b, styles);
  root.setCellPixelSize(8, 4);
  // A five-row meter from row -2: rows 0-2 show.
  scopes->drawVuMeter(root, -2, 2, 5, VuReading{0.5f, 0.0f, false}, VuMeterLook{styles.window_bg_color, std::nullopt}, 0, 6);
  root.refresh();
  CHECK(root.images().size() == 1);
  CHECK(root.images()[0].row == 0 && root.images()[0].col == 2 && root.images()[0].image->height == 3 * 8);
  CHECK(b.at(0, 2).image && b.at(2, 2).image && !b.at(3, 2).image && !b.at(0, 1).image);
  // Without the cell size in pixels it falls back to cells.
  CellBuffer c(5, 1);
  BufferPlane plain(c, styles);
  scopes->drawVuMeter(plain, 0, 0, 5, VuReading{1.0f, 0.0f, false}, VuMeterLook{styles.window_bg_color, std::nullopt}, 0, 5);
  plain.refresh();
  CHECK(plain.images().empty() && c.at(4, 0).text != " ");
}
