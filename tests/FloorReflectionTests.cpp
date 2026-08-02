#include "TestFramework.h"

#include "../FloorReflection.h"
#include "../ChannelConfiguration.h"
#include "../Song.h"
#include "../constants.h"

// Worked examples A/B/C/D match the ones hand-computed during planning:
// hl (ear height) = 1.7m, c = 343 m/s, sample rate 48000Hz,
// reflectionStrength (floorReflectionStrength) = 0.4 unless noted.

TEST(floor_reflection_example_a_level_moderate_distance) {
  // d=5, elevation=0 - level with the listener.
  auto g = computeFloorReflectionGeometry(1.7f, 5.0f, 0.0f, 0.4f, 48000.0f);
  CHECK_NEAR(g.delaySamples, 146.4f, 1.0f);       // ~3.05ms @ 48kHz
  CHECK_NEAR(g.gainRatio, 0.3308f, 0.005f);
  CHECK_NEAR(g.elevationDegrees, -34.2f, 0.2f);
}

TEST(floor_reflection_example_b_low_placed_kick) {
  // d=3, elevation=-10 - a plausible slightly-low percussion placement.
  auto g = computeFloorReflectionGeometry(1.7f, 3.0f, -10.0f, 0.4f, 48000.0f);
  CHECK_NEAR(g.delaySamples, 157.4f, 1.0f);        // ~3.28ms @ 48kHz
  CHECK_NEAR(g.gainRatio, 0.2909f, 0.005f);
  CHECK_NEAR(g.elevationDegrees, -44.27f, 0.2f);
}

TEST(floor_reflection_example_c_high_placed_crash) {
  // d=6, elevation=+15 - a plausible slightly-high percussion placement.
  // Steeper/stronger reflection than example B despite being farther
  // away - the whole point of driving this from the resolved position
  // rather than a fixed per-instrument constant.
  auto g = computeFloorReflectionGeometry(1.7f, 6.0f, 15.0f, 0.4f, 48000.0f);
  CHECK_NEAR(g.delaySamples, 227.3f, 1.0f);        // ~4.73ms @ 48kHz
  CHECK_NEAR(g.gainRatio, 0.3148f, 0.005f);
  CHECK_NEAR(g.elevationDegrees, -40.52f, 0.2f);
}

TEST(floor_reflection_distance_to_zero_saturates_delay_and_gain_vanishes) {
  // As distance -> 0, relative delay approaches the theoretical max
  // (2*hl/c) and gain vanishes (d/p -> 0) - a source right at the
  // listener's own position has a maximally-delayed but inaudible
  // reflection.
  auto g = computeFloorReflectionGeometry(1.7f, 0.0001f, 0.0f, 0.4f, 48000.0f);
  float expected_max_delay_samples = 2.0f * 1.7f / kSpeedOfSoundMPerSec * 48000.0f;
  CHECK_NEAR(g.delaySamples, expected_max_delay_samples, 1.0f);
  CHECK(g.gainRatio < 0.001f);
}

TEST(floor_reflection_hs_zero_is_continuous_coincidence) {
  // hl=1.7, d=3.4, elevation=-30 degrees: hs = 1.7 + 3.4*sin(-30) = 0
  // exactly - direct and reflected paths coincide here (p == d,
  // distance_ratio == 1 exactly, and the reflected elevation equals the
  // direct elevation), per the plan's own "clamp is continuous" argument
  // - no discontinuity on either side of this boundary.
  auto g = computeFloorReflectionGeometry(1.7f, 3.4f, -30.0f, 0.4f, 48000.0f);
  CHECK_NEAR(g.delaySamples, 0.0f, 0.5f);
  CHECK_NEAR(g.gainRatio, 0.4f, 0.005f);            // distance_ratio == 1, so gainRatio == reflectionStrength exactly
  CHECK_NEAR(g.elevationDegrees, -30.0f, 0.2f);      // reflected direction coincides with the direct one
}

TEST(floor_reflection_below_floor_clamps_negative_delay_and_gain_over_one) {
  // hl=1.7, d=3, elevation=-90 - "1.3m below the floor" (the plan's own
  // scenario). Without the extra delay/ratio clamps (beyond the hs
  // clamp), this would compute a negative relative delay and a
  // distance_ratio above 1 - both clamped here to a valid, non-negative,
  // non-amplifying tap.
  auto g = computeFloorReflectionGeometry(1.7f, 3.0f, -90.0f, 0.4f, 48000.0f);
  CHECK(g.delaySamples >= 0.0f);
  CHECK_NEAR(g.delaySamples, 0.0f, 0.5f);
  CHECK_NEAR(g.gainRatio, 0.4f, 0.005f);             // distance_ratio clamped to 1, so gainRatio == reflectionStrength
  CHECK_NEAR(g.elevationDegrees, -90.0f, 2.0f);       // straight down, within float precision of cosf(-90deg)
}

TEST(floor_reflection_buffer_sizing_covers_theoretical_max) {
  // hl clamped to the engineering ceiling (50m) - the buffer must cover
  // the theoretical max delay (2*hl/c) at this sample rate regardless of
  // any individual voice's own (always smaller) distance.
  float sample_rate = 48000.0f;
  float hl = 50.0f;
  int buffer_len = floorReflectionMaxDelaySamples(hl, sample_rate);
  float theoretical_max = 2.0f * hl / kSpeedOfSoundMPerSec * sample_rate;
  CHECK(static_cast<float>(buffer_len) >= theoretical_max);

  // And a voice constructed at that same hl, at a very small distance,
  // never asks for more delay than the buffer actually holds.
  auto g = computeFloorReflectionGeometry(hl, 0.01f, 0.0f, 0.4f, sample_rate);
  CHECK(g.delaySamples < static_cast<float>(buffer_len));
}

TEST(floor_reflection_defaults_agree_across_channelconfiguration_and_song) {
  // ChannelConfiguration's own field defaults (used whenever one is
  // constructed without ever loading a Song - tests, offline tools) and
  // a freshly-constructed Song's defaults (used whenever a song's XML
  // doesn't set the attribute) both come from the same constants.h
  // values - this test is what would actually catch it if the two ever
  // drifted apart.
  ChannelConfiguration config;
  Song song;
  CHECK_NEAR(config.getEarHeight(), song.getEarHeight(), 0.0001f);
  CHECK(config.getFloorReflectionEnabled() == song.getFloorReflectionEnabled());
  CHECK_NEAR(config.getFloorReflectionStrength(), song.getFloorReflectionStrength(), 0.0001f);
  CHECK_NEAR(config.getGroundAbsorption(), song.getGroundAbsorption(), 0.0001f);

  CHECK_NEAR(config.getEarHeight(), constants::DEFAULT_EAR_HEIGHT, 0.0001f);
  CHECK(config.getFloorReflectionEnabled() == constants::DEFAULT_FLOOR_REFLECTION_ENABLED);
  CHECK_NEAR(config.getFloorReflectionStrength(), constants::DEFAULT_FLOOR_REFLECTION_STRENGTH, 0.0001f);
  CHECK_NEAR(config.getGroundAbsorption(), constants::DEFAULT_GROUND_ABSORPTION, 0.0001f);
}
