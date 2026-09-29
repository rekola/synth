#ifndef _SPECTRALBANDPROFILE_H_
#define _SPECTRALBANDPROFILE_H_

#include "Tuning.h"

#include <cmath>

// Shared partial-frequency mapping for PadSynth/Additive: snaps a harmonic
// partial to the nearest step of the song's own tuning, so a wavetable/
// sinusoid bank built for e.g. 31-EDO has partials that land on 31-EDO scale
// steps rather than pure harmonic ratios that would otherwise beat against
// the tuning's own notes. Only the first partial_limit partials are snapped -
// beyond that, higher partials revert to plain harmonic ratios (snapping
// arbitrarily high partials would just be pure quantization noise, since
// steps get denser than the harmonic series as n grows).
//
// tuning_matched=false (or edo_steps<=0, i.e. Tuning::PERCUSSION) always
// returns the plain harmonic ratio n, for A/B comparison and for tunings
// with no interval structure to snap to.
//
// Resampling a table built for one EDO to a different scale step keeps every
// snapped partial locked to a step of that same EDO (the ratio is computed
// from edo_steps, not from a fixed sample rate/pitch), which is the point of
// tuning-matching in the first place - only the harmonic-region partials
// above partial_limit drift by the same fractional-Hz amount an ordinary
// harmonic wavetable's would.
inline float tuningMatchedPartialRatio(int n, int edo_steps, int partial_limit, bool tuning_matched) {
  if (!tuning_matched || edo_steps <= 0 || n < 1 || n > partial_limit) {
    return static_cast<float>(n);
  }
  float steps = std::round(static_cast<float>(edo_steps) * std::log2(static_cast<float>(n)));
  return std::pow(2.0f, steps / static_cast<float>(edo_steps));
}

// m with every factor of 2 removed - e.g. oddPart(24) == 3, oddPart(32) == 1.
inline int oddPart(int m) {
  m = m < 0 ? -m : m;
  while (m > 0 && m % 2 == 0) m /= 2;
  return m;
}

// PadSynth's own tuning-matching rule (the anchored-spectral-envelope-
// remap feature - see dsp/SpectralEnvelopeRemap.h/ImportedPadSynthProfile.h):
// applied to a single partial already placed at g_h (harmonic-ratio units,
// from ImportedPadSynthProfile.h's partialPosition()) - independent of the
// partial's own index/count, unlike tuningMatchedPartialRatio()'s
// partial_limit cutoff above (that function is unchanged, still used by
// SinusoidBank.cpp's own additive-oscillator inharmonicity model, a
// separate, pre-existing feature this one doesn't touch).
//
// m is the nearest integer harmonic number to g_h. Partial h is "tuned" -
// placed exactly at m's own tuned position, discarding any fractional
// stretch g_h had - when m's odd part (m with every factor of 2 removed)
// is <= L: at the default L=7 this is every m in {1-8, 10, 12, 14, 16, 20,
// 24, 28, 32, ...} - every octave-doubling of a small odd number, not just
// a fixed initial run. Every other partial keeps g_h exactly as given
// (untempered, stretch intact). tuning_matched==false or edo_steps<=0
// always returns g_h unchanged - same "nothing to snap to" contract as
// tuningMatchedPartialRatio() above.
inline float tuningMatchedPartialPosition(float g_h, int edo_steps, bool tuning_matched, int L = 7) {
  if (!tuning_matched || edo_steps <= 0) return g_h;
  int m = static_cast<int>(std::lround(g_h));
  if (m < 1 || oddPart(m) > L) return g_h;
  float steps = std::round(static_cast<float>(edo_steps) * std::log2(static_cast<float>(m)));
  return std::pow(2.0f, steps / static_cast<float>(edo_steps));
}

#endif
