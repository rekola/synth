#ifndef _VEC8_H_
#define _VEC8_H_

#include <cstddef>
#include <cstring>

// Eight floats as one compiler vector (compiled to SSE/AVX/NEON as the
// target allows), plus the few primitives every kernel needs. The width is
// fixed at eight on purpose: kernels pad their state to it, so results never
// depend on the machine or on how a block was cut.
namespace dsp {

constexpr int kLanes = 8;

typedef float v8f __attribute__((vector_size(32)));
typedef int v8i __attribute__((vector_size(32)));

inline v8f splat(float x) { return v8f{ x, x, x, x, x, x, x, x }; }

// 0, 1, ... 7.
inline v8f iota() { return v8f{ 0, 1, 2, 3, 4, 5, 6, 7 }; }

// Unaligned load/store (any float pointer).
inline v8f loadu(const float * p) {
  v8f v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

inline void storeu(float * p, v8f v) { std::memcpy(p, &v, sizeof(v)); }

// Sum of the eight lanes.
inline float hsum(v8f v) { return ((v[0] + v[4]) + (v[1] + v[5])) + ((v[2] + v[6]) + (v[3] + v[7])); }

// Largest absolute value among the eight lanes.
inline float hmaxAbs(v8f v) {
  v8f a = (v < splat(0.0f)) ? -v : v;
  float m = a[0];
  for (int i = 1; i < kLanes; i++) m = a[i] > m ? a[i] : m;
  return m;
}

// dst[i] += (g0 + dg * i) * src[i] for i in [0, n): a gain ramp mixed into
// dst. The ramp is evaluated from i, not accumulated, so it is the same
// wherever a call starts and ends.
inline void rampMix(float * dst, const float * src, float g0, float dg, int n) {
  const v8f lane = iota();
  const v8f g0v = splat(g0), dgv = splat(dg);
  int i = 0;
  for (; i + kLanes <= n; i += kLanes) {
    v8f gain = g0v + dgv * (splat(static_cast<float>(i)) + lane);
    storeu(dst + i, loadu(dst + i) + gain * loadu(src + i));
  }
  for (; i < n; i++) dst[i] += (g0 + dg * static_cast<float>(i)) * src[i];
}

// dst[i] += src[i] * gain.
inline void addScaled(float * dst, const float * src, float gain, int n) {
  const v8f g = splat(gain);
  int i = 0;
  for (; i + kLanes <= n; i += kLanes) storeu(dst + i, loadu(dst + i) + g * loadu(src + i));
  for (; i < n; i++) dst[i] += gain * src[i];
}

// Fractional part of a non-negative phase.
inline v8f fract(v8f p) {
  return p - __builtin_convertvector(__builtin_convertvector(p, v8i), v8f);
}

// sin(2*pi*f) for f in [0, 1): fold to [-1/4, 1/4] turns, then a Taylor
// series through x^13 (error ~1e-9 over the folded range).
inline v8f sineOfFraction(v8f f) {
  constexpr float kTwoPi = 6.28318530717958647692f;
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

#endif
