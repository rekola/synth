#pragma once

#include <algorithm>
#include <chrono>

// How far one wheel notch moves a view: a base step, grown while notches
// arrive in quick succession, so a slow notch is precise and a fast spin
// covers distance. Per axis and per widget, since each keeps its own pace.
namespace cellui {

class WheelAcceleration {
 public:
  using Clock = std::chrono::steady_clock;

  // The step for a notch now; `base` is the cells of a single, isolated notch.
  int step(int base, Clock::time_point now = Clock::now()) {
    streak_ = (started_ && now - last_ < kBurstGap) ? streak_ + 1 : 0;
    started_ = true;
    last_ = now;
    return base * std::min(1 + streak_ / kNotchesPerLevel, kMaxMultiplier);
  }

  static constexpr std::chrono::milliseconds kBurstGap{80};
  static constexpr int kNotchesPerLevel = 2;
  static constexpr int kMaxMultiplier = 4;

 private:
  Clock::time_point last_{};
  int streak_ = 0;
  bool started_ = false;
};

}  // namespace cellui
