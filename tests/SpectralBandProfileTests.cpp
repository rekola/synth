#include "TestFramework.h"

#include "../src/instruments/SpectralBandProfile.h"

#include <cmath>

TEST(odd_part_strips_factors_of_two) {
  CHECK(oddPart(1) == 1);
  CHECK(oddPart(2) == 1);
  CHECK(oddPart(4) == 1);
  CHECK(oddPart(24) == 3);
  CHECK(oddPart(32) == 1);
  CHECK(oddPart(7) == 7);
  CHECK(oddPart(28) == 7);
}

TEST(tuning_matched_partial_position_off_or_no_edo_is_identity) {
  CHECK_NEAR(tuningMatchedPartialPosition(9.0f, 31, false), 9.0f, 1e-6f);
  CHECK_NEAR(tuningMatchedPartialPosition(9.0f, 0, true), 9.0f, 1e-6f);
}

TEST(lower_l_frees_more_harmonics) {
  // With L=5, harmonics landing on 7/14/28 (odd part 7) flip from tuned
  // (at the default L=7) to free.
  float tuned_at_7 = tuningMatchedPartialPosition(7.0f, 31, true, /* L */ 7);
  float free_at_5 = tuningMatchedPartialPosition(7.0f, 31, true, /* L */ 5);
  CHECK_NEAR(tuned_at_7, std::pow(2.0f, std::round(31.0f * std::log2(7.0f)) / 31.0f), 1e-4f);
  CHECK_NEAR(free_at_5, 7.0f, 1e-6f); // unchanged - freed

  for (float m : {7.0f, 14.0f, 28.0f}) {
    CHECK_NEAR(tuningMatchedPartialPosition(m, 31, true, 5), m, 1e-6f);
  }
}
