#ifndef _MULTITAPDELAY_H_
#define _MULTITAPDELAY_H_

#include "BusEffect.h"
#include "DelayPattern.h"
#include "../SphericalPosition.h"

#include <array>
#include <vector>

// Multi-tap delay: a single shared delay line fed by the mono SendB sum,
// read back at 4 fixed offsets (1x/2x/4x/8x a base row-fraction interval),
// each carrying its own fixed relative gain and azimuth - a "rich echo"
// character shared by every mode. Only the longest (8x) tap also feeds
// back into the line, damped and scaled by delayFeedback - so every tap
// naturally repeats (quieter, darker) on later passes too, since they all
// read the same evolving buffer, not just the feedback tap itself.
//
// The 4 taps' *directions* are fixed forever except the feedback tap's,
// which is what a DelayPattern mode actually animates: Static leaves it
// untouched, PingPong flips its azimuth sign, Orbit rotates it, Recede
// shrinks its gain and drops its elevation - once per detected feedback
// pass (a pass = one full lap of the feedback tap's own delay length),
// not per-sample (see advancePass() in MultiTapDelay.cpp). SendBusProcessor
// re-derives ambisonic gains from getTapDirection() every block and uses a
// dedicated AmbisonicVoiceEncoder for the feedback tap so a pattern update
// landing mid-block interpolates smoothly instead of clicking - taps 0-2
// never need that since their direction never changes.
class MultiTapDelay : public BusEffect {
 public:
  explicit MultiTapDelay(int sampleRate);

  static constexpr int kNumTaps = 4;

  // Longest tap this class ever allocates for, regardless of what an
  // extreme (very slow) tempo's resolved baseRows*8 would otherwise want -
  // there's no minimum-tempo floor in this engine, so this is the one
  // thing keeping buffer sizing bounded (same shape as FDNReverb's own
  // kMaxPreDelaySeconds clamp).
  static constexpr float kMaxDelaySeconds = 2.0f;

  // baseRows: base tap interval in pattern rows (tap i's time = baseRows *
  // 2^i * rowDurationSeconds); feedbackGain: linear gain applied to the
  // damped feedback-tap readout each pass (clamped well below 1 for
  // stability); damping: 0 (bright) - 1 (dark), same `1 - damping`
  // one-pole convention FDNReverb uses; pattern/patternSpeed: see the
  // class comment above and advancePass() in the .cpp. rowDurationSeconds:
  // the song's current tempo resolved to seconds-per-row (getRowDuration()),
  // computed by the caller (this class has no notion of tempo itself).
  // Never reallocates - buffers are sized once, at construction, to
  // kMaxDelaySeconds - so this is safe to call at any time, including
  // mid-playback.
  void setParameters(float baseRows, float feedbackGain, float damping, DelayPattern pattern, float patternSpeed, float rowDurationSeconds);

  // Processes `frames` samples of mono input (the send-B sum) into the 4
  // tap outputs, retrievable via getTap()/getTapDirection() until the next
  // process() call. Always runs, even for silent input, so the feedback/
  // damping/pattern state stays continuous across blocks.
  void process(const float * input, int frames) override;

  const float * getTap(int i) const { return taps_[static_cast<size_t>(i)].data(); }
  SphericalPosition getTapDirection(int i) const {
    return SphericalPosition{ azimuth_[static_cast<size_t>(i)], elevation_[static_cast<size_t>(i)], 1.0f };
  }

  // The feedback tap's own extra gain multiplier (Recede mode only - stays
  // 1.0 in every other mode), on top of its fixed kTapGain - exposed
  // mainly so tests can verify the per-pass compounding directly.
  float getFeedbackGainMultiplier() const { return feedbackGainMul_; }

 private:
  void advancePass();

  std::vector<float> buffer_;
  int writePos_ = 0;

  // This tap's fixed relative gain (baked into taps_'s output directly) and
  // resolved read length in samples, per tap - see setParameters().
  static constexpr float kTapGain[kNumTaps] = { 1.00f, 0.75f, 0.55f, 0.40f };
  int length_[kNumTaps] = { 1, 1, 1, 1 };

  // Per-tap direction: fixed for taps 0-2, evolving for tap 3 (index
  // kFeedbackTap) under Ping-pong/Orbit/Recede - see advancePass().
  static constexpr int kFeedbackTap = kNumTaps - 1;
  float azimuth_[kNumTaps] = { 30.0f, -30.0f, 60.0f, -60.0f };
  float elevation_[kNumTaps] = { 0.0f, 0.0f, 0.0f, 0.0f };
  float feedbackGainMul_ = 1.0f; // Recede's per-pass shrink, applied on top of kTapGain[kFeedbackTap]

  float feedbackGain_ = 0.0f;
  float dampingCoef_ = 1.0f;
  float dampingState_ = 0.0f;

  DelayPattern pattern_ = DelayPattern::Static;
  float patternSpeed_ = 0.0f;

  // Detects "one full lap of the feedback tap's delay" without a timer -
  // advances by `frames` each process() call, firing advancePass() (and
  // wrapping back down) every time it reaches the feedback tap's own
  // current length.
  int passCounter_ = 0;

  std::array<std::vector<float>, kNumTaps> taps_;
};

#endif
