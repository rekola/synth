#include "TestFramework.h"

#include "../src/instruments/PartialPosition.h"
#include "../src/instruments/SpectralBandProfile.h"

#include <cmath>
#include <vector>

using namespace std;

TEST(partial_position_harmonic_is_identity) {
  PartialPositionSpec spec; // default: Kind::Harmonic
  for (int h = 1; h <= 8; h++) CHECK_NEAR(partialPosition(h, spec), static_cast<float>(h), 1e-6f);
}

TEST(partial_position_integer_list_matches_given_entries) {
  PartialPositionSpec spec;
  spec.kind = PartialPositionSpec::Kind::IntegerList;
  spec.explicit_positions = {1, 2, 4, 5, 7, 9, 11, 13};
  CHECK_NEAR(partialPosition(3, spec), 4.0f, 1e-6f);
  CHECK_NEAR(partialPosition(6, spec), 9.0f, 1e-6f);
  // Past the end falls back to plain harmonic.
  CHECK_NEAR(partialPosition(9, spec), 9.0f, 1e-6f);
}

TEST(partial_position_fractional_stretch_hits_its_own_anchor) {
  // "partial 10 at 10.04"
  float b = stretchCoefficientForAnchor(10, 10.04f);
  PartialPositionSpec spec;
  spec.kind = PartialPositionSpec::Kind::FractionalStretch;
  spec.stretch_b = b;
  CHECK_NEAR(partialPosition(10, spec), 10.04f, 1e-3f);

  float b2 = stretchCoefficientForAnchor(10, 10.06f);
  PartialPositionSpec spec2;
  spec2.kind = PartialPositionSpec::Kind::FractionalStretch;
  spec2.stretch_b = b2;
  CHECK_NEAR(partialPosition(10, spec2), 10.06f, 1e-3f);
}

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

TEST(bells_partial_three_is_tuned_partial_six_is_free) {
  PartialPositionSpec spec;
  spec.kind = PartialPositionSpec::Kind::IntegerList;
  spec.explicit_positions = {1, 2, 4, 5, 7, 9, 11, 13};

  // Partial 3 -> g(3) = 4, oddPart(4) = 1 <= 7 -> tuned to harmonic 4's
  // own 31-EDO position (== 4.0 exactly, since 4 is a power of two).
  float g3 = partialPosition(3, spec);
  float tuned3 = tuningMatchedPartialPosition(g3, 31, true);
  CHECK_NEAR(tuned3, 4.0f, 1e-4f);

  // Partial 6 -> g(6) = 9, oddPart(9) = 9 > 7 -> free, g_h unchanged.
  float g6 = partialPosition(6, spec);
  float free6 = tuningMatchedPartialPosition(g6, 31, true);
  CHECK_NEAR(free6, 9.0f, 1e-6f);
}

TEST(lower_l_frees_more_harmonics) {
  // With L=5, harmonics landing on 7/14/28 (odd part 7) flip from tuned
  // (at the default L=7) to free.
  CHECK(std::fabs(tuningMatchedPartialPosition(7.0f, 31, true, 7) - tuningMatchedPartialPosition(7.0f, 31, true, 7)) < 1e-6f);
  float tuned_at_7 = tuningMatchedPartialPosition(7.0f, 31, true, /* L */ 7);
  float free_at_5 = tuningMatchedPartialPosition(7.0f, 31, true, /* L */ 5);
  CHECK_NEAR(tuned_at_7, std::pow(2.0f, std::round(31.0f * std::log2(7.0f)) / 31.0f), 1e-4f);
  CHECK_NEAR(free_at_5, 7.0f, 1e-6f); // unchanged - freed

  for (float m : {7.0f, 14.0f, 28.0f}) {
    CHECK_NEAR(tuningMatchedPartialPosition(m, 31, true, 5), m, 1e-6f);
  }
}

TEST(stretched_preset_with_tuning_matching_off_keeps_every_g_h) {
  float b = stretchCoefficientForAnchor(10, 10.04f);
  PartialPositionSpec spec;
  spec.kind = PartialPositionSpec::Kind::FractionalStretch;
  spec.stretch_b = b;
  for (int h = 1; h <= 12; h++) {
    float g_h = partialPosition(h, spec);
    CHECK_NEAR(tuningMatchedPartialPosition(g_h, 31, false), g_h, 1e-6f);
  }
}

TEST(bells_ordering_stays_strictly_increasing_after_tuning_matching) {
  PartialPositionSpec spec;
  spec.kind = PartialPositionSpec::Kind::IntegerList;
  spec.explicit_positions = {1, 2, 4, 5, 7, 9, 11, 13};

  float previous = 0.0f;
  for (int h = 1; h <= 8; h++) {
    float g_h = partialPosition(h, spec);
    float position = tuningMatchedPartialPosition(g_h, 31, true);
    CHECK(position > previous);
    previous = position;
  }
}
