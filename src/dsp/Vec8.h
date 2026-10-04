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
typedef double v4d __attribute__((vector_size(32)));
typedef float v4f __attribute__((vector_size(16)));

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

// In-place transpose of an 8x8 block held as eight rows.
inline void transpose8x8(v8f r[8]) {
  const v8f t0 = __builtin_shufflevector(r[0], r[1], 0, 8, 1, 9, 4, 12, 5, 13);
  const v8f t1 = __builtin_shufflevector(r[0], r[1], 2, 10, 3, 11, 6, 14, 7, 15);
  const v8f t2 = __builtin_shufflevector(r[2], r[3], 0, 8, 1, 9, 4, 12, 5, 13);
  const v8f t3 = __builtin_shufflevector(r[2], r[3], 2, 10, 3, 11, 6, 14, 7, 15);
  const v8f t4 = __builtin_shufflevector(r[4], r[5], 0, 8, 1, 9, 4, 12, 5, 13);
  const v8f t5 = __builtin_shufflevector(r[4], r[5], 2, 10, 3, 11, 6, 14, 7, 15);
  const v8f t6 = __builtin_shufflevector(r[6], r[7], 0, 8, 1, 9, 4, 12, 5, 13);
  const v8f t7 = __builtin_shufflevector(r[6], r[7], 2, 10, 3, 11, 6, 14, 7, 15);

  const v8f u0 = __builtin_shufflevector(t0, t2, 0, 1, 8, 9, 4, 5, 12, 13);
  const v8f u1 = __builtin_shufflevector(t0, t2, 2, 3, 10, 11, 6, 7, 14, 15);
  const v8f u2 = __builtin_shufflevector(t1, t3, 0, 1, 8, 9, 4, 5, 12, 13);
  const v8f u3 = __builtin_shufflevector(t1, t3, 2, 3, 10, 11, 6, 7, 14, 15);
  const v8f v0 = __builtin_shufflevector(t4, t6, 0, 1, 8, 9, 4, 5, 12, 13);
  const v8f v1 = __builtin_shufflevector(t4, t6, 2, 3, 10, 11, 6, 7, 14, 15);
  const v8f v2 = __builtin_shufflevector(t5, t7, 0, 1, 8, 9, 4, 5, 12, 13);
  const v8f v3 = __builtin_shufflevector(t5, t7, 2, 3, 10, 11, 6, 7, 14, 15);

  r[0] = __builtin_shufflevector(u0, v0, 0, 1, 2, 3, 8, 9, 10, 11);
  r[4] = __builtin_shufflevector(u0, v0, 4, 5, 6, 7, 12, 13, 14, 15);
  r[1] = __builtin_shufflevector(u1, v1, 0, 1, 2, 3, 8, 9, 10, 11);
  r[5] = __builtin_shufflevector(u1, v1, 4, 5, 6, 7, 12, 13, 14, 15);
  r[2] = __builtin_shufflevector(u2, v2, 0, 1, 2, 3, 8, 9, 10, 11);
  r[6] = __builtin_shufflevector(u2, v2, 4, 5, 6, 7, 12, 13, 14, 15);
  r[3] = __builtin_shufflevector(u3, v3, 0, 1, 2, 3, 8, 9, 10, 11);
  r[7] = __builtin_shufflevector(u3, v3, 4, 5, 6, 7, 12, 13, 14, 15);
}

// In-place transpose of a 4x4 block of doubles held as four rows.
inline void transpose4x4(v4d r[4]) {
  const v4d a0 = __builtin_shufflevector(r[0], r[1], 0, 4, 2, 6);
  const v4d a1 = __builtin_shufflevector(r[0], r[1], 1, 5, 3, 7);
  const v4d a2 = __builtin_shufflevector(r[2], r[3], 0, 4, 2, 6);
  const v4d a3 = __builtin_shufflevector(r[2], r[3], 1, 5, 3, 7);
  r[0] = __builtin_shufflevector(a0, a2, 0, 1, 4, 5);
  r[1] = __builtin_shufflevector(a1, a3, 0, 1, 4, 5);
  r[2] = __builtin_shufflevector(a0, a2, 2, 3, 6, 7);
  r[3] = __builtin_shufflevector(a1, a3, 2, 3, 6, 7);
}

// Largest absolute value in p[0, n).
inline float maxAbs(const float * p, int n) {
  v8f m = splat(0.0f);
  int i = 0;
  for (; i + kLanes <= n; i += kLanes) {
    const v8f v = loadu(p + i);
    const v8f a = (v < splat(0.0f)) ? -v : v;
    m = (a > m) ? a : m;
  }
  float result = hmaxAbs(m);
  for (; i < n; i++) {
    const float a = p[i] < 0.0f ? -p[i] : p[i];
    result = a > result ? a : result;
  }
  return result;
}

// Sum of the squares of p[0, n), in a fixed order (eight running sums, then
// the tail), so the result doesn't depend on how the compiler vectorises.
inline float sumSquares(const float * p, int n) {
  v8f acc = splat(0.0f);
  int i = 0;
  for (; i + kLanes <= n; i += kLanes) {
    const v8f v = loadu(p + i);
    acc += v * v;
  }
  float result = hsum(acc);
  for (; i < n; i++) result += p[i] * p[i];
  return result;
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
