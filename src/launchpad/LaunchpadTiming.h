#ifndef _LAUNCHPADTIMING_H_
#define _LAUNCHPADTIMING_H_

#include <chrono>
#include <vector>

// A free-running step counter driven by wall-clock time deltas rather than
// sample counts - the core of LaunchpadManager's drum-machine audition loop
// (plans/drum-machine.md, Phase 7: every DrumMachineTrack in the song loops
// in phase while the transport is stopped). Factored out here, with no
// LaunchpadIO/DeviceState/Controller dependency, so this step-advance math
// is unit-testable via plain ctest, independent of any Launchpad hardware -
// same reasoning as LaunchpadLayout.h keeping the pure pad-layout math
// separate from LaunchpadManager's own hardware-adjacent state.
class StepClock {
public:
  // (Re)starts the clock at step 0, phase 0. Deliberately doesn't fire
  // step 0 itself - "step 0 fires immediately on start" vs. "waits a full
  // row first" is the caller's policy choice, not something this pure
  // clock should assume.
  void start() { running_ = true; step_ = 0; phase_ = 0.0f; }
  void stop() { running_ = false; }
  bool isRunning() const { return running_; }
  int currentStep() const { return step_; }

  // Advances by dt seconds; row_duration is the current seconds-per-step
  // (the caller recomputes it each call, since tempo can change live).
  // Returns every step number that newly fired this call, in order (empty
  // if less than a full row's worth of time has passed, or the clock
  // isn't running, or row_duration isn't positive) - a loop internally,
  // not a single if, so a caller that goes a while between advance()
  // calls (e.g. a slow UI tick) still gets every step in between rather
  // than silently skipping any.
  std::vector<int> advance(float dt, float row_duration) {
    std::vector<int> fired;
    if (!running_ || row_duration <= 0.0f) return fired;
    phase_ += dt;
    while (phase_ >= row_duration) {
      phase_ -= row_duration;
      step_++;
      fired.push_back(step_);
    }
    return fired;
  }

private:
  bool running_ = false;
  int step_ = 0;
  float phase_ = 0.0f;
};

// A generic "press once to arm, press again within a window to confirm"
// debounce - the core of LaunchpadManager's Stop-Clip Clear gesture
// (plans/drum-machine.md, Phase 7: a double-press within a short window
// clears a drum machine's step data). Not specific to Clear - just a plain
// confirm-debounce primitive, factored out for the same testability reason
// as StepClock above.
class ConfirmTimer {
public:
  // Call on every press. Returns true iff this press is a confirm (a
  // second press landed within `window` of a still-armed first press) -
  // false means this press itself just (re)armed instead, whether that's
  // a genuine first press or a stale one past the window (a stale press
  // doesn't confirm - it just starts a fresh arm). A confirming press
  // disarms afterward, so a third press starts a fresh arm rather than
  // immediately confirming again.
  bool press(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::duration window) {
    if (armed_ && (now - armed_time_) <= window) {
      armed_ = false;
      return true;
    }
    armed_ = true;
    armed_time_ = now;
    return false;
  }

  bool isArmed() const { return armed_; }
  std::chrono::steady_clock::time_point armedTime() const { return armed_time_; }

  // Clears a stale arm once `window` has passed with no confirming
  // press - for a caller that wants an external indicator (e.g. an LED)
  // to honestly show "no longer armed" even if no second press ever
  // comes. press() already treats a stale arm as a fresh first press on
  // its own, so this doesn't change press()'s own behavior - it's purely
  // for observers of isArmed()/armedTime() between presses.
  void expireIfStale(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::duration window) {
    if (armed_ && (now - armed_time_) > window) armed_ = false;
  }

private:
  bool armed_ = false;
  std::chrono::steady_clock::time_point armed_time_;
};

#endif
