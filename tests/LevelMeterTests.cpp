#include "TestFramework.h"

#include "../src/ui/tui/LevelMeter.h"

TEST(level_meter_fraction_maps_minus_60_to_0_db_onto_0_to_1) {
  CHECK(level_meter::fraction(-1.0f) == 0.0f); // TrackInfo's "no reading yet"
  CHECK(level_meter::fraction(0.0f) == 0.0f);
  CHECK(level_meter::fraction(0.001f) == 0.0f);                 // -60dB
  CHECK_NEAR(level_meter::fraction(0.01f), 1.0f / 3.0f, 1e-5f); // -40dB
  CHECK_NEAR(level_meter::fraction(0.1f), 2.0f / 3.0f, 1e-5f);  // -20dB
  CHECK_NEAR(level_meter::fraction(1.0f), 1.0f, 1e-5f);
  CHECK(level_meter::fraction(2.0f) == 1.0f); // clipped to full scale
}

TEST(level_meter_vertical_cell_fills_its_right_column_bottom_up) {
  CHECK(level_meter::verticalCell(0) == " ");
  CHECK(level_meter::verticalCell(1) == "⢀");
  CHECK(level_meter::verticalCell(2) == "⢠");
  CHECK(level_meter::verticalCell(3) == "⢰");
  CHECK(level_meter::verticalCell(4) == "⢸");
  CHECK(level_meter::verticalCell(9) == "⢸"); // clamped
}

TEST(level_meter_vertical_bar_fills_from_the_bottom_cell_up) {
  auto empty = level_meter::verticalBar(0.0f, 2);
  CHECK(empty.size() == 2 && empty[0] == " " && empty[1] == " ");
  auto half = level_meter::verticalBar(0.5f, 2); // 4 of 8 steps
  CHECK(half[0] == " " && half[1] == "⢸");
  auto five_eighths = level_meter::verticalBar(0.625f, 2); // 5 of 8
  CHECK(five_eighths[0] == "⢀" && five_eighths[1] == "⢸");
  CHECK(level_meter::verticalBar(0.5f, 0).empty());
}

TEST(level_meter_peak_marker_floats_above_the_bar) {
  // Bar at 1 of 8 steps, peak at step 6: the marker is a lone dot in the
  // top cell's right column, with a gap below it.
  auto bar = level_meter::verticalBar(0.125f, 2, 0.75f);
  CHECK(bar[0] == level_meter::cell(level_meter::Glyphs::BRAILLE, {}, {0, 2}));
  CHECK(bar[0] != " " && bar[0] != level_meter::verticalCell(2));
  CHECK(bar[1] == "⢀");
  // A peak at or below the bar's own tip adds nothing.
  auto flat = level_meter::verticalBar(0.5f, 2, 0.25f);
  CHECK(flat[0] == " " && flat[1] == "⢸");
}

TEST(level_meter_two_columns_share_a_cell) {
  auto both = level_meter::verticalBar(level_meter::Glyphs::BRAILLE, 1, {4, 0}, {4, 0});
  CHECK(both[0] == "⣿");
  auto left_only = level_meter::verticalBar(level_meter::Glyphs::BRAILLE, 1, {2, 0}, {0, 0});
  CHECK(left_only[0] == "⡄");
}

TEST(level_meter_sextant_bar_has_three_steps_per_cell) {
  CHECK(level_meter::stepsPerCell(level_meter::Glyphs::BLOCKS) == 3);
  auto bar = level_meter::verticalBar(level_meter::Glyphs::BLOCKS, 2, {}, {4, 0}); // 4 of 6 steps, right column
  CHECK(bar[1] == "▐"); // full right column
  CHECK(bar[0] != " "); // one step into the top cell
}

TEST(level_meter_ballistics_smooths_a_low_note_block_ripple) {
  // A 40 Hz tone read in 10 ms blocks swings between 0.05 and 0.7 RMS;
  // the shown level must stay steady after settling.
  level_meter::Ballistics ballistics;
  float low = 1.0f, high = 0.0f;
  for (int i = 0; i < 200; i++) {
    float rms = (i % 2 == 0) ? 0.05f : 0.7f;
    float shown = ballistics.update(rms, 0.01f);
    if (i > 100) { low = std::min(low, shown); high = std::max(high, shown); }
  }
  CHECK(high - low < 0.03f);
  CHECK(low > 0.3f);
}

TEST(level_meter_ballistics_falls_no_faster_than_the_release_rate) {
  level_meter::Ballistics ballistics;
  for (int i = 0; i < 100; i++) ballistics.update(0.5f, 0.01f);
  float before = ballistics.update(0.5f, 0.01f);
  float after = 0.0f;
  for (int i = 0; i < 10; i++) after = ballistics.update(0.0f, 0.01f); // 0.1 s of silence
  float max_drop_db = level_meter::Ballistics::kReleaseDbPerSecond * 0.1f;
  CHECK(after >= before * std::pow(10.0f, -max_drop_db / 20.0f) - 1e-4f);
  CHECK(after < before);
}

TEST(level_meter_ballistics_treats_no_reading_as_silence) {
  level_meter::Ballistics ballistics;
  CHECK(ballistics.update(-1.0f, 0.02f) == 0.0f);
}

TEST(level_meter_peak_hold_rises_at_once_holds_then_falls_slowly) {
  level_meter::PeakHold peak;
  CHECK(peak.update(0.8f, 0.02f) == 0.8f);
  CHECK(peak.update(0.2f, 0.3f) == 0.8f); // holding
  CHECK(peak.update(0.2f, 0.3f) == 0.8f); // hold runs out during this step
  float falling = peak.update(0.2f, 0.2f);
  CHECK_NEAR(falling, 0.8f - level_meter::PeakHold::kFallFractionPerSecond * 0.2f, 1e-5f);
  for (int i = 0; i < 20; i++) falling = peak.update(0.2f, 0.5f);
  CHECK_NEAR(falling, 0.2f, 1e-5f);
  CHECK(peak.update(0.9f, 0.01f) == 0.9f);
}
