#ifndef _DSP_PEAKLIMITER_H_
#define _DSP_PEAKLIMITER_H_

#include <algorithm>
#include <cmath>

namespace dsp {

// A stereo-linked peak limiter with no lookahead, so it adds no latency.
// When a sample would pass the ceiling the gain drops at once to exactly what
// keeps it there, so the output never passes it; the gain then holds for
// kHoldSeconds (longer than half a cycle of a low note, so it does not duck
// and recover on every peak of one wave) and recovers exponentially over
// kReleaseSeconds. Below the ceiling, with the gain recovered, it is exact
// unity. One gain for both channels keeps the stereo image where it was.
class PeakLimiter {
 public:
  static constexpr double kCeilingDb = -1.0;
  static constexpr double kHoldSeconds = 0.025;
  static constexpr double kReleaseSeconds = 0.15;

  explicit PeakLimiter(int sample_rate = 48000) { setSampleRate(sample_rate); }

  void setSampleRate(int sample_rate) {
    const double rate = sample_rate > 0 ? sample_rate : 48000;
    hold_samples_ = static_cast<int>(std::lround(kHoldSeconds * rate));
    release_ = static_cast<float>(std::exp(-1.0 / (kReleaseSeconds * rate)));
  }

  static float ceiling() { return static_cast<float>(std::pow(10.0, kCeilingDb / 20.0)); }

  // Limits one stereo frame in place.
  void process(float & left, float & right) {
    // The gain this frame needs. Any frame that needs about as much
    // reduction as is applied already restarts the hold, so a sustained loud
    // note keeps one gain rather than recovering between its peaks.
    const float peak = std::max(std::fabs(left), std::fabs(right));
    const float needed = peak > ceiling_ ? ceiling_ / peak : 1.0f;
    if (needed < gain()) reduction_ = 1.0f - needed;
    if (needed < 1.0f && needed <= gain() * kHoldTolerance) hold_left_ = hold_samples_;
    if (reduction_ > 0.0f) {
      left *= gain();
      right *= gain();
    }
    recover();
  }

  // `frames` of silence: the gain recovers as it would have.
  void skip(int frames) {
    for (int i = 0; i < frames && reduction_ > 0.0f; i++) recover();
  }

  float gain() const { return 1.0f - reduction_; }
  bool atRest() const { return reduction_ <= 0.0f; }

 private:
  void recover() {
    if (hold_left_ > 0) {
      hold_left_--;
      return;
    }
    // Kept as the reduction, which a float holds finely all the way down,
    // where 1 - reduction would round to a gain that stops short of unity.
    if (reduction_ > 0.0f) {
      reduction_ *= release_;
      if (reduction_ < 1e-6f) reduction_ = 0.0f;
    }
  }

  static constexpr float kHoldTolerance = 1.001f; // within 0.01 dB of the gain applied
  const float ceiling_ = ceiling();
  float reduction_ = 0.0f; // 1 - the gain
  int hold_samples_ = 0;
  int hold_left_ = 0;
  float release_ = 0.0f;
};

} // namespace dsp

#endif
