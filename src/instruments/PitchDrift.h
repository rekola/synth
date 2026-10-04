#ifndef _PITCHDRIFT_H_
#define _PITCHDRIFT_H_

#include "../dsp/HashField.h"

#include <cmath>
#include <cstdint>

namespace {
constexpr uint64_t kPitchDriftSalt = 0x5D3B7A91C4E2F608ull;
}

// A slow, aperiodic pitch wander for one oscillator member: smooth value
// noise over control points `period` samples apart, each a hashed value in
// +-`depth` cents (the first is 0, so a note starts on pitch), blended with a
// smoothstep. A pure function of the sample age `t` - never of how the
// timeline is cut into blocks - so the trajectory is the same for any block
// size. The deviation is a frequency ratio fraction (cents scaled by ln2/1200,
// linear, which for a few cents differs from 2^(c/1200) by far less than the
// wander itself); integral() is its running sum in samples, which is what a
// phase accumulator needs. A cache of the current segment makes the forward
// sweep O(1); it is only an optimisation, any t can be evaluated.
class PitchDrift {
 public:
  PitchDrift() = default;

  PitchDrift(int64_t coord, float depth_cents, double period_samples)
    : coord_(coord), depth_(depth_cents * kCentsToRatio), period_(period_samples), inverse_period_(1.0 / period_samples) {
    reset();
  }

  bool active() const { return depth_ > 0.0f && period_ > 0.0; }

  // The frequency ratio is 1 + deviation(t).
  double deviation(uint64_t t) {
    seek(t);
    return value(frac(t));
  }

  // Integral of deviation() over [0, t], in samples.
  double integral(uint64_t t) {
    seek(t);
    return integralAt(frac(t));
  }

  // Both at once, for the per-group hot path.
  void sample(uint64_t t, double & deviation_out, double & integral_out) {
    seek(t);
    const double f = frac(t);
    deviation_out = value(f);
    integral_out = integralAt(f);
  }

 private:
  static constexpr double kCentsToRatio = 0.69314718055994531 / 1200.0;

  // Control point i; zero at the start so the note begins on pitch.
  double control(uint64_t i) const {
    if (i == 0) return 0.0;
    return static_cast<double>(HashField(kPitchDriftSalt).bipolar(coord_, paramId("pitch_drift") + static_cast<uint32_t>(i), depth_));
  }

  void reset() {
    index_ = 0;
    before_ = 0.0;
    a_ = 0.0;
    b_ = control(1);
  }

  double frac(uint64_t t) const { return static_cast<double>(t) * inverse_period_ - static_cast<double>(index_); }

  double integralAt(double f) const {
    return before_ + period_ * (a_ * f + (b_ - a_) * (f * f * f - 0.5 * f * f * f * f));
  }

  void seek(uint64_t t) {
    const uint64_t target = static_cast<uint64_t>(static_cast<double>(t) * inverse_period_);
    if (target < index_) reset();
    while (index_ < target) {
      before_ += period_ * (a_ + b_) * 0.5;
      a_ = b_;
      b_ = control(++index_ + 1);
    }
  }

  double value(double f) const { return a_ + (b_ - a_) * (f * f * (3.0 - 2.0 * f)); }

  int64_t coord_ = 0;
  float depth_ = 0.0f;
  double period_ = 1.0, inverse_period_ = 1.0;

  uint64_t index_ = 0;
  double before_ = 0.0; // integral up to the start of the cached segment
  double a_ = 0.0, b_ = 0.0;
};

#endif
