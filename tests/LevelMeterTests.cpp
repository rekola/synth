#include "TestFramework.h"

#include "../src/ui/LevelMeter.h"

TEST(level_meter_fraction_maps_minus_60_to_0_db_onto_0_to_1) {
  CHECK(level_meter::fraction(-1.0f) == 0.0f); // TrackInfo's "no reading yet"
  CHECK(level_meter::fraction(0.0f) == 0.0f);
  CHECK(level_meter::fraction(0.001f) == 0.0f);                 // -60dB
  CHECK_NEAR(level_meter::fraction(0.01f), 1.0f / 3.0f, 1e-5f); // -40dB
  CHECK_NEAR(level_meter::fraction(0.1f), 2.0f / 3.0f, 1e-5f);  // -20dB
  CHECK_NEAR(level_meter::fraction(1.0f), 1.0f, 1e-5f);
  CHECK(level_meter::fraction(2.0f) == 1.0f); // clipped to full scale
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
