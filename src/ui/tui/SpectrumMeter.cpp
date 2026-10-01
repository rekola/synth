#include "SpectrumMeter.h"

#include "../UIPlane.h"
#include "../StyleProvider.h"

#include <algorithm>
#include <cmath>

void
SpectrumMeter::setSpectrum(const std::vector<float> & db, float bin_hz) {
  auto [ rows, cols ] = getDim();
  if (rows <= 0 || cols <= 0 || db.empty()) return;

  auto now = std::chrono::steady_clock::now();
  auto dt = std::min(std::chrono::duration<float>(now - last_update_).count(), 0.25f);
  last_update_ = now;

  float nyquist = static_cast<float>(db.size()) * bin_hz;
  if (bin_hz <= 0.0f || nyquist <= kMinHz) return;

  // Log-frequency display bins. A display bin spanning several linear bins
  // shows the loudest; one narrower than a linear bin (the bass) interpolates
  // between its neighbours instead of going blank.
  size_t num_bins = static_cast<size_t>(2 * cols);
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
      value = -kRangeDb;
      for (size_t i = static_cast<size_t>(lo); i < db.size() && static_cast<float>(i) < hi; i++) value = std::max(value, db[i]);
    }
    fractions[b] = std::clamp(1.0f + value / kRangeDb, 0.0f, 1.0f);
  }
  peak_holds_.resize(num_bins);

  auto & styles = getPlane().getStyles();
  setBgColor(styles.window_bg_color);
  fill();
  setFgColor(styles.meter_active_color);
  for (int c = 0; c < cols; c++) {
    auto column = [&](size_t bin) {
      float peak = peak_holds_[bin].update(fractions[bin], dt);
      return level_meter::BarColumn{level_meter::barSteps(fractions[bin], rows), level_meter::barSteps(peak, rows)};
    };
    auto left = column(static_cast<size_t>(2 * c));
    auto right = column(static_cast<size_t>(2 * c + 1));
    auto bar = level_meter::verticalBar(level_meter::Glyphs::BRAILLE, rows, left, right);
    for (int i = 0; i < rows; i++) putstr(i, c, bar[static_cast<size_t>(i)]);
  }
}
