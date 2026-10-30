#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// The live spectrum as a curve for a LineChart: log-frequency from kMinHz up
// to Nyquist, `points` values 0..1, kRangeDb of level below 0 dBFS at the top.
namespace spectrum_curve {

constexpr float kRangeDb = 80.0f;
constexpr float kMinHz = 40.0f;

// `db` is dBFS per linear frequency bin `bin_hz` apart from DC. Empty if
// there is nothing to show.
inline std::vector<float> values(const std::vector<float> & db, float bin_hz, size_t points) {
  float nyquist = static_cast<float>(db.size()) * bin_hz;
  if (db.empty() || points == 0 || bin_hz <= 0.0f || nyquist <= kMinHz) return {};

  // A point spanning several linear bins shows their mean power; one narrower
  // than a linear bin (the bass) interpolates between its neighbours instead
  // of going blank.
  float start = std::log2(kMinHz), step = (std::log2(nyquist) - start) / static_cast<float>(points);
  auto level = [&](float index) {
    float clamped = std::clamp(index, 0.0f, static_cast<float>(db.size() - 1));
    size_t lo = static_cast<size_t>(clamped), hi = std::min(lo + 1, db.size() - 1);
    float frac = clamped - static_cast<float>(lo);
    return db[lo] * (1.0f - frac) + db[hi] * frac;
  };
  std::vector<float> out(points, 0.0f);
  for (size_t b = 0; b < points; b++) {
    float lo = std::exp2(start + step * static_cast<float>(b)) / bin_hz;
    float hi = std::exp2(start + step * static_cast<float>(b + 1)) / bin_hz;
    float value;
    if (hi - lo < 1.0f) {
      value = level(0.5f * (lo + hi));
    } else {
      // Mean power, not the max: a wide point of noise-like content would
      // otherwise always read at its loudest component.
      float power = 0.0f;
      size_t count = 0;
      for (size_t i = static_cast<size_t>(lo); i < db.size() && static_cast<float>(i) < hi; i++, count++) power += std::pow(10.0f, db[i] / 10.0f);
      value = count > 0 ? 10.0f * std::log10(std::max(power / static_cast<float>(count), 1e-12f)) : -kRangeDb;
    }
    out[b] = std::clamp(1.0f + value / kRangeDb, 0.0f, 1.0f);
  }
  return out;
}

} // namespace spectrum_curve
