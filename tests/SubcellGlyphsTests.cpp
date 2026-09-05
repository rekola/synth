#include "TestFramework.h"

#include "../src/ui/SubcellGlyphs.h"

#include <vector>

TEST(quadrant_codepoints_mask_zero_and_mask_fifteen_are_space_and_full_block) {
  CHECK(kQuadrantCodepoints[0] == 0x0020);
  CHECK(kQuadrantCodepoints[15] == 0x2588);
}

TEST(quadrant_codepoints_are_all_distinct) {
  for (int a = 0; a < 16; a++) {
    for (int b = a + 1; b < 16; b++) {
      CHECK(kQuadrantCodepoints[a] != kQuadrantCodepoints[b]);
    }
  }
}

TEST(sextant_codepoint_reuses_pre_existing_codepoints_for_its_four_special_masks) {
  CHECK(sextantCodepoint(0) == 0x0020); // all off - space
  CHECK(sextantCodepoint(63) == 0x2588); // all on - full block
  CHECK(sextantCodepoint(21) == 0x258C); // left column only - LEFT HALF BLOCK
  CHECK(sextantCodepoint(42) == 0x2590); // right column only - RIGHT HALF BLOCK
}

TEST(sextant_codepoint_every_other_mask_falls_in_the_legacy_computing_block_and_is_distinct) {
  for (int m = 1; m < 63; m++) {
    if (m == 21 || m == 42) continue;
    auto cp = sextantCodepoint(m);
    CHECK(cp >= 0x1FB00 && cp <= 0x1FB3B);
  }
  for (int a = 0; a < 64; a++) {
    for (int b = a + 1; b < 64; b++) {
      CHECK(sextantCodepoint(a) != sextantCodepoint(b));
    }
  }
}

TEST(braille_codepoint_mask_zero_is_space) {
  CHECK(brailleCodepoint(0) == 0x0020);
}

TEST(braille_codepoint_all_on_is_the_full_eight_dot_cell) {
  CHECK(brailleCodepoint(0xFF) == 0x28FF);
}

// Cross-checks against PatternEditor's own pre-existing VU meter glyphs
// (kGlyphs[], a hand-picked bottom-up fill of the right column only, dots
// 4/5/6/8) - the row-major mask for "just the right column, this many
// rows from the bottom" must permute to exactly those same codepoints.
TEST(braille_codepoint_right_column_masks_match_the_pre_existing_vu_meter_glyphs) {
  CHECK(brailleCodepoint(0x80) == 0x2880);       // row3,col1 only - dot8
  CHECK(brailleCodepoint(0xA0) == 0x28A0);       // + row2,col1 - dots 6,8
  CHECK(brailleCodepoint(0xA8) == 0x28B0);       // + row1,col1 - dots 5,6,8
  CHECK(brailleCodepoint(0xAA) == 0x28B8);       // + row0,col1 - dots 4,5,6,8
}

TEST(braille_codepoint_every_mask_is_distinct_and_in_the_braille_block) {
  for (int a = 0; a < 256; a++) {
    auto cp = brailleCodepoint(a);
    CHECK(a == 0 ? cp == 0x0020 : (cp >= 0x2800 && cp <= 0x28FF));
    for (int b = a + 1; b < 256; b++) {
      CHECK(brailleCodepoint(a) != brailleCodepoint(b));
    }
  }
}

// Every possible split of 4 identical samples ties at zero cost, and the
// all-off mask (0) is tried first and never beaten by a later, merely-
// equal one (strict "<" comparison) - so the "on" group ends up empty
// (its mean defaults to {0,0,0}, having summed zero samples) and the
// "off" group absorbs all four at their own true, uniform color.
TEST(quantize_to_two_colors_uniform_samples_all_land_in_the_off_group) {
  std::vector<SubcellRgb> samples = {{10, 20, 30}, {10, 20, 30}, {10, 20, 30}, {10, 20, 30}};
  SubcellRgb on, off;
  int mask = quantizeToTwoColors(samples, on, off);
  CHECK(mask == 0);
  CHECK_NEAR(on.r, 0.0f, 1e-4f); CHECK_NEAR(on.g, 0.0f, 1e-4f); CHECK_NEAR(on.b, 0.0f, 1e-4f);
  CHECK_NEAR(off.r, 10.0f, 1e-4f); CHECK_NEAR(off.g, 20.0f, 1e-4f); CHECK_NEAR(off.b, 30.0f, 1e-4f);
}

// Samples 0/2 are one color, 1/3 a clearly different one - the exact
// (brute-force) optimum groups them that way at zero cost (each group is
// already uniform), regardless of which bits end up "on" vs "off".
TEST(quantize_to_two_colors_separates_two_distinct_clusters_exactly) {
  std::vector<SubcellRgb> samples = {{0, 0, 0}, {100, 100, 100}, {0, 0, 0}, {100, 100, 100}};
  SubcellRgb on, off;
  int mask = quantizeToTwoColors(samples, on, off);
  for (size_t i = 0; i < samples.size(); i++) {
    auto & mean = (mask & (1 << i)) ? on : off;
    CHECK_NEAR(mean.r, samples[i].r, 1e-4f);
    CHECK_NEAR(mean.g, samples[i].g, 1e-4f);
    CHECK_NEAR(mean.b, samples[i].b, 1e-4f);
  }
  CHECK(off.r != on.r); // genuinely two different colors, not one group absorbing everything
}
