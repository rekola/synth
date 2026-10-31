#include "TestFramework.h"

#include "../src/ui/SpectrumCurve.h"

TEST(spectrum_curve_puts_0_db_at_the_top_and_the_floor_at_the_bottom) {
  std::vector<float> db(512, 0.0f);
  auto loud = spectrum_curve::values(db, 50.0f, 40);
  CHECK(loud.size() == 40u);
  for (float v : loud) CHECK_NEAR(v, 1.0f, 1e-4f);
  std::vector<float> quiet(512, -spectrum_curve::kRangeDb);
  for (float v : spectrum_curve::values(quiet, 50.0f, 40)) CHECK_NEAR(v, 0.0f, 1e-4f);
}

TEST(spectrum_curve_follows_a_tone_along_a_log_frequency_axis) {
  std::vector<float> db(1024, -80.0f);
  db[100] = -10.0f; // 100 bins * 20 Hz = 2 kHz
  auto curve = spectrum_curve::values(db, 20.0f, 64);
  size_t peak = static_cast<size_t>(std::max_element(curve.begin(), curve.end()) - curve.begin());
  // The axis runs from kMinHz to Nyquist (20.48 kHz) in log steps.
  float expected = std::log2(2000.0f / spectrum_curve::kMinHz) / std::log2(20480.0f / spectrum_curve::kMinHz) * 64.0f;
  CHECK_NEAR(static_cast<float>(peak), expected, 2.0f);
}

TEST(spectrum_curve_is_empty_with_nothing_to_show) {
  CHECK(spectrum_curve::values({}, 20.0f, 40).empty());
  CHECK(spectrum_curve::values(std::vector<float>(64, -10.0f), 0.0f, 40).empty());
  CHECK(spectrum_curve::values(std::vector<float>(1, -10.0f), 20.0f, 40).empty()); // Nyquist below the axis
}
