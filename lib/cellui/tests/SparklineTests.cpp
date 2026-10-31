#include "TestFramework.h"

#include <cellui/BufferPlane.h>

#include <cmath>
#include <cellui/term/CellCharts.h>
#include <cellui/term/GlyphMeter.h>
#include <cellui/Charts.h>
#include <cellui/SparklineImage.h>
#include <cellui/WaveformImage.h>

using namespace cellui;

namespace {

struct Rgb { int r, g, b; };

Rgb at(const std::vector<uint32_t> & img, unsigned width, unsigned x, unsigned y) {
  uint32_t p = img[y * width + x];
  return {int(p & 0xff), int((p >> 8) & 0xff), int((p >> 16) & 0xff)};
}

bool same(Rgb a, const Color & c) { return a.r == c.getRed() && a.g == c.getGreen() && a.b == c.getBlue(); }

}  // namespace

TEST(sparkline_image_fills_its_slot_from_the_bottom) {
  StyleProvider styles;
  SparklineLook look{Color(10, 20, 30), Color(200, 100, 50)};
  // One cell's two dot columns, the bar in the right one, half full.
  auto img = sparkline_image::render({std::nullopt, SparklineReading{0.5f, 0.0f, false}}, look, 20, 40, styles);
  CHECK(img.size() == 800u);
  CHECK(same(at(img, 20, 15, 39), *look.color));   // low in the right slot
  CHECK(same(at(img, 20, 15, 5), look.background)); // above the level
  CHECK(same(at(img, 20, 5, 39), look.background));  // the empty left slot
  // A peak line, and the clip colour while saturated.
  img = sparkline_image::render({SparklineReading{0.0f, 0.75f, false}}, look, 10, 40, styles);
  CHECK(same(at(img, 10, 5, 10), *look.color) && same(at(img, 10, 5, 30), look.background));
  img = sparkline_image::render({SparklineReading{1.0f, 0.0f, true}}, look, 10, 40, styles);
  CHECK(same(at(img, 10, 5, 20), styles.level_full_color));
}

TEST(sparkline_image_spaces_its_bars_evenly_with_the_rest_at_the_edges) {
  StyleProvider styles;
  SparklineLook look{Color(10, 20, 30), Color(200, 100, 50)};
  // 27 pixels don't divide into 4 slots: the bars stay evenly spaced.
  std::vector<std::optional<SparklineReading>> full(4, SparklineReading{1.0f, 0.0f, false});
  auto img = sparkline_image::render(full, look, 27, 8, styles);
  std::vector<unsigned> starts, ends;
  for (unsigned x = 0; x < 27; x++) {
    bool on = same(at(img, 27, x, 4), *look.color);
    bool before = x > 0 && same(at(img, 27, x - 1, 4), *look.color);
    if (on && !before) starts.push_back(x);
    if (!on && before) ends.push_back(x);
  }
  if (same(at(img, 27, 26, 4), *look.color)) ends.push_back(27);
  CHECK(starts.size() == 4 && ends.size() == 4);
  for (size_t i = 1; i < starts.size() && i < ends.size(); i++) {
    CHECK(starts[i] - starts[i - 1] == starts[1] - starts[0]);
    CHECK(ends[i] - starts[i] == ends[0] - starts[0]);
  }
  if (starts.size() == 4 && ends.size() == 4) {
    unsigned left = starts[0], right = 27 - ends[3];
    CHECK(left <= right + 1 && right <= left + 1);
  }
}

TEST(sparkline_cell_version_is_the_braille_bar_clipped_to_its_rows) {
  StyleProvider styles;
  CellBuffer b(6, 2);
  BufferPlane root(b, styles);
  SparklineReading reading{0.6f, 0.9f, false};
  // Five rows from row 0, the top one clipped away.
  term::drawCellSparkline(root, 0, 1, 5, {reading}, SparklineLook{styles.window_bg_color, std::nullopt}, 1, 6);
  root.refresh();
  auto bar = term::glyph_meter::verticalBar(reading.fraction, 5, reading.peak);
  CHECK(b.at(0, 1).text == " ");
  for (int i = 1; i < 5; i++) CHECK(b.at(i, 1).text == bar[static_cast<size_t>(i)]);
}

TEST(sparkline_pixel_version_shows_only_its_visible_rows) {
  auto scopes = pixelCharts();
  if (!scopes) return;  // no pixel backend in this build
  StyleProvider styles;
  CellBuffer b(6, 3);
  BufferPlane root(b, styles);
  root.setCellPixelSize(8, 4);
  // A five-row meter from row -2: rows 0-2 show, of the whole picture.
  scopes->drawSparkline(root, -2, 2, 5, SparklineReading{0.5f, 0.0f, false}, SparklineLook{styles.window_bg_color, std::nullopt}, 0, 6);
  root.refresh();
  CHECK(root.images().size() == 1);
  CHECK(root.images()[0].row == -2 && root.images()[0].col == 2 && root.images()[0].image->height == 5 * 8);
  CHECK(b.at(0, 2).image && b.at(2, 2).image && !b.at(3, 2).image && !b.at(0, 1).image);
  // Scrolled, it is the same picture, placed elsewhere.
  uint64_t version = 0;
  for (int row = -1; row <= 3; row++) {
    root.erase();
    scopes->drawSparkline(root, row, 2, 5, SparklineReading{0.5f, 0.0f, false}, SparklineLook{styles.window_bg_color, std::nullopt}, 0, 6, 9);
    root.refresh();
    CHECK(root.images().size() == 1 && root.images()[0].row == row);
    if (row == -1) version = root.images()[0].image->version;
    CHECK(root.images()[0].image->version == version);
  }
  // Clipped to rows 1-2 of the plane: the cells outside stay text.
  root.erase();
  scopes->drawSparkline(root, 0, 2, 5, SparklineReading{0.5f, 0.0f, false}, SparklineLook{styles.window_bg_color, std::nullopt}, 1, 3, 9);
  root.refresh();
  CHECK(!b.at(0, 2).image && b.at(1, 2).image && b.at(2, 2).image && !b.at(3, 2).image);
  // Without the cell size in pixels it falls back to cells.
  CellBuffer c(5, 1);
  BufferPlane plain(c, styles);
  scopes->drawSparkline(plain, 0, 0, 5, SparklineReading{1.0f, 0.0f, false}, SparklineLook{styles.window_bg_color, std::nullopt}, 0, 5);
  plain.refresh();
  CHECK(plain.images().empty() && c.at(4, 0).text != " ");
}

TEST(waveform_row_cell_version_fills_loud_slices_and_dots_quiet_ones) {
  StyleProvider styles;
  CellBuffer b(1, 16);
  BufferPlane root(b, styles);
  WaveformLook look{styles.window_fg_color, styles.window_bg_color, styles.window_border_color};
  auto scopes = cellCharts();
  // A full-width bar in every slice fills the row; silence leaves guide lines and dots.
  scopes->drawWaveformRow(root, 0, 0, 16, std::vector<float>(kWaveformSlices, 1.0f), look);
  root.refresh();
  for (int c = 0; c < 16; c++) {
    CHECK(b.at(0, c).text != "·" && b.at(0, c).text != "│");
    // Covered cells take the bar's colour (to within the quantizer's rounding).
    CHECK(b.at(0, c).bg && std::abs(b.at(0, c).bg->toSrgb8()[0] - styles.window_fg_color.toSrgb8()[0]) <= 2);
  }
  scopes->drawWaveformRow(root, 0, 0, 16, std::vector<float>(kWaveformSlices, 0.0f), look);
  root.refresh();
  CHECK(b.at(0, 8).text == "│");
  CHECK(b.at(0, 1).text == "·");
}

TEST(waveform_row_pixel_version_is_a_row_wide_image) {
  auto image = waveform_image::render(std::vector<float>(kWaveformSlices, 0.5f), Color(255, 255, 255), Color(0, 0, 0), Color(128, 128, 128), 40, 10);
  CHECK(image.size() == 400u);
  // Centre column is covered by the bar; the outer edge is the background.
  CHECK((image[5 * 40 + 20] & 0xffffffu) != 0u);
  CHECK((image[5 * 40 + 0] & 0xffffffu) == 0u);
}

TEST(sparkline_with_several_bars_puts_two_to_a_cell_from_the_left) {
  StyleProvider styles;
  CellBuffer b(4, 4);
  BufferPlane root(b, styles);
  SparklineLook look{styles.window_bg_color, std::nullopt};
  // Three bars: two cells, the second with only its left bar.
  term::drawCellSparkline(root, 0, 1, 4, {{1.0f, 0.0f, false}, {1.0f, 0.0f, false}, {1.0f, 0.0f, false}}, look, 0, 4);
  root.refresh();
  CHECK(b.at(3, 1).text == "\xe2\xa3\xbf");  // both columns full
  CHECK(b.at(3, 2).text == "\xe2\xa1\x87");  // only the left column
  CHECK(b.at(3, 3).text == " ");
}
