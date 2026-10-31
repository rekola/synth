#include "ChannelMeter.h"

#include <cellui/Plane.h>
#include <cellui/StyleProvider.h>

#include <algorithm>

void ChannelMeter::setLevels(const std::vector<float> & rms, const std::string & label) {
  auto now = std::chrono::steady_clock::now();
  auto dt = std::min(std::chrono::duration<float>(now - last_update_).count(), 0.25f);
  last_update_ = now;
  for (size_t i = 0; i < kMaxChannels; i++) {
    auto & channel = channels_[i];
    float level = i < rms.size() ? rms[i] : 0.0f;
    channel.reading.saturated = level >= 1.0f;
    channel.reading.fraction = level_meter::fraction(channel.ballistics.update(level, dt));
    channel.reading.peak = channel.peak_hold.update(channel.reading.fraction, dt);
  }

  auto [rows, cols] = getDim();
  if (rows <= 0 || cols <= 0) return;
  auto & styles = getPlane().getStyles();
  setBgColor(styles.window_bg_color);
  fill();

  int bar_rows = label.empty() ? rows : rows - 1;
  cellui::SparklineLook look{styles.window_bg_color, std::nullopt};
  // One sparkline, two bars to a cell, as many as fit.
  std::vector<cellui::SparklineReading> readings;
  for (size_t i = 0; i < std::min(kMaxChannels, static_cast<size_t>(2 * cols)); i++) readings.push_back(channels_[i].reading);
  if (bar_rows > 0) charts_.drawSparkline(getPlane(), 0, 0, bar_rows, readings, look, 0, bar_rows, 1);
  if (!label.empty()) {
    setFgColor(styles.window_fg_color);
    putstr(rows - 1, 0, label);
  }
}

bool ChannelMeter::atRest() const {
  return std::all_of(channels_.begin(), channels_.end(), [](const Channel & c) {
    return c.reading.fraction == 0.0f && c.reading.peak == 0.0f && !c.reading.saturated;
  });
}
