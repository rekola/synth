#ifndef _DELAYLINETAIL_H_
#define _DELAYLINETAIL_H_

#include <algorithm>

// Tracks how many more samples a delay-line-based per-voice effect must
// keep being rendered after its input source goes silent, so audio
// already written into the delay line but not yet read back out isn't
// discarded.
//
// The bug this exists to fix: a VoiceState's default isActive() (and
// EffectVoiceState's own `VoiceState::isActive() || isEffectActive()`)
// tracks only whether children are still producing audio. The moment the
// wrapped instrument's own envelope reaches zero, the whole voice gets
// excluded from InstrumentTrackState::renderVoices()'s render loop (its
// own `if (voice->isActive())` check happens *before* render(), not
// after) and reaped by clearFinishedVoices() shortly after - discarding
// whatever a still-populated fractional delay line (wow/flutter, chorus,
// ...) had queued up but not yet emitted, which is a truncation/click
// bug (heard as an abrupt stop), not a gain-envelope bug. Confirmed
// present in both effects/TapeDegradation.cpp's wow/flutter delay line
// and effects/Chorus.cpp's ChorusEngine when either is voice-attached -
// a track-attached instance never hits this, since TrackState::
// renderChildren() (unlike VoiceState::renderChildren()) renders every
// child unconditionally, active or not.
//
// Pure counter, no AudioBuffer/audio-thread-unsafe state - a caller
// owns one instance per delay line (sized to that delay line's own
// maximum possible delay in samples), calls update() once per render()
// with whether its input was real this block, and folds isDraining()
// into its own isActive() override (mirroring EffectFilterVoiceState's
// existing "my own state, not just my children, decides isActive()"
// shape - effects/EnvelopeFilter.cpp).
class DelayLineTail {
 public:
  explicit DelayLineTail(int maxDelaySamples) : max_delay_samples_(maxDelaySamples) { }

  // sourceActive: whether this block's input was real audio (not just a
  // silence placeholder - see AudioBufferUtils.h's ensureMainChannel()).
  // While true, the drain countdown stays pinned at its maximum (there's
  // always at least maxDelaySamples of real-or-recent content in the
  // line). Once it goes false, the countdown starts falling; reaching 0
  // means every sample that could still be sitting in the delay line has
  // now been read out at least once.
  void update(bool sourceActive, int frames) {
    if (sourceActive) remaining_samples_ = max_delay_samples_;
    else remaining_samples_ = std::max(0, remaining_samples_ - frames);
  }

  bool isDraining() const { return remaining_samples_ > 0; }

  int maxDelaySamples() const { return max_delay_samples_; }

 private:
  int max_delay_samples_;
  int remaining_samples_ = 0; // starts at 0, not max_delay_samples_ - a freshly-constructed voice's delay line is genuinely empty (see FractionalDelayLine::resize()'s own zero-fill), nothing to drain until real input actually arrives
};

#endif
