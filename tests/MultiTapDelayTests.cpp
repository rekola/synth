#include "TestFramework.h"

#include "../bus/MultiTapDelay.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace std;

namespace {

// Renders `frames` samples of a unit impulse (1.0 at sample 0, silence
// after) through `delay`, returning each tap's full output concatenated -
// same technique as FDNReverbTests.cpp's renderImpulse().
vector<vector<float>> renderImpulse(MultiTapDelay & delay, int frames) {
  vector<float> input(static_cast<size_t>(frames), 0.0f);
  input[0] = 1.0f;

  vector<vector<float>> taps(static_cast<size_t>(MultiTapDelay::kNumTaps));
  int block = 512;
  for (int offset = 0; offset < frames; offset += block) {
    int n = min(block, frames - offset);
    delay.process(input.data() + offset, n);
    for (int t = 0; t < MultiTapDelay::kNumTaps; t++) {
      auto tap = delay.getTap(t);
      taps[static_cast<size_t>(t)].insert(taps[static_cast<size_t>(t)].end(), tap, tap + n);
    }
  }
  return taps;
}

int firstNonZero(const vector<float> & tap) {
  for (size_t i = 0; i < tap.size(); i++) {
    if (tap[i] != 0.0f) return static_cast<int>(i);
  }
  return -1;
}

}

TEST(multi_tap_delay_resolves_taps_at_1_2_4_8x_base_interval) {
  int sampleRate = 44100;
  MultiTapDelay delay(sampleRate);
  // baseRows=1, rowDuration=1/100th of a second -> tap 0 (1x) lands at
  // ~441 samples, tap 3 (8x) at ~3528 samples.
  float rowDuration = 0.01f;
  delay.setParameters(1.0f, 0.5f, 0.3f, DelayPattern::Static, 18.0f);
  delay.setRowDuration(rowDuration);

  auto taps = renderImpulse(delay, sampleRate);
  int onset[MultiTapDelay::kNumTaps];
  for (int t = 0; t < MultiTapDelay::kNumTaps; t++) {
    onset[t] = firstNonZero(taps[static_cast<size_t>(t)]);
    CHECK(onset[t] > 0);
  }

  // Each tap's onset should be at (1 << t) times the base interval, within
  // rounding.
  float expected0 = rowDuration * static_cast<float>(sampleRate);
  for (int t = 0; t < MultiTapDelay::kNumTaps; t++) {
    float expected = expected0 * static_cast<float>(1 << t);
    CHECK_NEAR(static_cast<float>(onset[t]), expected, 2.0f);
  }
}

TEST(multi_tap_delay_clamps_resolved_time_at_kMaxDelaySeconds) {
  int sampleRate = 44100;
  MultiTapDelay delay(sampleRate);
  // An extreme (very slow) tempo: rowDuration alone (for the 1x tap)
  // already exceeds kMaxDelaySeconds once multiplied by the 8x tap's
  // ratio - every tap's onset should clamp at the same ceiling rather
  // than growing unbounded (no minimum-tempo floor exists elsewhere).
  delay.setParameters(1.0f, 0.0f, 0.3f, DelayPattern::Static, 18.0f);
  delay.setRowDuration(10.0f);

  // Render comfortably past kMaxDelaySeconds so a correctly-clamped onset
  // (right at the ceiling) still falls within the rendered window.
  auto taps = renderImpulse(delay, sampleRate * 3);
  int maxSamples = static_cast<int>(MultiTapDelay::kMaxDelaySeconds * static_cast<float>(sampleRate));
  for (int t = 0; t < MultiTapDelay::kNumTaps; t++) {
    int onset = firstNonZero(taps[static_cast<size_t>(t)]);
    CHECK(onset > 0);
    CHECK(onset <= maxSamples);
  }
}

TEST(multi_tap_delay_static_pattern_never_moves_feedback_tap) {
  int sampleRate = 44100;
  MultiTapDelay delay(sampleRate);
  // Small, exact 100-sample feedback-tap length: baseRows=1, ratio 8,
  // rowDuration chosen so 1*8*rowDuration*sampleRate == 100.
  float rowDuration = 100.0f / 8.0f / static_cast<float>(sampleRate);
  delay.setParameters(1.0f, 0.9f, 0.0f, DelayPattern::Static, 18.0f);
  delay.setRowDuration(rowDuration);

  auto before = delay.getTapDirection(MultiTapDelay::kNumTaps - 1);
  vector<float> silence(300, 0.0f);
  delay.process(silence.data(), static_cast<int>(silence.size())); // 3 passes
  auto after = delay.getTapDirection(MultiTapDelay::kNumTaps - 1);

  CHECK_NEAR(before.azimuth, after.azimuth, 0.0001f);
  CHECK_NEAR(before.elevation, after.elevation, 0.0001f);
  CHECK_NEAR(1.0f, delay.getFeedbackGainMultiplier(), 0.0001f);
}

TEST(multi_tap_delay_pingpong_flips_azimuth_sign_each_pass) {
  int sampleRate = 44100;
  MultiTapDelay delay(sampleRate);
  float rowDuration = 100.0f / 8.0f / static_cast<float>(sampleRate);
  delay.setParameters(1.0f, 0.9f, 0.0f, DelayPattern::PingPong, 18.0f);
  delay.setRowDuration(rowDuration);

  float initial = delay.getTapDirection(MultiTapDelay::kNumTaps - 1).azimuth;

  vector<float> silence(100, 0.0f);
  delay.process(silence.data(), 100); // 1 pass
  CHECK_NEAR(-initial, delay.getTapDirection(MultiTapDelay::kNumTaps - 1).azimuth, 0.001f);

  delay.process(silence.data(), 100); // 2 passes
  CHECK_NEAR(initial, delay.getTapDirection(MultiTapDelay::kNumTaps - 1).azimuth, 0.001f);
}

TEST(multi_tap_delay_orbit_rotates_azimuth_by_pattern_speed_per_pass) {
  int sampleRate = 44100;
  MultiTapDelay delay(sampleRate);
  float rowDuration = 100.0f / 8.0f / static_cast<float>(sampleRate);
  float speed = 18.0f;
  delay.setParameters(1.0f, 0.9f, 0.0f, DelayPattern::Orbit, speed);
  delay.setRowDuration(rowDuration);

  float initial = delay.getTapDirection(MultiTapDelay::kNumTaps - 1).azimuth;

  vector<float> silence(100, 0.0f);
  delay.process(silence.data(), 100); // 1 pass
  CHECK_NEAR(initial + speed, delay.getTapDirection(MultiTapDelay::kNumTaps - 1).azimuth, 0.001f);

  delay.process(silence.data(), 100); // 2 passes
  CHECK_NEAR(initial + 2.0f * speed, delay.getTapDirection(MultiTapDelay::kNumTaps - 1).azimuth, 0.001f);
}

TEST(multi_tap_delay_recede_compounds_gain_and_elevation_per_pass) {
  int sampleRate = 44100;
  MultiTapDelay delay(sampleRate);
  float rowDuration = 100.0f / 8.0f / static_cast<float>(sampleRate);
  float speed = 18.0f;
  delay.setParameters(1.0f, 0.9f, 0.0f, DelayPattern::Recede, speed);
  delay.setRowDuration(rowDuration);

  float expectedGainStep = 1.0f - speed / 100.0f;
  float expectedElevStep = -speed / 4.0f;

  vector<float> silence(100, 0.0f);
  delay.process(silence.data(), 100); // 1 pass
  CHECK_NEAR(expectedGainStep, delay.getFeedbackGainMultiplier(), 0.001f);
  CHECK_NEAR(expectedElevStep, delay.getTapDirection(MultiTapDelay::kNumTaps - 1).elevation, 0.001f);

  delay.process(silence.data(), 100); // 2 passes
  CHECK_NEAR(expectedGainStep * expectedGainStep, delay.getFeedbackGainMultiplier(), 0.001f);
  CHECK_NEAR(2.0f * expectedElevStep, delay.getTapDirection(MultiTapDelay::kNumTaps - 1).elevation, 0.001f);
}

TEST(multi_tap_delay_recede_elevation_clamps_at_90_degrees) {
  int sampleRate = 44100;
  MultiTapDelay delay(sampleRate);
  float rowDuration = 100.0f / 8.0f / static_cast<float>(sampleRate);
  // A large pattern speed drives a single pass's elevation step (-speed/4)
  // well past -90 on its own - must clamp, not wrap or go unbounded.
  delay.setParameters(1.0f, 0.9f, 0.0f, DelayPattern::Recede, 1000.0f);
  delay.setRowDuration(rowDuration);

  vector<float> silence(100, 0.0f);
  delay.process(silence.data(), 100); // 1 pass
  CHECK_NEAR(-90.0f, delay.getTapDirection(MultiTapDelay::kNumTaps - 1).elevation, 0.001f);
}

TEST(multi_tap_delay_stable_across_parameter_extremes) {
  // Every combination stays finite/bounded and the tail eventually decays
  // well below its own peak, for all four pattern modes - mirrors
  // fdn_reverb_stable_across_parameter_extremes.
  struct Case { float baseRows, feedback, damping, patternSpeed; DelayPattern pattern; };
  Case cases[] = {
    { 0.01f, 0.0f, 0.0f, 0.0f, DelayPattern::Static },
    { 4.0f, 0.95f, 1.0f, 0.0f, DelayPattern::Static },
    { 0.1875f, 0.95f, 0.3f, 18.0f, DelayPattern::PingPong },
    { 0.1875f, 0.95f, 0.3f, 90.0f, DelayPattern::Orbit },
    { 0.1875f, 0.95f, 0.3f, 200.0f, DelayPattern::Recede },
  };

  int sampleRate = 44100;
  int frames = sampleRate * 2;
  int window = 4410; // 100ms

  for (auto & c : cases) {
    MultiTapDelay delay(sampleRate);
    delay.setParameters(c.baseRows, c.feedback, c.damping, c.pattern, c.patternSpeed);
    delay.setRowDuration(60.0f / 4.0f / 120.0f);
    auto taps = renderImpulse(delay, frames);

    vector<double> window_energy(static_cast<size_t>(frames / window), 0.0);
    for (auto & tap : taps) {
      for (int i = 0; i < frames; i++) {
	float v = tap[static_cast<size_t>(i)];
	CHECK(isfinite(v));
	CHECK(fabs(v) < 100.0f);
	window_energy[static_cast<size_t>(i / window)] += static_cast<double>(v) * v;
      }
    }

    double peak = *max_element(window_energy.begin(), window_energy.end());
    double final_window = window_energy.back();
    CHECK(peak > 0.0);
    CHECK(final_window < peak * 0.5); // decaying, not sustaining/growing
  }
}
