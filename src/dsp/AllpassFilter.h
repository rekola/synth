#ifndef _ALLPASSFILTER_H_
#define _ALLPASSFILTER_H_

#include <cmath>

// A single first-order allpass stage - the building block classic
// phaser/flanger effects cascade several of, each swept by a shared LFO
// (see effects/Phaser.cpp, this codebase's own use). Canonical difference
// equation (bilinear-transform of a single real pole/zero pair):
//   y[n] = a*x[n] + x[n-1] - a*y[n-1]
// `a` (in (-1, 1)) sets the stage's own -90-degree crossover frequency -
// coefficientFor() below derives it from a real frequency/sample-rate
// pair. A first-order allpass passes every frequency at unity gain but
// shifts phase by an amount that grows with frequency (0 degrees at DC,
// -90 degrees at the crossover frequency, approaching -180 degrees at
// Nyquist); summing several such phase-shifted copies back with the dry
// signal is what creates the moving comb-filter notches a phaser sweeps -
// no gain change ever happens in this class itself, only phase.
template <class T>
class AllpassStage {
public:
  static T coefficientFor(T fc, T sample_rate) {
    T tan_val = std::tan(static_cast<T>(M_PI) * fc / sample_rate);
    return (tan_val - static_cast<T>(1)) / (tan_val + static_cast<T>(1));
  }

  T process(T x, T a) {
    T y = a * x + xz1_ - a * yz1_;
    xz1_ = x;
    yz1_ = y;
    return y;
  }

  // Advances state as if a zero-input sample had been processed - same
  // "keep the filter's history evolving through silence rather than
  // freezing and resuming later as if no time had passed" reasoning as
  // Biquad<T>::apply()'s own no-buffer overload.
  void advanceSilently(T a) { process(static_cast<T>(0), a); }

private:
  T xz1_ = 0, yz1_ = 0;
};

#endif
