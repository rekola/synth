#ifndef _SPECTRALENVELOPEREMAP_H_
#define _SPECTRALENVELOPEREMAP_H_

#include <cmath>
#include <cstddef>
#include <vector>

// Shared between <padsynth> and <additive> - see each instrument's own
// header for the identically-named XML attributes this selects between.
// None means the postprocess stage is off (the default).
enum class SpectralPostprocessKind { None, ResidueClassWeighting, StretchMix };

// Anchored spectral-envelope resampling: resamples a harmonic-amplitude
// profile P[1..count] along the harmonic-number axis so its envelope stays
// pinned to fixed absolute frequencies (like a formant or a body resonance)
// rather than scaling with the played note's own fundamental - the
// "chipmunk effect" a plain harmonic oscillator has when transposed. Output
// still contains only integer harmonics of the played note; only each
// harmonic's *amplitude* is resampled, never its frequency.
//
// Pure, allocation-free (beyond the caller's own output vector, which is
// only resized, never grown/shrunk per call in the steady state), caller-
// owns-the-array utilities - no engine/instrument-specific state. Two
// distinct callers use these at two different times: PadSynthWavetable
// evaluates them once per generated table (per octave region, at that
// region's own reference frequency); SinusoidBank evaluates them once per
// note-on (at that note's real frequency) - see each file's own comment for
// why re-evaluation under pitch bend isn't done.
//
// r = (f / f_a)^p (`f` = the note/sample's own frequency, `f_a` = the
// anchor frequency, `p` = the tracking exponent, [0, 2]):
//   p == 0, or f == f_a: r == 1, identity (no remap).
//   0 < p <= 1: gather. Output harmonic h reads the prototype at fractional
//     position h*r (linear interpolation) - the prototype's own envelope
//     compresses/expands to land on the output's own frequency grid.
//   p > 1 (or r > 1 generally, from a large p or f far above f_a): scatter.
//     Prototype harmonic i lands at output position i/r, split with linear
//     weights between its two neighboring output harmonics - several
//     prototype harmonics can land on the same output harmonic, and are
//     summed there (accumulate_in_power selects how: power/RMS, correct
//     for independent-random-phase partials, where amplitudes summing
//     linearly would be too loud - or plain linear addition, correct when
//     phases are coherent/deterministic).
// Any contribution that would land at or below harmonic 0 folds into h = 1
// (a note has no sub-fundamental harmonic to hold it).
inline void anchoredSpectralEnvelopeRemap(const std::vector<float> & prototype, float note_frequency,
                                           float anchor_frequency, float tracking_exponent,
                                           bool accumulate_in_power, std::vector<float> & out) {
  size_t count = prototype.size();
  out.assign(count, 0.0f);
  if (count == 0 || anchor_frequency <= 0.0f || note_frequency <= 0.0f) {
    out = prototype;
    return;
  }

  float r = std::pow(note_frequency / anchor_frequency, tracking_exponent);

  // Prototype value at 1-indexed harmonic position h; 0 outside [1, count] -
  // this is what makes P[0] == 0 (folding a sub-fundamental gather position
  // to silence) and "past the array end" == 0 both fall out of one rule.
  auto protoAt = [&](int h) -> float {
    if (h < 1 || h > static_cast<int>(count)) return 0.0f;
    return prototype[static_cast<size_t>(h - 1)];
  };

  if (r <= 1.0f) {
    // Gather: direct assignment, no cross-harmonic accumulation - each
    // output harmonic reads exactly one (interpolated) prototype position,
    // so there is nothing for accumulate_in_power to do here. This formula
    // also naturally reduces to the identity at r == 1 (x == h, frac == 0),
    // so no separate identity special-case is needed.
    for (int h = 1; h <= static_cast<int>(count); h++) {
      float x = static_cast<float>(h) * r;
      int i0 = static_cast<int>(std::floor(x));
      float frac = x - static_cast<float>(i0);
      float v0 = protoAt(i0);
      float v1 = protoAt(i0 + 1);
      out[static_cast<size_t>(h - 1)] = v0 + frac * (v1 - v0);
    }
    return;
  }

  // Scatter: prototype harmonic i's own amplitude, weighted, lands on one
  // or two output harmonics; several prototype harmonics can converge on
  // the same output harmonic. Linear accumulation and power accumulation
  // are gathered in parallel per output bin and one is picked at the end,
  // rather than branching inside the hot loop.
  std::vector<float> linear_accum(count, 0.0f);
  std::vector<float> power_accum(count, 0.0f);

  auto scatter = [&](int target_h, float contribution) {
    int h = target_h < 1 ? 1 : target_h; // fold sub-fundamental into h = 1
    if (h < 1 || h > static_cast<int>(count)) return;
    linear_accum[static_cast<size_t>(h - 1)] += contribution;
    power_accum[static_cast<size_t>(h - 1)] += contribution * contribution;
  };

  for (int i = 1; i <= static_cast<int>(count); i++) {
    float value = prototype[static_cast<size_t>(i - 1)];
    if (value == 0.0f) continue;
    float pos = static_cast<float>(i) / r;
    int i0 = static_cast<int>(std::floor(pos));
    float frac = pos - static_cast<float>(i0);
    scatter(i0, (1.0f - frac) * value);
    scatter(i0 + 1, frac * value);
  }

  for (size_t idx = 0; idx < count; idx++) {
    out[idx] = accumulate_in_power ? std::sqrt(power_accum[idx]) : linear_accum[idx];
  }
}

// Stage 2a: residue-class weighting - harmonics h with h mod n == r (mod n)
// keep full amplitude; every other harmonic is scaled by (1 - amount).
// Operates on a 1-indexed-by-position array in place.
inline void residueClassWeighting(std::vector<float> & spectrum, int n, int r, float amount) {
  if (n <= 0) return;
  int target_residue = ((r % n) + n) % n;
  for (size_t idx = 0; idx < spectrum.size(); idx++) {
    int h = static_cast<int>(idx) + 1;
    int residue = ((h % n) + n) % n;
    if (residue != target_residue) spectrum[idx] *= (1.0f - amount);
  }
}

// Stage 2b: stretch-mix - out = (1-a)*S + a*S', where S'[n*h] = S[h] and
// every other bin of S' is 0 (a copy of S stretched out by a factor of n
// along the harmonic axis, discarding anything past the array's own end).
// `spectrum` and `out` may not alias.
inline void stretchMix(const std::vector<float> & spectrum, int n, float amount, std::vector<float> & out) {
  size_t count = spectrum.size();
  out.assign(count, 0.0f);
  for (size_t idx = 0; idx < count; idx++) out[idx] = (1.0f - amount) * spectrum[idx];
  if (n <= 0) return;
  for (size_t idx = 0; idx < count; idx++) {
    size_t h = idx + 1;
    size_t target = h * static_cast<size_t>(n);
    if (target >= 1 && target <= count) out[target - 1] += amount * spectrum[idx];
  }
}

#endif
