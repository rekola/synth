#pragma once

#include <algorithm>
#include <cmath>

// Level metering shared by the audio meters: one dB mapping so every meter reads
// the same level the same way, and the smoothing of a shown level and its peak.
namespace level_meter {

// A linear RMS level's position on a meter: 0 at kFloorDb or below
// (silence, and TrackInfo's -1 "no reading yet" too), 1 at full scale.
// dB-mapped so quiet material still shows.
constexpr float kFloorDb = -60.0f;
inline float fraction(float linear) {
  if (linear <= 0.0f) return 0.0f;
  return std::clamp((20.0f * std::log10(linear) - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
}

// A fraction in whole steps of what a meter can draw: a cell of glyphs shows eighths, a picture its pixels. A
// change smaller than that shows nothing, so it asks for no redraw.
constexpr int kGlyphStepsPerRow = 8;
inline int steps(float fraction, int levels) { return static_cast<int>(std::clamp(fraction, 0.0f, 1.0f) * static_cast<float>(levels) + 0.5f); }

// The steps a meter shows, with a dead band: a level that ripples by less than a step around a step
// boundary (a sustained note) keeps the step it last had, and changes only once it has moved a whole
// step away. The error is under a step, which is a pixel or an eighth of a cell.
class StepHold {
 public:
  int update(float fraction, int levels) {
    float target = std::clamp(fraction, 0.0f, 1.0f) * static_cast<float>(levels);
    bool at_end = target <= 0.0f || target >= static_cast<float>(levels);
    if (at_end || std::fabs(target - static_cast<float>(held_)) >= 1.0f) held_ = static_cast<int>(target + 0.5f);
    return held_;
  }

 private:
  int held_ = 0;
};

// A displayed level's rise and fall, so a low note doesn't make the bar
// jump between the peaks and zero crossings of successive short blocks.
// Fed the linear RMS of each block; the smoothing is done on power (never
// on RMS or dB values, which still ripple), and the shown level then falls
// at a fixed dB rate.
class Ballistics {
 public:
  static constexpr float kIntegrationSeconds = 0.08f;
  static constexpr float kReleaseDbPerSecond = 40.0f;
  static constexpr float kSilenceDb = -100.0f;

  // Returns the level to display as a linear RMS.
  float update(float rms, float dt_seconds) {
    rms = std::max(rms, 0.0f); // "no reading yet" is silence
    float weight = dt_seconds > 0.0f ? 1.0f - std::exp(-dt_seconds / kIntegrationSeconds) : 0.0f;
    power_ += (rms * rms - power_) * weight;
    float target_db = power_ > 1e-10f ? 10.0f * std::log10(power_) : kSilenceDb;
    shown_db_ = target_db >= shown_db_ ? target_db : std::max(target_db, shown_db_ - kReleaseDbPerSecond * dt_seconds);
    return shown_db_ <= kSilenceDb ? 0.0f : std::pow(10.0f, shown_db_ / 20.0f);
  }

 private:
  float power_ = 0.0f;
  float shown_db_ = kSilenceDb;
};

// A peak marker: follows a rising level at once, holds still for a moment,
// then falls back much more slowly than the level itself. Works on a
// meter's 0..1 fraction, so it doesn't depend on the meter's size.
class PeakHold {
 public:
  static constexpr float kHoldSeconds = 0.5f;
  static constexpr float kFallFractionPerSecond = 0.6f;

  float update(float fraction, float dt_seconds) {
    if (fraction >= held_) {
      held_ = fraction;
      hold_left_ = kHoldSeconds;
    } else if (hold_left_ > 0.0f) {
      hold_left_ -= dt_seconds;
    } else {
      held_ = std::max(fraction, held_ - kFallFractionPerSecond * dt_seconds);
    }
    return held_;
  }

 private:
  float held_ = 0.0f;
  float hold_left_ = 0.0f;
};

} // namespace level_meter
