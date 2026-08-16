#include "TestFramework.h"

#include "../src/dsp/TapeTransport.h"

#include <algorithm>
#include <cmath>

// Poisson event rate: run the transport standalone (no AudioBuffer/
// position awareness at all - see dsp/TapeTransport.h's own doc comment)
// for a fixed simulated duration at a fixed seed, count dropout/click
// trigger events, and check the count against rate * duration within a
// generous statistical tolerance. healthSensitivity is 0 here so the
// effective rate stays exactly dropoutRateHz/clickRateHz throughout (no
// health-driven rate scaling to confound the expected count).
TEST(tape_transport_dropout_rate_matches_expected_poisson_mean) {
  TapeTransportDsp transport(12345);
  TapeTransportParams params;
  params.healthSensitivity = 0.0f;
  params.dropoutRateHz = 2.0f;
  params.clickRateHz = 0.0f;
  params.hissLevelDB = -120.0f; // irrelevant to this test, kept quiet

  float sampleRate = 2000.0f; // coarse - only the event timing matters here
  float duration_s = 200.0f;
  int n = static_cast<int>(duration_s * sampleRate);

  int events = 0;
  float prev_gain = 1.0f;
  for (int i = 0; i < n; i++) {
    auto s = transport.nextSample(params, sampleRate);
    if (prev_gain >= 1.0f && s.dropoutGain < 1.0f) events++;
    prev_gain = s.dropoutGain;
  }

  float expected = params.dropoutRateHz * duration_s; // 400
  CHECK_NEAR(static_cast<float>(events), expected, expected * 0.25f); // Poisson std ~ sqrt(400) = 20, so 25% (100) is a very safe margin
}

TEST(tape_transport_click_rate_matches_expected_poisson_mean) {
  TapeTransportDsp transport(999);
  TapeTransportParams params;
  params.healthSensitivity = 0.0f;
  params.dropoutRateHz = 0.0f;
  params.clickRateHz = 3.0f;
  params.hissLevelDB = -120.0f;

  float sampleRate = 2000.0f;
  float duration_s = 200.0f;
  int n = static_cast<int>(duration_s * sampleRate);

  int events = 0;
  for (int i = 0; i < n; i++) {
    auto s = transport.nextSample(params, sampleRate);
    if (s.clickImpulse != 0.0f) events++;
  }

  float expected = params.clickRateHz * duration_s; // 600
  CHECK_NEAR(static_cast<float>(events), expected, expected * 0.25f);
}

// A rate of 0 disables its Poisson process entirely (drawExponentialSeconds()'s
// own "never" fallback) - guards against a future change accidentally
// treating 0 as "as fast as possible" instead.
TEST(tape_transport_zero_rate_never_triggers) {
  TapeTransportDsp transport(1);
  TapeTransportParams params;
  params.dropoutRateHz = 0.0f;
  params.clickRateHz = 0.0f;
  params.hissLevelDB = -120.0f;

  float sampleRate = 48000.0f;
  bool any_dropout = false, any_click = false;
  for (int i = 0; i < 480000; i++) { // 10 simulated seconds
    auto s = transport.nextSample(params, sampleRate);
    if (s.dropoutGain < 1.0f) any_dropout = true;
    if (s.clickImpulse != 0.0f) any_click = true;
  }
  CHECK(!any_dropout);
  CHECK(!any_click);
}

// Health stays within [1 - healthSensitivity, 1] (the range the raw
// low-passed noise walk is deliberately mapped into - see
// dsp/TapeTransport.cpp) regardless of how long the transport runs, and
// every other per-sample output stays finite - a defensive bounds check
// against runaway/NaN state over a long note.
TEST(tape_transport_health_and_outputs_stay_bounded) {
  TapeTransportDsp transport(42);
  TapeTransportParams params;
  params.healthSensitivity = 0.7f;
  params.healthRateHz = 1.0f; // fast wandering, to actually explore the range within the test's duration
  params.wowDepthCents = 60.0f;
  params.flutterDepthCents = 30.0f;
  params.dropoutRateHz = 5.0f;
  params.clickRateHz = 5.0f;

  float sampleRate = 48000.0f;
  for (int i = 0; i < 480000; i++) { // 10 simulated seconds
    auto s = transport.nextSample(params, sampleRate);
    CHECK(std::isfinite(s.pitchDeviationCents));
    CHECK(std::isfinite(s.dropoutGain));
    CHECK(std::isfinite(s.clickImpulse));
    CHECK(std::isfinite(s.hiss));
    CHECK(s.health <= 1.0f + 1e-4f);
    CHECK(s.health >= 1.0f - params.healthSensitivity - 1e-4f);
    CHECK(s.dropoutGain > 0.0f);
    CHECK(s.dropoutGain <= 1.0f);
  }
}

TEST(tape_transport_is_deterministic_per_seed) {
  TapeTransportParams params;
  TapeTransportDsp a(555), b(555);
  for (int i = 0; i < 1000; i++) {
    auto sa = a.nextSample(params, 48000.0f);
    auto sb = b.nextSample(params, 48000.0f);
    CHECK(sa.pitchDeviationCents == sb.pitchDeviationCents);
    CHECK(sa.hiss == sb.hiss);
  }
}

// Disintegration: decayMode should make health trend monotonically toward
// 0 as elapsed time grows, layered under (not replacing) the normal
// random wander - checked by comparing average health over the first vs.
// last third of a long run, with healthRateHz slowed down so the random
// wander itself doesn't dominate the comparison.
TEST(tape_transport_decay_mode_erodes_health_over_time) {
  TapeTransportDsp transport(7);
  TapeTransportParams params;
  params.healthRateHz = 0.02f; // slow wander, so decay is what dominates the trend
  params.healthSensitivity = 0.2f;
  params.decayMode = true;
  params.decayRatePerMinute = 6.0f; // fully eroded (decay_offset = 1) by 10s

  float sampleRate = 4000.0f;
  int n = static_cast<int>(30.0f * sampleRate); // 30 simulated seconds
  double sum_first_third = 0.0, sum_last_third = 0.0;
  int third = n / 3;
  for (int i = 0; i < n; i++) {
    auto s = transport.nextSample(params, sampleRate);
    if (i < third) sum_first_third += s.health;
    else if (i >= n - third) sum_last_third += s.health;
  }
  float avg_first = static_cast<float>(sum_first_third / third);
  float avg_last = static_cast<float>(sum_last_third / third);

  CHECK(avg_last < avg_first - 0.3f); // clearly eroded, not just noise
  CHECK(avg_last >= -1e-3f); // health clamped at 0, never negative
}

// decayMode false (default) should leave health's usual floor
// (1 - healthSensitivity) intact even over a long run - guards against
// decay_offset leaking in when the mode is off.
TEST(tape_transport_decay_mode_off_does_not_erode_health) {
  TapeTransportDsp transport(8);
  TapeTransportParams params;
  params.healthSensitivity = 0.3f;
  params.decayRatePerMinute = 6.0f; // set, but decayMode stays false - should be ignored entirely

  float sampleRate = 4000.0f;
  int n = static_cast<int>(30.0f * sampleRate);
  float min_health = 2.0f;
  for (int i = 0; i < n; i++) {
    auto s = transport.nextSample(params, sampleRate);
    min_health = std::min(min_health, s.health);
  }
  CHECK(min_health >= 1.0f - params.healthSensitivity - 1e-3f);
}

// Vinyl: wowLocked should produce a purely deterministic, rate-fixed
// pitch-deviation curve, unaffected by healthSensitivity/healthRateHz
// (a turntable's rotational wow doesn't wander with transport "trouble")
// - two instances with different seeds should agree exactly on wow's own
// contribution once flutter/health-driven components are zeroed out.
TEST(tape_transport_locked_wow_is_seed_independent) {
  TapeTransportParams params;
  params.wowLocked = true;
  params.wowLockedRateHz = 0.5556f;
  params.wowDepthCents = 15.0f;
  params.flutterDepthCents = 0.0f; // isolate wow alone
  params.hissLevelDB = -120.0f;

  TapeTransportDsp a(1), b(2); // different seeds
  float sampleRate = 4000.0f;
  for (int i = 0; i < 40000; i++) {
    auto sa = a.nextSample(params, sampleRate);
    auto sb = b.nextSample(params, sampleRate);
    CHECK_NEAR(sa.pitchDeviationCents, sb.pitchDeviationCents, 1e-4f);
  }
}
