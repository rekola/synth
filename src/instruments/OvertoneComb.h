#ifndef _OVERTONECOMB_H_
#define _OVERTONECOMB_H_

#include "OvertoneProfile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

// An overtone profile (OvertoneProfile.h) applied to a voice's finished
// signal, for an instrument that has no level per partial. Each comb term
// averages `step` copies of the signal spaced 1/(step*f) apart, f being the
// voice's fundamental: at every step-th harmonic of f the copies are in phase
// and the average is the harmonic itself; at every other harmonic they are
// spread evenly round the circle and cancel exactly. So adding the average,
// times the term's coefficient, raises exactly the shared overtones and
// leaves every other one where it was. The peaks sit at step*f and its
// multiples, which follow f when the pitch moves; there are no peaks between
// them. Fractional delays are read with cubic Lagrange interpolation.
class OvertoneComb {
 public:
  // In place over `frames` samples, the fundamental moving linearly from `f0`
  // to `f1` across them (where the voice's own increments move).
  void process(float * signal, int frames, double sample_rate, double f0, double f1, const OvertoneProfile & profile) {
    if (frames <= 0 || !(f0 > 0.0) || !(f1 > 0.0)) return;
    reserve(static_cast<size_t>(std::ceil(sample_rate / std::min(f0, f1))) + 8);

    // A term whose lowest peak lies above Nyquist raises nothing.
    const double highest = std::max(f0, f1);
    const long max_step = static_cast<long>(0.5 * sample_rate / highest);
    std::array<OvertoneProfile::Term, OvertoneProfile::kMaxTerms> terms;
    const int count = profile.raised() ? profile.combTerms(terms, max_step) : 0;

    OvertoneProfile::Ramp ramp;
    ramp.begin(profile, frames);
    const double slope = (f1 - f0) / frames;
    for (int n = 0; n < frames; n++) {
      const float x = signal[n];
      history_[write_ & mask_] = x;
      double y = x;
      if (count > 0) {
        const double f = f0 + slope * n;
        for (int t = 0; t < count; t++) {
          const int step = terms[static_cast<size_t>(t)].step;
          const double spacing = sample_rate / (step * f);
          double sum = x;
          for (int i = 1; i < step; i++) sum += read(spacing * i);
          y += ramp.coefficient(terms[static_cast<size_t>(t)].mask) * sum / step;
        }
      }
      signal[n] = static_cast<float>(y);
      ramp.next();
      write_++;
    }
  }

 private:
  // The ring holds at least `samples` of the past, keeping what it has.
  void reserve(size_t samples) {
    if (history_.size() >= samples) return;
    size_t size = 64;
    while (size < samples) size *= 2;
    std::vector<float> grown(size, 0.0f);
    for (size_t k = 0; k < history_.size(); k++) grown[(write_ - k) & (size - 1)] = history_[(write_ - k) & mask_];
    history_ = std::move(grown);
    mask_ = size - 1;
  }

  // The signal `delay` samples before the one just written (delay >= 2).
  double read(double delay) const {
    const auto whole = static_cast<size_t>(delay);
    const double mu = delay - static_cast<double>(whole);
    auto at = [&](size_t back) { return static_cast<double>(history_[(write_ - back) & mask_]); };
    const double before = at(whole - 1), here = at(whole), next = at(whole + 1), after = at(whole + 2);
    return -mu * (mu - 1.0) * (mu - 2.0) / 6.0 * before + (mu + 1.0) * (mu - 1.0) * (mu - 2.0) / 2.0 * here - (mu + 1.0) * mu * (mu - 2.0) / 2.0 * next + (mu + 1.0) * mu * (mu - 1.0) / 6.0 * after;
  }

  std::vector<float> history_;
  size_t mask_ = 0;
  size_t write_ = 0;
};

#endif
