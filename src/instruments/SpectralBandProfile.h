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
  if (!tuning_matched || edo_steps <= 0 || n > partial_limit) {
    return static_cast<float>(n);
  }
  float steps = std::round(static_cast<float>(edo_steps) * std::log2(static_cast<float>(n)));
  return std::pow(2.0f, steps / static_cast<float>(edo_steps));
}

#endif
