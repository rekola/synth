#include "TestFramework.h"

#include "../dsp/FractionalDelayLine.h"

#include <cmath>

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
}

// readCubic() (4-point Lagrange) should track a moving fractional delay
// noticeably more accurately than read()'s linear interpolation - this is
// the whole point of adding it for a modulated (e.g. wow/flutter-style)
// delay read: linear interpolation's aliasing is audible precisely
// because such a read never sits at an integer offset for long. Checked
// against a known sine near Nyquist/4, where linear interpolation's error
// is large enough to measure reliably.
TEST(fractional_delay_line_cubic_read_beats_linear_near_nyquist_quarter) {
  FractionalDelayLine delay(64);
  float fnorm = 0.1f; // cycles/sample - within Nyquist/4 (0.125)
  int n_write = 50;
  for (int n = 0; n < n_write; n++) delay.write(sinf(kTwoPi * fnorm * static_cast<float>(n)));

  float delay_samples = 7.37f; // deliberately non-integer
  float true_value = sinf(kTwoPi * fnorm * (static_cast<float>(n_write - 1) - delay_samples));

  float linear_error = fabsf(delay.read(delay_samples) - true_value);
  float cubic_error = fabsf(delay.readCubic(delay_samples) - true_value);

  CHECK(cubic_error < 0.01f);
  CHECK(cubic_error < linear_error);
}

// At an exact integer delay every interpolator degenerates to reading a
// single real sample - all of readCubic()'s Lagrange weights but one
// vanish at frac = 0 (w0 = 1, the rest exactly 0), so it should reproduce
// read()'s own value exactly, not just approximately.
TEST(fractional_delay_line_cubic_read_matches_linear_at_integer_delay) {
  FractionalDelayLine delay(32);
  for (int n = 0; n < 20; n++) delay.write(sinf(kTwoPi * 0.15f * static_cast<float>(n)));

  CHECK_NEAR(delay.read(5.0f), delay.readCubic(5.0f), 1e-5f);
}
