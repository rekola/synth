#ifndef _TAPETRANSPORT_H_
#define _TAPETRANSPORT_H_

#include "NoiseGenerator.h"
#include "PinkNoiseFilter.h"

#include <algorithm>

// Every tunable TapeTransportDsp::nextSample() reads each sample - owned
// by the caller (TapeDegradation.cpp's TapeDegradationDsp, itself sourced
// from XML via loadParameters()/a preset table), never mutated here. Plain
// data, not a class, so a caller can build one on the stack per call
// without any lifetime concerns.
//
// Deliberately does NOT include the attack/release ("spin-up"/"spin-down")
// shaping a voice-attached instance needs at note-on/note-off - that's a
// note-lifecycle concern (see TapeTransportLifecycle below), not a
// property of the stochastic transport itself, and a track-attached
// instance never has note-on/note-off events to shape around at all. The
// two compose in TapeDegradationDsp: this struct's own pitchDeviationCents
// is pure wow+flutter, faded/overridden by the caller's own lifecycle
// state where relevant.
struct TapeTransportParams {
  float wowRateHz = 0.7f;
  float wowDepthCents = 8.0f;
  bool wowLocked = false;          // Vinyl: a fixed, deterministic wow rate instead of the health-driven one below
  float wowLockedRateHz = 0.5556f; // 33 1/3 rpm
  float flutterRateHz = 8.0f;
  float flutterDepthCents = 3.0f;

  // How fast "health" (see TapeTransportSample::health below) wanders,
  // and how strongly it scales every other component's own depth/gain/
  // rate - the brief's single "correlated fault" driver.
  float healthRateHz = 0.2f;
  float healthSensitivity = 0.5f;

  float hissLevelDB = -48.0f;

  float dropoutRateHz = 0.15f;   // Poisson mean events/s at health = 1 (nominal)
  float dropoutDepthDB = -12.0f;
  float dropoutDurationMs = 15.0f;

  float clickRateHz = 0.3f;      // Poisson mean events/s at health = 1 (nominal)
  float clickGainDB = -18.0f;

  // Pressure-pad amplitude flutter (Mellotron) - a slow AM tap riding the
  // same flutter phase as the pitch flutter above (one mechanical wobble,
  // two audible symptoms, rather than a second independent LFO), scaled
  // by this depth. 0 (default) makes it inert - only the Mellotron preset
  // sets it.
  float ampFlutterDepth = 0.0f;

  // Low rumble (Vinyl) - a second, independent noise source from hiss
  // above, one-pole low-passed rather than pink-filtered (rumble is a
  // low-frequency-only phenomenon - motor/bearing noise and subsonic
  // record warp, not a broadband tilt) and mixed in the same additive way.
  // rumbleLevelDB defaults far enough down (-100dB) to be inert for every
  // preset that doesn't explicitly raise it.
  float rumbleLevelDB = -100.0f;
  float rumbleHz = 80.0f; // one-pole lowpass cutoff

  // One-way health decay (Disintegration) - health's usual ceiling (1.0,
  // see TapeTransportDsp's own class comment) drifts monotonically
  // downward as getElapsedSeconds() grows, at decayRatePerMinute health
  // units per minute of program time, layered underneath the normal
  // random wander rather than replacing it. decayMode false (default)
  // disables this entirely, regardless of decayRatePerMinute's value.
  bool decayMode = false;
  float decayRatePerMinute = 0.0f;

  // Dolby-style HF breathing (Cassette) / level-dependent grain noise
  // (Optical film) - both read from one shared envelope follower over the
  // actual input signal, which only TapeDegradationDsp has (this struct's
  // own contract is deliberately signal-agnostic - see the class comment
  // below), so these four fields are stored here purely as part of the
  // one preset-wide parameter bag but are read by TapeDegradationDsp
  // itself, not by TapeTransportDsp::nextSample(). breathingAmount
  // modulates the HF rolloff cutoff; hissLevelDependent scales hiss/grain
  // gain - same envelope, two independent destinations, selected by
  // preset. Both 0 (default) makes the envelope follower's cost the only
  // thing paid for nothing.
  float breathingAmount = 0.0f;
  float hissLevelDependent = 0.0f;
  float breathingAttackMs = 5.0f;
  float breathingReleaseMs = 80.0f;
};

// One sample's worth of everything TapeTransportDsp produces -
// TapeDegradationDsp (effects/TapeDegradation.cpp) folds these into its
// own mono signal chain (pitchDeviationCents into the wow/flutter delay
// read, dropoutGain multiplied uniformly into the block, clickImpulse and
// hiss summed in, health read separately to scale the saturation stage's
// drive) - none of that shaping happens here, this struct is pure
// transport output.
struct TapeTransportSample {
  float pitchDeviationCents = 0.0f; // wow + flutter combined, this sample (no spin-up/down shaping - see TapeTransportParams's own doc comment)
  float dropoutGain = 1.0f;         // 1 = no dip, dropoutDepthDB-scaled during one
  float clickImpulse = 0.0f;        // 0 most samples, a single-sample +-peak on a click
  float hiss = 0.0f;                // pink-noise sample, already level- and health-scaled
  float health = 1.0f;              // 1 = nominal, lower = more trouble - see below
  float ampFlutter = 1.0f;          // pressure-pad AM multiplier - 1 = no effect (ampFlutterDepth = 0)
  float rumble = 0.0f;              // one-pole-lowpassed noise sample, already level-scaled
};

// The shared "transport health" engine every TapeDegradation instance
// owns one of (one per track-attached instance, one per voice-attached
// instance - see TapeDegradation.cpp). Pure scalar state, no
// AudioBuffer/position awareness at all, so it's independently testable
// (statistical Poisson-rate checks, bounds checks) without any audio
// plumbing.
//
// `health` is a slow, low-pass-filtered random walk, ceilinged at 1.0
// (nominal) and dipping toward `1 - healthSensitivity` as it wanders -
// every other component reads `1 - health` ("trouble") as a shared
// multiplier on its own depth/gain/rate, which is what makes a dip in one
// component (e.g. wow depth momentarily widening) correlate with dips in
// every other component (hiss rising, a dropout/click becoming more
// likely, saturation biting harder - see TapeDegradationDsp) in the same
// instant, rather than five independent RNGs coincidentally lining up.
class TapeTransportDsp {
 public:
  explicit TapeTransportDsp(uint32_t seed) : white_(seed) { }

  TapeTransportSample nextSample(const TapeTransportParams & params, float sampleRate);

  // Elapsed seconds since this instance was constructed, accumulated as
  // frames/sampleRate rather than a raw sample count - deliberately, so
  // anything driven off it (e.g. a future one-way health decay for a
  // "disintegrating" preset) means the same thing regardless of which
  // sample rate the song happens to be rendered at.
  float getElapsedSeconds() const { return elapsed_seconds_; }

 private:
  static float drawExponentialSeconds(NoiseGenerator & rng, float rateHz);

  NoiseGenerator white_;
  PinkNoiseFilter pink_;

  float raw_health_ = 0.0f; // [-1, 1], the low-passed noise health_ is derived from
  float elapsed_seconds_ = 0.0f;
  float rumble_state_ = 0.0f; // one-pole lowpass state for the rumble noise source

  float wow_phase_ = 0.0f;        // [0, 1)
  float locked_wow_phase_ = 0.0f; // [0, 1), free-running regardless of wowLocked - always advanced so switching the flag mid-note (not currently possible - preset is load-time-only - but cheap to keep correct) never jumps
  float flutter_phase_ = 0.0f;    // [0, 1)

  float dropout_timer_ = 0.0f;      // seconds until the next dropout may begin
  float dropout_remaining_ = 0.0f;  // seconds left in an active dip, 0 = none active
  bool dropout_timer_seeded_ = false;

  float click_timer_ = 0.0f;        // seconds until the next click
  bool click_timer_seeded_ = false;
};

// Explicit note-lifecycle state machine for a voice-attached tape
// instance - deliberately separate from the instrument's own amplitude
// envelope (never wraps or duplicates an ADSR): the musical envelope
// stays upstream, on the instrument, entirely unaware this exists. This
// only shapes the *tape machine's own* spin-up/spin-down character
// (pitch swoop on note-on, hiss fade + pitch droop on note-off), driven
// by explicit noteOn()/noteOff() calls rather than inferred from
// children's own activity.
//
// Defaults to Running and *only* Running unless something calls
// noteOn()/noteOff() on it - a track-attached instance never does (there
// is no per-note lifecycle for a persistent "machine in the room" to
// have), so it behaves exactly as if this class didn't exist: always
// fully engaged, progress() always 0, isDone()/state() never leave
// Running. Transport state is therefore never a required part of the
// calling contract - see TapeDegradation.cpp's TapeDegradationTrackState,
// which never touches this at all.
class TapeTransportLifecycle {
 public:
  enum class State { Stopped, SpinUp, Running, SpinDown };

  // -> SpinUp. The only state noteOn() is ever expected to be called
  // from is a fresh voice's own construction (TapeDegradationVoiceState) -
  // every note gets its own fresh instance, so this never needs to handle
  // "retriggering an already-running instance" as a real case, but does
  // the sensible thing anyway (restarts spin-up from 0) if it's ever
  // reached from another state.
  void noteOn(float spinUpSeconds) {
    state_ = State::SpinUp;
    spin_up_seconds_ = std::max(0.0f, spinUpSeconds);
    state_elapsed_ = 0.0f;
  }

  // -> SpinDown, from wherever spin-up/running currently is - a note
  // interrupted mid-spin-up heads into spin-down from its current
  // (partial) progress rather than snapping back to full spin-up
  // amplitude first, so a very short note's total swoop+droop doesn't
  // end up *longer* than a sustained note's. No-op once already Stopped
  // or SpinDown (a second noteOff() - e.g. killNote() arriving right
  // after stopNote() - must not restart the fade from full volume).
  void noteOff(float spinDownSeconds) {
    if (state_ == State::Stopped || state_ == State::SpinDown) return;
    state_ = State::SpinDown;
    spin_down_seconds_ = std::max(0.0f, spinDownSeconds);
    state_elapsed_ = 0.0f;
  }

  // Advances by one sample (dt = 1/sampleRate) - called once per
  // TapeTransportDsp::nextSample()-equivalent tick, so this shares
  // exactly the same per-sample cadence as everything else driving
  // TapeDegradationDsp's own loop.
  void advance(float dt) {
    state_elapsed_ += dt;
    if (state_ == State::SpinUp && state_elapsed_ >= spin_up_seconds_) {
      state_ = State::Running;
      state_elapsed_ = 0.0f;
    } else if (state_ == State::SpinDown && state_elapsed_ >= spin_down_seconds_) {
      state_ = State::Stopped;
      state_elapsed_ = 0.0f;
    }
  }

  State state() const { return state_; }

  // [0, 1) through SpinUp/SpinDown, 0 during Stopped/Running (a steady
  // state has no progress of its own to report).
  float progress() const {
    if (state_ == State::SpinUp) return spin_up_seconds_ > 0.0f ? std::min(1.0f, state_elapsed_ / spin_up_seconds_) : 1.0f;
    if (state_ == State::SpinDown) return spin_down_seconds_ > 0.0f ? std::min(1.0f, state_elapsed_ / spin_down_seconds_) : 1.0f;
    return 0.0f;
  }

 private:
  State state_ = State::Running;
  float spin_up_seconds_ = 0.0f;
  float spin_down_seconds_ = 0.0f;
  float state_elapsed_ = 0.0f;
};

#endif
