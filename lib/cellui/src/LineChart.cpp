#include <cellui/LineChart.h>

#include <cellui/Plane.h>
#include <cellui/StyleProvider.h>

#include <algorithm>
#include <cmath>

namespace cellui {

size_t
LineChart::suggestedPoints() const {
  auto [ rows, cols ] = getDim();
  (void)rows;
  return std::max<size_t>(static_cast<size_t>(2 * std::max(cols, 0)), 8);
}

bool
LineChart::setValues(const std::vector<float> & values) {
  auto [ rows, cols ] = getDim();
  if (rows <= 0 || cols <= 0 || values.empty()) return false;

  std::vector<float> knots(values.size());
  for (size_t i = 0; i < values.size(); i++) knots[i] = std::clamp(values[i], 0.0f, 1.0f);
  // The curve passes through these points: if none has moved (to a fraction
  // of a pixel), what is drawn would be identical.
  std::vector<int> quantized(knots.size());
  for (size_t i = 0; i < knots.size(); i++) quantized[i] = static_cast<int>(knots[i] * 512.0f + 0.5f);
  auto samples = sampleCount(cols);
  if (quantized == last_values_ && rows == last_rows_ && cols == last_cols_ && samples == last_samples_) return false;
  last_values_ = std::move(quantized);
  last_rows_ = rows;
  last_cols_ = cols;
  last_samples_ = samples;
  drawSamples(splineSample(knots, samples));
  return true;
}

// Samples a Catmull-Rom spline through `knots` (evenly spaced) at `count`
// evenly spaced positions, clamped to 0..1.
std::vector<float>
LineChart::splineSample(const std::vector<float> & knots, size_t count) {
  std::vector<float> out(count, 0.0f);
  if (knots.empty() || count == 0) return out;
  auto at = [&](long i) { return knots[static_cast<size_t>(std::clamp<long>(i, 0, static_cast<long>(knots.size()) - 1))]; };
  for (size_t i = 0; i < count; i++) {
    // Knots sit at cell centres of an n-wide axis.
    float pos = (static_cast<float>(i) + 0.5f) / static_cast<float>(count) * static_cast<float>(knots.size()) - 0.5f;
    long k = static_cast<long>(std::floor(pos));
    float t = pos - static_cast<float>(k);
    float p0 = at(k - 1), p1 = at(k), p2 = at(k + 1), p3 = at(k + 2);
    float v = 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t + (3.0f * (p1 - p2) + p3 - p0) * t * t * t);
    out[i] = std::clamp(v, 0.0f, 1.0f);
  }
  return out;
}

void LineChart::clear() {
  auto [rows, cols] = getDim();
  last_values_.clear(); // what is drawn is no longer the last curve
  if (rows <= 0 || cols <= 0) return;
  drawSamples(std::vector<float>(sampleCount(cols), 0.0f));
}

}  // namespace cellui
