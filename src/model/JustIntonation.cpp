#include "JustIntonation.h"
#include "Note.h"

#include <algorithm>
#include <array>
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

std::optional<int> ScaleCorrections::forNote(int note_value) const {
  if (edo <= 0) return std::nullopt;
  auto step = static_cast<size_t>((((note_value - key) % edo) + edo) % edo);
  if (!present[step]) return std::nullopt;
  return cents[step];
}

ScaleCorrections scaleCorrectionsFor(Tuning tuning, Scale scale, int key) {
  ScaleCorrections out;
  int edo = edoStepsFor(tuning);
  auto just = justScale(scale);
  if (edo <= 0 || edo > ScaleCorrections::kMaxSteps || !just) return out;
  const auto table = just->gamut;
  out.edo = edo;
  out.key = ((std::max(key, 0) % edo) + edo) % edo;
  const double step_cents = 1200.0 / edo;

  // Every note of the scale's gamut goes to its nearest step, in the scale or not. Where
  // several share a step, a note in the scale beats one of the scale's side,
  // which beats the rest; the lowest ratio breaks a tie.
  std::array<int, ScaleCorrections::kMaxSteps> rank{};
  std::vector<std::size_t> order(table.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::sort(order.begin(), order.end(),
            [&](std::size_t a, std::size_t b) { return table[a].ratio < table[b].ratio; });
  for (auto i : order) {
    const auto & note = table[i];
    int step = ((note.ratio.steps(edo) % edo) + edo) % edo;
    auto index = static_cast<std::size_t>(step);
    int note_rank = just->contains(note) ? 0 : just->prefers(note) ? 1
                                                                   : 2;
    if (out.present[index] && rank[index] <= note_rank) continue;
    out.cents[index] = static_cast<int16_t>(std::lround(note.ratio.cents() - step * step_cents));
    out.present[index] = true;
    rank[index] = note_rank;
  }
  return out;
}

int correctionCentsInChord(int edo, int value, int bass, int key, const ScaleCorrections & scale) {
  if (scale.empty() || scale.edo != edo) return correctionCentsInChord(edo, value, bass, key);
  if (auto own = scale.forNote(value)) return *own;
  auto bass_correction = scale.forNote(bass);
  if (!bass_correction) return correctionCentsInChord(edo, value, bass, key);
  auto upper = intervalFor(edo, value - bass);
  // No ratio for the interval: the note keeps its place against the key.
  if (!upper.found) return correctionCentsFor(edo, value - std::max(key, 0));
  double step_cents = 1200.0 / edo;
  int interval = (((value - bass) % edo) + edo) % edo;
  double upper_cents = 1200.0 * std::log2(static_cast<double>(upper.num) / upper.den);
  return static_cast<int>(std::lround(*bass_correction + upper_cents - interval * step_cents));
}

int correctionCentsForNote(Tuning tuning, int note_value, int key) {
  int edo = edoStepsFor(tuning);
  if (edo <= 0) return 0;
  return correctionCentsFor(edo, note_value - (key >= 0 ? key : 0));
}

}
