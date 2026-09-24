#include "TestFramework.h"

#include "../src/launchpad/LaunchpadLayout.h"

using LaunchpadLayout::PanPad;
using LaunchpadLayout::panBarPads;
using LaunchpadLayout::panPadToAzimuth;

namespace {
  // A row as a string: '.' off, 'c' centred marker, '-' bar, '#' tip.
  std::string show(float azimuth) {
    std::string s;
    for (auto pad : panBarPads(azimuth)) s += pad == PanPad::OFF ? '.' : pad == PanPad::CENTER ? 'c' : pad == PanPad::BAR ? '-' : '#';
    return s;
  }
}

TEST(pan_pads_show_only_the_inner_pair_dimly_at_exactly_zero) {
  CHECK(show(0.0f) == "...cc...");
}

TEST(pan_bar_points_left_for_negative_and_right_for_positive) {
  CHECK(show(-22.5f) == "...#....");
  CHECK(show(-45.0f) == "..#-....");
  CHECK(show(-90.0f) == "#---....");
  CHECK(show(22.5f) == "....#...");
  CHECK(show(45.0f) == "....-#..");
  CHECK(show(90.0f) == "....---#");
}

TEST(pan_bar_length_rounds_away_from_zero_so_a_small_azimuth_is_not_centred) {
  CHECK(show(5.0f) == "....#...");
  CHECK(show(-5.0f) == "...#....");
  CHECK(show(23.0f) == "....-#..");
  CHECK(show(-89.0f) == "#---....");
}

TEST(pan_row_is_fully_lit_behind_the_listener) {
  CHECK(show(180.0f) == "########");
  CHECK(show(-180.0f) == "########");
  CHECK(show(135.0f) == "########");
  CHECK(show(-100.0f) == "########");
  CHECK(show(540.0f) == "########"); // wraps to 180
}

TEST(pan_pad_press_azimuths_match_what_the_bar_shows_for_them) {
  for (int column = 0; column < 8; column++) {
    auto pads = panBarPads(panPadToAzimuth(column));
    CHECK(pads[static_cast<size_t>(column)] == PanPad::TIP);
  }
  CHECK_NEAR(panPadToAzimuth(0), -90.0f, 1e-6f);
  CHECK_NEAR(panPadToAzimuth(3), -22.5f, 1e-6f);
  CHECK_NEAR(panPadToAzimuth(4), 22.5f, 1e-6f);
  CHECK_NEAR(panPadToAzimuth(7), 90.0f, 1e-6f);
}
