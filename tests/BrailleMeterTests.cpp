#include "TestFramework.h"

#include "../src/ui/tui/BrailleMeter.h"

TEST(braille_meter_fraction_maps_minus_40_to_0_db_onto_0_to_1) {
  CHECK(braille_meter::fraction(-1.0f) == 0.0f); // TrackInfo's "no reading yet"
  CHECK(braille_meter::fraction(0.0f) == 0.0f);
  CHECK(braille_meter::fraction(0.01f) == 0.0f); // -40dB
  CHECK_NEAR(braille_meter::fraction(0.1f), 0.5f, 1e-5f); // -20dB
  CHECK_NEAR(braille_meter::fraction(1.0f), 1.0f, 1e-5f);
  CHECK(braille_meter::fraction(2.0f) == 1.0f); // clipped to full scale
}

TEST(braille_meter_vertical_cell_fills_its_right_column_bottom_up) {
  CHECK(braille_meter::verticalCell(0) == " ");
  CHECK(braille_meter::verticalCell(1) == "⢀");
  CHECK(braille_meter::verticalCell(2) == "⢠");
  CHECK(braille_meter::verticalCell(3) == "⢰");
  CHECK(braille_meter::verticalCell(4) == "⢸");
  CHECK(braille_meter::verticalCell(9) == "⢸"); // clamped
}

TEST(braille_meter_vertical_bar_fills_from_the_bottom_cell_up) {
  auto empty = braille_meter::verticalBar(0.0f, 2);
  CHECK(empty.size() == 2 && empty[0] == " " && empty[1] == " ");
  auto half = braille_meter::verticalBar(0.5f, 2); // 4 of 8 steps
  CHECK(half[0] == " " && half[1] == "⢸");
  auto five_eighths = braille_meter::verticalBar(0.625f, 2); // 5 of 8
  CHECK(five_eighths[0] == "⢀" && five_eighths[1] == "⢸");
  CHECK(braille_meter::verticalBar(0.5f, 0).empty());
}
