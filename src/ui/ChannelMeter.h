#pragma once

#include <cellui/Charts.h>
#include <cellui/Widget.h>

#include "LevelMeter.h"

#include <array>
#include <chrono>
#include <string>
#include <vector>

// The scope row's raw per-channel volume meter: one sparkline, a bar for each channel
// with its own smoothing and peak marker, and a legend along the bottom row.
// The channels' levels are audio; how a sparkline is drawn is the display's.
class ChannelMeter : public cellui::Widget {
 public:
  // The channels there is room for: order-3 ambisonic plus AuxA/AuxB.
  static constexpr size_t kMaxChannels = 18;

  ChannelMeter(cellui::Plane & parent, const cellui::Charts & charts) : Widget(parent), charts_(charts) {}

  // A fresh reading, one linear RMS per channel (missing ones are silent),
  // and the legend for it. Redraws.
  void setLevels(const std::vector<float> & rms, const std::string & label);

  // True when every bar and peak marker has fallen to nothing, so a further
  // silent reading would change nothing; until then setLevels() has to keep
  // being called for the bars to keep falling.
  bool atRest() const;

  // Forgets every level and peak. Redraws, with the legend `label`.
  void clear(const std::string & label) {
    channels_ = {};
    setLevels({}, label);
  }

 private:
  struct Channel {
    level_meter::Ballistics ballistics;
    level_meter::PeakHold peak_hold;
    cellui::SparklineReading reading;
  };
  const cellui::Charts & charts_;
  std::array<Channel, kMaxChannels> channels_;
  std::chrono::steady_clock::time_point last_update_;
};
