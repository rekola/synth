#ifndef _CHANNELMETER_H_
#define _CHANNELMETER_H_

#include "../UIElement.h"
#include "LevelMeter.h"

#include <algorithm>
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

  // True when every bar and peak marker has fallen to nothing, so a further
  // silent reading would change nothing; until then setLevels() has to keep
  // being called for the bars to keep falling.
  bool atRest() const {
    return std::all_of(channels_.begin(), channels_.end(), [](const Channel & c) {
      return c.fraction == 0.0f && c.peak_fraction == 0.0f && !c.clipping;
    });
  }

  // Forgets every level and peak. Redraws, with the legend `label`.
  void clear(const std::string & label) {
    channels_ = {};
    setLevels({}, label);
  }

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
