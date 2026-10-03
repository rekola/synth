#include "OscillatorKernel.h"

#include <cmath>

#include <cstring>

namespace {

typedef float v8f __attribute__((vector_size(32)));
typedef int v8i __attribute__((vector_size(32)));

constexpr float kTwoPi = 6.28318530717958647692f;

inline v8f splat(float x) { return v8f{ x, x, x, x, x, x, x, x }; }

// Fractional part of a non-negative phase.
inline v8f fract(v8f p) {
  return p - __builtin_convertvector(__builtin_convertvector(p, v8i), v8f);
}

// sin(2*pi*f) for f in [0, 1): fold to [-1/4, 1/4] turns, then a Taylor
// series through x^13 (error ~1e-9 over the folded range).
inline v8f sineOfFraction(v8f f) {
  const v8f half = splat(0.5f), quarter = splat(0.25f);
  v8f u = (f > half) ? f - splat(1.0f) : f;
  u = (u > quarter) ? half - u : u;
  u = (u < -quarter) ? -half - u : u;

  v8f x = u * splat(kTwoPi);
  v8f x2 = x * x;
  v8f p = splat(1.0f / 6227020800.0f);
  p = p * x2 - splat(1.0f / 39916800.0f);
  p = p * x2 + splat(1.0f / 362880.0f);
  p = p * x2 - splat(1.0f / 5040.0f);
  p = p * x2 + splat(1.0f / 120.0f);
  p = p * x2 - splat(1.0f / 6.0f);
  p = p * x2 + splat(1.0f);
  return x * p;
}

}

namespace oscillator_kernel {

float
sineTurns(float turns) {
  float f = turns - std::floor(turns);
  float u = f > 0.5f ? f - 1.0f : f;
  u = u > 0.25f ? 0.5f - u : u;
  u = u < -0.25f ? -0.5f - u : u;

  float x = u * kTwoPi;
  float x2 = x * x;
  float p = 1.0f / 6227020800.0f;
  p = p * x2 - 1.0f / 39916800.0f;
  p = p * x2 + 1.0f / 362880.0f;
  p = p * x2 - 1.0f / 5040.0f;
  p = p * x2 + 1.0f / 120.0f;
  p = p * x2 - 1.0f / 6.0f;
  p = p * x2 + 1.0f;
  return x * p;
}

void
mix(WaveformType type, float pulse_width, double phase, double rate, float level, int frames, float * out) {
  const v8f lanes = v8f{ 0, 1, 2, 3, 4, 5, 6, 7 } * splat(static_cast<float>(rate));
  const v8f level_v = splat(level);
  const v8f pulse = splat(pulse_width);
  const v8f half = splat(0.5f), one = splat(1.0f);

  const size_t groups = paddedFrames(frames) / kLanes;
  for (size_t g = 0; g < groups; g++) {
    // Each group of eight starts from a double-derived phase, so float
    // rounding never accumulates across the block.
    double start = phase + rate * static_cast<double>(g * kLanes);
    start -= std::floor(start);
    v8f f = fract(splat(static_cast<float>(start)) + lanes);

    v8f a;
    switch (type) {
    case WaveformType::SINE: a = sineOfFraction(f); break;
    case WaveformType::SAW: a = (f < half) ? f * splat(2.0f) : f * splat(2.0f) - splat(2.0f); break;
    case WaveformType::TRIANGLE: a = (f < half) ? one - f * splat(4.0f) : f * splat(4.0f) - splat(3.0f); break;
    case WaveformType::SQUARE: a = (f < pulse) ? -one : one; break;
    default: a = splat(0.0f); break;
    }

    v8f existing;
    std::memcpy(&existing, out + g * kLanes, sizeof(existing));
    a = a * level_v + existing;
    std::memcpy(out + g * kLanes, &a, sizeof(a));
  }
}

}
