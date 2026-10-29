#include <cellui/ChannelMeter.h>

#include <cellui/Plane.h>
#include <cellui/StyleProvider.h>

#include <algorithm>

namespace cellui {

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
  if (bar_rows > 0) drawBars(bar_rows, std::min(cols, static_cast<int>(kMaxChannels / 2)));
  if (!label.empty()) {
    setFgColor(styles.window_fg_color);
    putstr(rows - 1, 0, label);
  }
}

}  // namespace cellui
