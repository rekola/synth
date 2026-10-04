#ifndef _PITCHDRIFT_H_
#define _PITCHDRIFT_H_

#include "../dsp/HashField.h"

#include <cmath>
#include <cstdint>

namespace {
constexpr uint64_t kPitchDriftSalt = 0x5D3B7A91C4E2F608ull;
}

// A slow, aperiodic pitch wander for the members of an oscillator array:
// smooth value noise over control points `period` samples apart, each a hashed
// value in +-`depth` cents (the first is 0, so a note starts on pitch),
// blended with a smoothstep. A pure function of the sample age `t` - never of
// how the timeline is cut into blocks - so the trajectory is the same for any
// block size. The deviation is a frequency ratio fraction (cents scaled by
// ln2/1200, linear, which for a few cents differs from 2^(c/1200) by far less
// than the wander itself); the integral is its running sum in samples, which
// is what a phase accumulator needs.
//
// The work splits in two. Where `t` falls in the noise (segment, position,
// smoothstep and integral weights) is the same for every member, so a shared
// PitchDriftClock works it out once per time; each PitchDriftMember then only
// holds its own control points and blends them with those weights.

// Where a time falls in the noise, the same for every member.
struct PitchDriftPoint {
  uint64_t segment = 0;
  double fraction = 0.0; // position within the segment, 0..1
  double smooth = 0.0;   // smoothstep of the fraction
  double area = 0.0;     // integral of that smoothstep from 0 to the fraction
  double period = 1.0;   // segment length in samples
};

class PitchDriftClock {
 public:
  PitchDriftClock() = default;
  explicit PitchDriftClock(double period_samples) : period_(period_samples), inverse_period_(1.0 / period_samples) { }

  bool active() const { return period_ > 0.0; }

  PitchDriftPoint at(uint64_t t) const {
    const double x = static_cast<double>(t) * inverse_period_;
    PitchDriftPoint p;
    p.segment = static_cast<uint64_t>(x);
    p.fraction = x - static_cast<double>(p.segment);
    const double f = p.fraction;
    p.smooth = f * f * (3.0 - 2.0 * f);
    p.area = f * f * f - 0.5 * f * f * f * f;
    p.period = period_;
    return p;
  }

 private:
  double period_ = 0.0, inverse_period_ = 0.0;
};

// One member's wander: its own control points, blended with a shared point.
// The current segment is cached so a forward sweep is O(1); any point can be
// evaluated, a backward one just replays from the start.
class PitchDriftMember {
 public:
  PitchDriftMember() = default;

  PitchDriftMember(int64_t coord, float depth_cents)
    : coord_(coord), depth_(depth_cents * kCentsToRatio) {
    reset();
  }

  bool active() const { return depth_ > 0.0f; }

  // The frequency ratio is 1 + deviation, and integral is the deviation's sum
  // over [0, t] in samples.
  void sample(const PitchDriftPoint & p, double & deviation, double & integral) {
    seek(p);
    deviation = a_ + (b_ - a_) * p.smooth;
    integral = before_ + p.period * (a_ * p.fraction + (b_ - a_) * p.area);
  }

  double deviation(const PitchDriftPoint & p) {
    double d, i;
    sample(p, d, i);
    return d;
  }

  double integral(const PitchDriftPoint & p) {
    double d, i;
    sample(p, d, i);
    return i;
  }

 private:
  static constexpr double kCentsToRatio = 0.69314718055994531 / 1200.0;

  // Control point i; zero at the start so the note begins on pitch.
  double control(uint64_t i) const {
    if (i == 0) return 0.0;
    return static_cast<double>(HashField(kPitchDriftSalt).bipolar(coord_, paramId("pitch_drift") + static_cast<uint32_t>(i), depth_));
  }

  void reset() {
    segment_ = 0;
    before_ = 0.0;
    a_ = 0.0;
    b_ = control(1);
  }

  void seek(const PitchDriftPoint & p) {
    if (p.segment == segment_) return;
    if (p.segment < segment_) reset();
    while (segment_ < p.segment) {
      before_ += p.period * (a_ + b_) * 0.5;
      a_ = b_;
      b_ = control(++segment_ + 1);
    }
  }

  int64_t coord_ = 0;
  float depth_ = 0.0f;

  uint64_t segment_ = 0;
  double before_ = 0.0; // integral up to the start of the cached segment
  double a_ = 0.0, b_ = 0.0;
};

#endif
