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
