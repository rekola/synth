#include "ChannelMeter.h"

#include "../UIPlane.h"
#include "../StyleProvider.h"

#include <algorithm>

void
ChannelMeter::setLevels(const std::vector<float> & rms, const std::string & label) {
  auto now = std::chrono::steady_clock::now();
  auto dt = std::min(std::chrono::duration<float>(now - last_update_).count(), 0.25f);
  last_update_ = now;
  for (size_t i = 0; i < kMaxChannels; i++) {
    auto & channel = channels_[i];
    float level = i < rms.size() ? rms[i] : 0.0f;
    channel.clipping = level >= 1.0f;
    channel.fraction = level_meter::fraction(channel.ballistics.update(level, dt));
    channel.peak_fraction = channel.peak_hold.update(channel.fraction, dt);
  }

  auto [ rows, cols ] = getDim();
  if (rows <= 0 || cols <= 0) return;
  auto & styles = getPlane().getStyles();
  setBgColor(styles.window_bg_color);
  fill();

  int bar_rows = label.empty() ? rows : rows - 1;
  if (bar_rows > 0) {
    int cells = std::min(cols, static_cast<int>(kMaxChannels / 2));
    for (int c = 0; c < cells; c++) {
      auto & left = channels_[static_cast<size_t>(2 * c)];
      auto & right = channels_[static_cast<size_t>(2 * c + 1)];
      auto column = [&](const Channel & channel) {
        return level_meter::BarColumn{level_meter::barSteps(channel.fraction, bar_rows), level_meter::barSteps(channel.peak_fraction, bar_rows)};
      };
      auto bar = level_meter::verticalBar(level_meter::Glyphs::BRAILLE, bar_rows, column(left), column(right));
      bool clipping = left.clipping || right.clipping;
      for (int i = 0; i < bar_rows; i++) {
        // Shaded by the height of this cell; i = 0 is the top one.
        setFgColor(clipping ? styles.meter_clip_color : styles.meterColor((static_cast<float>(bar_rows - i) - 0.5f) / static_cast<float>(bar_rows)));
        putstr(i, c, bar[static_cast<size_t>(i)]);
      }
    }
  }
  if (!label.empty()) {
    setFgColor(styles.window_fg_color);
    putstr(rows - 1, 0, label);
  }
}
