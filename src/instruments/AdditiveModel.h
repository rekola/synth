#ifndef _ADDITIVEMODEL_H_
#define _ADDITIVEMODEL_H_

#include "SinusoidBank.h"
#include "../model/NoteCoordinate.h"

#include <vector>

// The <additive> instrument's sound as a pure function: what partials a
// note has (frequency, level, decay, phase, which string), with no audio and
// no state, so tests read the numbers directly.
struct AdditiveModelParams {
  int partials = 16;
  float tilt_db = -6.0f;         // dB/octave of partial number
  float velocity_tilt_db = 3.0f; // tilt change over a full velocity swing
  float stretch = 0.0f;          // epsilon: partial n sits (n-1)*epsilon above its grid position
  bool tuning_matched = true;
  int unison_voices = 1;                                  // strings per note, 1-3
  float unison_detune_cents = 1.0f;                       // spacing between adjacent strings
  float decay_a = 0.3f, decay_b = 0.002f, decay_p = 1.2f; // alpha = a + b * f^p
};

struct NoteContext {
  float frequency; // fundamental, Hz
  float velocity;  // [0, 1]
  float sample_rate;
  int edo_steps; // 0 = no tuning structure
  NoteCoordinate coord;
};

// Frequency ratio of partial n of `partials`. On a tuning, the nearest step
// of the song's tuning, so partials shared by chord tones of the tuning's own
// intervals meet exactly; then (n-1)*stretch is added, a bounded form of
// inharmonicity (at most 1200*log2(1+stretch) cents above the grid) that
// keeps shared partials of any interval up to an octave within
// stretch times the fundamental of each other. Partial 1 is exactly 1.
float additivePartialRatio(int n, int partials, float stretch, int edo_steps, bool tuning_matched);

// Offset in cents of string `string` of `strings`: evenly spaced by
// `spacing_cents` and centred on 0 (so the note's pitch is exact), plus a
// small hashed jitter so copies don't beat at a mechanical rate.
float unisonOffsetCents(int string, int strings, float spacing_cents, const NoteCoordinate & coord);

// Where a note sits across a keyboard, -0.5 (lowest key) to 0.5 (highest),
// by its pitch relative to middle C over an 88-key span.
float keyboardPosition(float frequency);

// Azimuth offset in degrees of string `string` of `strings` from the key's
// position: spread evenly, centred.
float stringAzimuthOffsetDeg(int string, int strings, float spacing_deg);

std::vector<PartialSpec> buildPartialSpecs(const AdditiveModelParams & params, const NoteContext & note);

#endif
