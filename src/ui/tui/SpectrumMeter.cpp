#include "SpectrumMeter.h"

#include "../UIPlane.h"
#include "../StyleProvider.h"

#include <algorithm>
#include <cmath>

void
SpectrumMeter::setSpectrum(const std::vector<float> & db) {
  auto [ rows, cols ] = getDim();
  if (rows <= 0 || cols <= 0 || db.empty()) return;

  auto now = std::chrono::steady_clock::now();
  auto dt = std::min(std::chrono::duration<float>(now - last_update_).count(), 0.25f);
  last_update_ = now;

  // Log-spaced bins, the loudest linear bin in each, so the bass isn't
  // squeezed into a couple of columns.
  size_t num_bins = static_cast<size_t>(2 * cols);
  float start = std::log2(40.0f), bin_size = (std::log2(40.0f + static_cast<float>(db.size())) - start) / static_cast<float>(num_bins);
  float ceiling_db = 20.0f * std::log10(static_cast<float>(db.size()));
  std::vector<float> fractions(num_bins, 0.0f);
  for (size_t i = 0; i < db.size(); i++) {
    size_t bin = std::min(static_cast<size_t>((std::log2(40.0f + static_cast<float>(i)) - start) / bin_size), num_bins - 1);
    fractions[bin] = std::max(fractions[bin], std::clamp((db[i] - (ceiling_db - kRangeDb)) / kRangeDb, 0.0f, 1.0f));
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
