#ifndef _CHANNELMETER_H_
#define _CHANNELMETER_H_

#include "../UIElement.h"
#include "LevelMeter.h"

#include <array>
#include <chrono>
#include <string>
#include <vector>

// The raw per-channel volume meter of the scope row: one vertical bar per
// channel, two channels sharing each cell (one per dot column), each with
// its own smoothing and peak marker. A legend lines up with the cells
// along the bottom row.
class ChannelMeter : public UIElement {
public:
  // The channels the meter has room for: order-3 ambisonic plus AuxA/AuxB.
  static constexpr size_t kMaxChannels = 18;

  explicit ChannelMeter(UIPlane & parent) : UIElement(parent) { }

  // A fresh reading, one linear RMS per channel (missing ones are
  // silent), and the legend for it. Redraws.
  void setLevels(const std::vector<float> & rms, const std::string & label);

private:
  struct Channel {
    level_meter::Ballistics ballistics;
    level_meter::PeakHold peak_hold;
    float fraction = 0.0f;
    float peak_fraction = 0.0f;
    bool clipping = false;
  };
  std::array<Channel, kMaxChannels> channels_;
  std::chrono::steady_clock::time_point last_update_;
};

#endif
