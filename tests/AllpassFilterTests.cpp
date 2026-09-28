#include "TestFramework.h"

#include "../src/dsp/AllpassFilter.h"

#include <cmath>
#include <vector>

TEST(allpass_stage_passes_a_sine_at_unity_gain) {
  // The defining property of an allpass filter: every frequency passes at
  // exactly unity gain (only phase changes) - checked by comparing the
  // steady-state RMS of a sine run through several periods against the
  // input's own RMS, once the filter's own transient has settled.
  const float sample_rate = 44100.0f;
  const float freq = 440.0f;
  const float fc = 1000.0f; // stage's own crossover, deliberately away from freq
  float a = AllpassStage<float>::coefficientFor(fc, sample_rate);

  AllpassStage<float> stage;
  int n = 4410; // 0.1s - many periods at 440Hz, plenty for the transient to settle
  int settle = 200;
  double sum_in = 0.0, sum_out = 0.0;
  int counted = 0;
  for (int i = 0; i < n; i++) {
    float x = std::sin(2.0f * static_cast<float>(M_PI) * freq * static_cast<float>(i) / sample_rate);
    float y = stage.process(x, a);
    if (i >= settle) {
      sum_in += x * x;
      sum_out += y * y;
      counted++;
    }
  }
  double rms_in = std::sqrt(sum_in / counted);
  double rms_out = std::sqrt(sum_out / counted);
  CHECK_NEAR(static_cast<float>(rms_out), static_cast<float>(rms_in), 0.01f);
}

TEST(allpass_stage_shifts_phase_near_its_own_crossover) {
  // At fc itself, a first-order allpass shifts phase by exactly -90
  // degrees (a quarter period) in steady state - checked by cross-
  // correlating the output against a quarter-period-delayed copy of the
  // input sine (the expected output shape) and confirming they line up
  // far better than against the undelayed input.
  const float sample_rate = 44100.0f;
  const float freq = 1000.0f;
  float a = AllpassStage<float>::coefficientFor(freq, sample_rate);

  AllpassStage<float> stage;
  int n = 4410;
  int settle = 400;
  std::vector<float> out(static_cast<size_t>(n));
  for (int i = 0; i < n; i++) {
    float x = std::sin(2.0f * static_cast<float>(M_PI) * freq * static_cast<float>(i) / sample_rate);
    out[static_cast<size_t>(i)] = stage.process(x, a);
  }

  auto quarterDelayed = [&](int i) {
    float samples_per_period = sample_rate / freq;
    float delayed_i = static_cast<float>(i) - samples_per_period / 4.0f;
    return std::sin(2.0f * static_cast<float>(M_PI) * freq * delayed_i / sample_rate);
  };

  double err_delayed = 0.0, err_undelayed = 0.0;
  for (int i = settle; i < n; i++) {
    float x = std::sin(2.0f * static_cast<float>(M_PI) * freq * static_cast<float>(i) / sample_rate);
    float d = out[static_cast<size_t>(i)] - quarterDelayed(i);
    float u = out[static_cast<size_t>(i)] - x;
    err_delayed += d * d;
    err_undelayed += u * u;
  }
  CHECK(err_delayed < err_undelayed * 0.05);
}
