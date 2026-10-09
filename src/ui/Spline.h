#ifndef _SPLINE_H_
#define _SPLINE_H_

#include <algorithm>
#include <cmath>
#include <vector>

namespace spline {

// Samples a Catmull-Rom spline through `knots` (evenly spaced) at `count`
// evenly spaced positions, clamped to 0..1. Knots sit at the centres of
// an axis `knots.size()` cells wide.
inline std::vector<float> sample(const std::vector<float> & knots, size_t count) {
  std::vector<float> out(count, 0.0f);
  if (knots.empty() || count == 0) return out;
  auto at = [&](long i) { return knots[static_cast<size_t>(std::clamp<long>(i, 0, static_cast<long>(knots.size()) - 1))]; };
  for (size_t i = 0; i < count; i++) {
    float pos = (static_cast<float>(i) + 0.5f) / static_cast<float>(count) * static_cast<float>(knots.size()) - 0.5f;
    long k = static_cast<long>(std::floor(pos));
    float t = pos - static_cast<float>(k);
    float p0 = at(k - 1), p1 = at(k), p2 = at(k + 1), p3 = at(k + 2);
    float v = 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t + (3.0f * (p1 - p2) + p3 - p0) * t * t * t);
    out[i] = std::clamp(v, 0.0f, 1.0f);
  }
  return out;
}

}

#endif
