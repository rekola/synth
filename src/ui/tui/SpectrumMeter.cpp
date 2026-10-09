#include "SpectrumMeter.h"

#include "../UIPlane.h"
#include "../StyleProvider.h"

#include <algorithm>
#include <cmath>

bool
SpectrumMeter::setSpectrum(const std::vector<float> & db, float bin_hz) {
  auto [ rows, cols ] = getDim();
  if (rows <= 0 || cols <= 0 || db.empty()) return false;

  float nyquist = static_cast<float>(db.size()) * bin_hz;
  if (bin_hz <= 0.0f || nyquist <= kMinHz) return false;

  // Log-frequency display bins. A display bin spanning several linear bins
  // shows their mean power; one narrower than a linear bin (the bass) interpolates
  // between its neighbours instead of going blank.
  // Two knots per cell; the spline interpolates between them for the
  // finer pixel renderer.
  size_t num_bins = std::max<size_t>(static_cast<size_t>(2 * cols), 8);
  float start = std::log2(kMinHz), step = (std::log2(nyquist) - start) / static_cast<float>(num_bins);
  auto level = [&](float index) {
    float clamped = std::clamp(index, 0.0f, static_cast<float>(db.size() - 1));
    size_t lo = static_cast<size_t>(clamped), hi = std::min(lo + 1, db.size() - 1);
    float frac = clamped - static_cast<float>(lo);
    return db[lo] * (1.0f - frac) + db[hi] * frac;
  };
  std::vector<float> fractions(num_bins, 0.0f);
  for (size_t b = 0; b < num_bins; b++) {
    float lo = std::exp2(start + step * static_cast<float>(b)) / bin_hz;
    float hi = std::exp2(start + step * static_cast<float>(b + 1)) / bin_hz;
    float value;
    if (hi - lo < 1.0f) {
      value = level(0.5f * (lo + hi));
    } else {
      // Mean power, not the max: a wide bin of noise-like content would
      // otherwise always read at its loudest component.
      float power = 0.0f;
      size_t count = 0;
      for (size_t i = static_cast<size_t>(lo); i < db.size() && static_cast<float>(i) < hi; i++, count++) power += std::pow(10.0f, db[i] / 10.0f);
      value = count > 0 ? 10.0f * std::log10(std::max(power / static_cast<float>(count), 1e-12f)) : -kRangeDb;
    }
    fractions[b] = std::clamp(1.0f + value / kRangeDb, 0.0f, 1.0f);
  }
  // The curve passes through these points: if none has moved (to a fraction
  // of a pixel), what is drawn would be identical.
  std::vector<int> quantized(fractions.size());
  for (size_t b = 0; b < fractions.size(); b++) quantized[b] = static_cast<int>(fractions[b] * 512.0f + 0.5f);
  auto bars = barCount(cols);
  if (quantized == last_knots_ && rows == last_rows_ && cols == last_cols_ && bars == last_bars_) return false;
  last_knots_ = std::move(quantized);
  last_rows_ = rows;
  last_cols_ = cols;
  last_bars_ = bars;
  drawBars(splineSample(fractions, bars));
  return true;
}

// Samples a Catmull-Rom spline through `knots` (evenly spaced) at `count`
// evenly spaced positions, clamped to 0..1.
std::vector<float>
SpectrumMeter::splineSample(const std::vector<float> & knots, size_t count) {
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

void
SpectrumMeter::drawBars(const std::vector<float> & levels) {
  auto [ rows, cols ] = getDim();
  auto & styles = getPlane().getStyles();
  setBgColor(styles.window_bg_color);
  fill();
  for (int c = 0; c < cols; c++) {
    auto column = [&](size_t bin) {
      return level_meter::BarColumn{level_meter::barSteps(levels[bin], rows), 0};
    };
    auto bar = level_meter::verticalBar(level_meter::Glyphs::BRAILLE, rows, column(static_cast<size_t>(2 * c)), column(static_cast<size_t>(2 * c + 1)));
    for (int i = 0; i < rows; i++) {
      // Shaded by the height of this cell; i = 0 is the top one.
      setFgColor(styles.spectrumColor((static_cast<float>(rows - i) - 0.5f) / static_cast<float>(rows)));
      putstr(i, c, bar[static_cast<size_t>(i)]);
    }
  }
}
