#include "JustIntonation.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numeric>
#include <vector>

namespace just_intonation {

namespace {

// Widest numerator times denominator a candidate may have.
constexpr int kMaxHeight = 8192;

int largestPrimeFactor(int n) {
  int largest = 1;
  for (int p = 2; p * p <= n; p++) {
    while (n % p == 0) {
      largest = p;
      n /= p;
    }
  }
  return n > 1 ? n : largest;
}

struct Choice {
  Interval interval;
  long height = 0;
  double error = 0.0; // absolute, in cents
  bool set = false;
};

std::vector<Interval> buildTable(int edo) {
  const double step_cents = 1200.0 / edo;
  const double cap = std::min(kMaxErrorCents, step_cents / 2.0);

  std::vector<Choice> best(static_cast<size_t>(edo));
  best[0].interval = { 1, 1, true };
  best[0].height = 1;
  best[0].set = true;

  for (int den = 1; den * den < kMaxHeight; den++) {
    for (int num = den + 1; num < 2 * den && static_cast<long>(num) * den <= kMaxHeight; num++) {
      if (std::gcd(num, den) != 1) continue;
      if (largestPrimeFactor(num) > kMaxPrime || largestPrimeFactor(den) > kMaxPrime) continue;
      double cents = 1200.0 * std::log2(static_cast<double>(num) / den);
      int step = static_cast<int>(std::lround(cents / step_cents));
      if (step <= 0 || step >= edo) continue;
      double error = std::fabs(cents - step * step_cents);
      if (error > cap) continue;
      long height = static_cast<long>(num) * den;
      auto & choice = best[static_cast<size_t>(step)];
      if (!choice.set || height < choice.height || (height == choice.height && error < choice.error)) {
        choice = { { num, den, true }, height, error, true };
      }
    }
  }

  std::vector<Interval> table(static_cast<size_t>(edo));
  for (int step = 0; step < edo; step++) {
    auto & choice = best[static_cast<size_t>(step)];
    table[static_cast<size_t>(step)] = choice.set ? choice.interval : Interval{ 1, 1, false };
  }
  return table;
}

const std::vector<Interval> & tableFor(int edo) {
  static std::mutex mutex;
  static std::map<int, std::vector<Interval> > tables;
  std::lock_guard<std::mutex> lock(mutex);
  auto it = tables.find(edo);
  if (it == tables.end()) it = tables.emplace(edo, buildTable(edo)).first;
  return it->second; // map nodes stay put, so the reference outlives the lock
}

}

Interval intervalFor(int edo, int steps) {
  if (edo <= 0) return { 1, 1, false };
  steps = ((steps % edo) + edo) % edo;
  return tableFor(edo)[static_cast<size_t>(steps)];
}

int correctionCentsFor(int edo, int steps) {
  if (edo <= 0) return 0;
  steps = ((steps % edo) + edo) % edo;
  auto interval = intervalFor(edo, steps);
  if (!interval.found) return 0;
  double just = 1200.0 * std::log2(static_cast<double>(interval.num) / interval.den);
  return static_cast<int>(std::lround(just - steps * 1200.0 / edo));
}

int correctionCentsInChord(int edo, int value, int bass, int key) {
  if (edo <= 0) return 0;
  if (key < 0) key = 0; // no key set: C, as correctionCentsForNote() reads it
  auto wrap = [edo](int steps) { return ((steps % edo) + edo) % edo; };
  int bass_steps = wrap(bass - key);
  int interval = wrap(value - bass);
  auto root = intervalFor(edo, bass_steps);
  auto upper = intervalFor(edo, interval);
  // No ratio for one of them: the note keeps its place against the key.
  if (!root.found || !upper.found) return correctionCentsFor(edo, value - key);
  auto cents = [](const Interval & i) { return 1200.0 * std::log2(static_cast<double>(i.num) / i.den); };
  double just = cents(root) + cents(upper);
  double equal = (bass_steps + interval) * 1200.0 / edo;
  return static_cast<int>(std::lround(just - equal));
}

int correctionCentsForNote(Tuning tuning, int note_value, int key) {
  int edo = edoStepsFor(tuning);
  if (edo <= 0) return 0;
  return correctionCentsFor(edo, note_value - (key >= 0 ? key : 0));
}

}
