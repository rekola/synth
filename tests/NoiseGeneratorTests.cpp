#include "TestFramework.h"

#include "../dsp/NoiseGenerator.h"
#include "../dsp/PinkNoiseFilter.h"

TEST(noise_generator_is_deterministic_per_seed) {
  NoiseGenerator a(12345), b(12345);
  for (int i = 0; i < 100; i++) {
    CHECK(a.next() == b.next());
  }
}

TEST(noise_generator_different_seeds_decorrelate) {
  NoiseGenerator a(1), b(2);
  bool any_different = false;
  for (int i = 0; i < 100; i++) {
    if (a.next() != b.next()) { any_different = true; break; }
  }
  CHECK(any_different);
}

TEST(noise_generator_output_stays_in_range) {
  NoiseGenerator gen(42);
  for (int i = 0; i < 10000; i++) {
    float v = gen.next();
    CHECK(v >= -1.0f);
    CHECK(v < 1.0f);
  }
}

// Pink noise's -3dB/octave rolloff makes it smoother sample-to-sample than
// flat white noise - measured here via the RMS of the first-difference
// signal (high-frequency content) relative to the RMS of the signal
// itself: white noise's samples are uncorrelated, so its difference
// signal carries close to the same energy as the signal itself, while
// pink noise's correlated, low-pass-leaning samples yield a markedly
// smaller difference-to-signal ratio.
TEST(pink_noise_filter_reduces_high_frequency_content_relative_to_white) {
  NoiseGenerator gen(7);
  PinkNoiseFilter pink;

  const int n = 44100; // >= 1 second at this codebase's standard 44100 Hz
  double white_energy = 0.0, white_diff_energy = 0.0;
  double pink_energy = 0.0, pink_diff_energy = 0.0;
  float prev_white = 0.0f, prev_pink = 0.0f;

  for (int i = 0; i < n; i++) {
    float w = gen.next();
    float p = pink.process(w);

    white_energy += static_cast<double>(w) * w;
    pink_energy += static_cast<double>(p) * p;
    if (i > 0) {
      white_diff_energy += static_cast<double>(w - prev_white) * (w - prev_white);
      pink_diff_energy += static_cast<double>(p - prev_pink) * (p - prev_pink);
    }
    prev_white = w;
    prev_pink = p;
  }

  double white_ratio = white_diff_energy / white_energy;
  double pink_ratio = pink_diff_energy / pink_energy;

  CHECK(pink_ratio < white_ratio * 0.5);
}
