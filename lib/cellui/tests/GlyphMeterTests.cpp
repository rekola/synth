#include "TestFramework.h"

#include <cellui/term/GlyphMeter.h>

using namespace cellui::term;

TEST(glyph_meter_vertical_cell_fills_its_right_column_bottom_up) {
  CHECK(glyph_meter::verticalCell(0) == " ");
  CHECK(glyph_meter::verticalCell(1) == "⢀");
  CHECK(glyph_meter::verticalCell(2) == "⢠");
  CHECK(glyph_meter::verticalCell(3) == "⢰");
  CHECK(glyph_meter::verticalCell(4) == "⢸");
  CHECK(glyph_meter::verticalCell(9) == "⢸"); // clamped
}

TEST(glyph_meter_vertical_bar_fills_from_the_bottom_cell_up) {
  auto empty = glyph_meter::verticalBar(0.0f, 2);
  CHECK(empty.size() == 2 && empty[0] == " " && empty[1] == " ");
  auto half = glyph_meter::verticalBar(0.5f, 2); // 4 of 8 steps
  CHECK(half[0] == " " && half[1] == "⢸");
  auto five_eighths = glyph_meter::verticalBar(0.625f, 2); // 5 of 8
  CHECK(five_eighths[0] == "⢀" && five_eighths[1] == "⢸");
  CHECK(glyph_meter::verticalBar(0.5f, 0).empty());
}

TEST(glyph_meter_peak_marker_floats_above_the_bar) {
  // Bar at 1 of 8 steps, peak at step 6: the marker is a lone dot in the
  // top cell's right column, with a gap below it.
  auto bar = glyph_meter::verticalBar(0.125f, 2, 0.75f);
  CHECK(bar[0] == glyph_meter::cell(glyph_meter::Glyphs::BRAILLE, {}, {0, 2}));
  CHECK(bar[0] != " " && bar[0] != glyph_meter::verticalCell(2));
  CHECK(bar[1] == "⢀");
  // A peak at or below the bar's own tip adds nothing.
  auto flat = glyph_meter::verticalBar(0.5f, 2, 0.25f);
  CHECK(flat[0] == " " && flat[1] == "⢸");
}

TEST(glyph_meter_two_columns_share_a_cell) {
  auto both = glyph_meter::verticalBar(glyph_meter::Glyphs::BRAILLE, 1, {4, 0}, {4, 0});
  CHECK(both[0] == "⣿");
  auto left_only = glyph_meter::verticalBar(glyph_meter::Glyphs::BRAILLE, 1, {2, 0}, {0, 0});
  CHECK(left_only[0] == "⡄");
}

TEST(glyph_meter_sextant_bar_has_three_steps_per_cell) {
  CHECK(glyph_meter::stepsPerCell(glyph_meter::Glyphs::BLOCKS) == 3);
  auto bar = glyph_meter::verticalBar(glyph_meter::Glyphs::BLOCKS, 2, {}, {4, 0}); // 4 of 6 steps, right column
  CHECK(bar[1] == "▐"); // full right column
  CHECK(bar[0] != " "); // one step into the top cell
}
