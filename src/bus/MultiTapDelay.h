#ifndef _MULTITAPDELAY_H_
#define _MULTITAPDELAY_H_

#include "BusEffect.h"
#include "DelayPattern.h"
#include "../ambisonic/SphericalPosition.h"

#include <array>
#include <string>
#include <vector>

// Named parameter sets showcasing this effect (see MultiTapDelay.cpp's
// presetValues() for the exact numbers and loadParameters() for how a
// preset interacts with individually-specified attributes) - same shape
// as bus/GranularCloud.h's GranularPreset and bus/FDNReverb.h's
// FDNReverbPreset. DEFAULT is
// itself a named, described preset (see presetValues()) - the one a bare
// `<delay/>`, or an explicit preset="default", resolves to - not just an
// unnamed fallback; to_string() still maps it to "" so a default-preset
// instance round-trips quietly, with no explicit preset="..." attribute
// written, matching every other implicit-default attribute this class
// writes. Several of these are named after (and tuned to showcase) one of
// the DelayPattern modes above, with companion feedback/damping/
// patternSpeed values chosen to suit that mode - not just that mode left
// at this class's own flat defaults.
enum class MultiTapDelayPreset { DEFAULT = 0, SLAPBACK, PINGPONG, ORBIT, RECEDE, DUB };

static inline const std::string to_string(MultiTapDelayPreset preset) {
  switch (preset) {
  case MultiTapDelayPreset::DEFAULT: return "";
  case MultiTapDelayPreset::SLAPBACK: return "slapback";
  case MultiTapDelayPreset::PINGPONG: return "pingpong";
  case MultiTapDelayPreset::ORBIT: return "orbit";
  case MultiTapDelayPreset::RECEDE: return "recede";
  case MultiTapDelayPreset::DUB: return "dub";
  }
  return "";
}

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
  // class comment above and advancePass() in the .cpp. Row duration
  // itself is *not* one of this call's parameters - unlike baseRows/
  // feedbackGain/damping/pattern/patternSpeed, it isn't a stored,
  // per-slot deviating value (loadParameters()/storeParameters() below
  // never touch it); it comes from the song's own tempo and is set
  // independently via setRowDuration() below, so a tempo change and a
  // parameter change can each be applied without needing the other's
  // current value on hand. Never reallocates - buffers are sized once, at
  // construction, to kMaxDelaySeconds - so this is safe to call at any
  // time, including mid-playback.
  void setParameters(float baseRows, float feedbackGain, float damping, DelayPattern pattern, float patternSpeed);

  // The song's current tempo resolved to seconds-per-row
  // (ChannelConfiguration::getRowDuration()) - see setParameters()'s
  // comment above for why this is separate. Recomputes tap lengths from
  // whatever baseRows setParameters() last set; never reallocates, same
  // as setParameters().
  void setRowDuration(float rowDurationSeconds) override;

  // Processes `frames` samples of mono input (the send-B sum) into the 4
  // tap outputs, retrievable via getTap()/getTapDirection() until the next
  // process() call. Always runs, even for silent input, so the feedback/
  // damping/pattern state stays continuous across blocks.
  void process(const float * input, int frames) override;

  int getNumTaps() const override { return kNumTaps; }
  const float * getTap(int i) const override { return taps_[static_cast<size_t>(i)].data(); }
  SphericalPosition getTapDirection(int i) const override {
    return SphericalPosition{ azimuth_[static_cast<size_t>(i)], elevation_[static_cast<size_t>(i)], 1.0f };
  }

  // The feedback tap's own extra gain multiplier (Recede mode only - stays
  // 1.0 in every other mode), on top of its fixed kTapGain - exposed
  // mainly so tests can verify the per-pass compounding directly.
  float getFeedbackGainMultiplier() const { return feedbackGainMul_; }

  // Read back the (clamped) values setParameters() last stored - used
  // only for deviation-only project-file saving (storeParameters() below),
  // not by any DSP code here (which works from the already-derived
  // dampingCoef_ instead of raw damping).
  float getBaseRows() const { return baseRows_; }
  float getFeedbackGain() const { return feedbackGain_; }
  float getDamping() const { return rawDamping_; }
  DelayPattern getPattern() const { return pattern_; }
  float getPatternSpeed() const { return patternSpeed_; }
  MultiTapDelayPreset getPreset() const { return preset_; }

  // <delay> element's own attributes: "preset" plus baseRows/feedback/
  // damping/pattern/patternSpeed, each deviation-only against whatever the
  // resolved preset (or, absent one, this constructor's own tuned defaults
  // - see MultiTapDelay.cpp's presetValues()) implies - wet/chainSend are
  // handled generically by BusEffect::loadParameters()/storeParameters(),
  // called first.
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  void advancePass();

  // Combines baseRows_ (set by setParameters()) and rowDurationSeconds_
  // (set by setRowDuration()) into length_[] - called from both, so
  // either can be updated independently without needing the other's
  // current value passed back in.
  void recomputeTapLengths();

  std::vector<float> buffer_;
  int writePos_ = 0;

  // Cached inputs to recomputeTapLengths() - see setParameters()'s and
  // setRowDuration()'s own comments for why these are tracked
  // independently rather than combined into one call.
  float baseRows_ = 0.1875f;
  float rowDurationSeconds_ = 60.0f / 4.0f / 90.0f; // matches Song's own default 90bpm tempo

  // This tap's fixed relative gain (baked into taps_'s output directly) and
  // resolved read length in samples, per tap - see recomputeTapLengths().
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
  float rawDamping_ = 0.0f; // cached purely for getDamping() - DSP uses dampingCoef_

  DelayPattern pattern_ = DelayPattern::Static;
  float patternSpeed_ = 0.0f;

  MultiTapDelayPreset preset_ { MultiTapDelayPreset::DEFAULT };

  // Detects "one full lap of the feedback tap's delay" without a timer -
  // advances by `frames` each process() call, firing advancePass() (and
  // wrapping back down) every time it reaches the feedback tap's own
  // current length.
  int passCounter_ = 0;

  std::array<std::vector<float>, kNumTaps> taps_;
};

#endif
