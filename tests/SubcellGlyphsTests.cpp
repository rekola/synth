#include "TestFramework.h"

#include "../src/ui/SubcellGlyphs.h"

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
