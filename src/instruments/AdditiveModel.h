#ifndef _ADDITIVEMODEL_H_
#define _ADDITIVEMODEL_H_

#include "SinusoidBank.h"
#include "../model/NoteCoordinate.h"

#include <string>
#include <vector>

// The <additive> instrument's sound as a pure function: what partials a
// note has (frequency, level, decay, phase, which output row), with no audio
// and no state, so tests read the numbers directly.
//
// A note is `unison_voices` strings, one output row each, plus one row per
// body mode. A string's partials start at the level its excitation gives
// them; their frequencies sit on the song's tuning.

enum class Excitation { Hammer,
                        Pluck };

// One fixed-frequency resonator standing in for the instrument's body or
// soundboard: the same for every key, so it is not tuned to the note.
struct BodyMode {
  float frequency_hz;
  float amplitude; // times thump and the note's level
  float alpha;     // decay, nepers/second
};

struct AdditiveModelParams {
  int partials = 16;
  float stretch = 0.0f; // epsilon: partial n sits (n-1)*epsilon above its grid position
  bool tuning_matched = true;
  int unison_voices = 1;            // strings per note, 1-3
  float unison_detune_cents = 1.0f; // spacing between adjacent strings
  // Mode frequency ratios replacing the harmonic series (bars, bells); empty
  // means a string. Modes sit at the note's frequency times the ratio, off
  // the tuning grid, with no stretch.
  std::vector<float> modes;

  Excitation excitation = Excitation::Hammer;
  float strike = 0.125f;            // strike or pluck point, fraction of the string
  float hammer_cutoff_hz = 4000.0f; // hammer lowpass corner at middle C, velocity 0.5
  float hammer_tracking = 0.0f;     // corner scales by (f0 / middle C)^tracking
  float hammer_velocity = 0.5f;     // corner scales by (velocity / 0.5)^exponent
  float pluck_cutoff_hz = 0.0f;     // fixed pluck lowpass corner, 0 = off
  float partial_floor_db = -60.0f;  // partials starting below this vs the strongest are not built

  float decay_a = 0.3f, decay_b = 0.002f, decay_p = 1.2f; // alpha = a + b * f^p
  float decay_tracking = 0.0f;                            // alpha scales by (f0 / middle C)^tracking
  float decay_spread = 0.0f;                              // alpha of the outer strings is (1 +- spread) times the middle's

  float thump = 0.0f;          // body level relative to the note's level, 0 = off
  float thump_tracking = 0.0f; // body level scales by (middle C / f0)^tracking: bass knocks harder
  std::vector<BodyMode> body;
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
// inharmonicity (at most about 1200*log2(1+stretch) cents above the grid)
// that keeps shared partials of any interval up to an octave within stretch
// times the fundamental of each other. Partial 1 is exactly 1.
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

// Azimuth offset in degrees of body mode `mode` of `modes`, spread over
// `width_deg` and centred. Consecutive modes are dealt alternately to the
// outside and inside slots, so neighbours in frequency sit apart.
float bodyAzimuthOffsetDeg(int mode, int modes, float width_deg);

// Second-order lowpass magnitude at `frequency_hz` for corner `cutoff_hz`
// (the smooth spectrum of a short force pulse falls about 12 dB/octave).
float lowpassGain(float frequency_hz, float cutoff_hz);

// The hammer lowpass corner for a note: the felt's contact time shrinks with
// hammer speed, so a harder hit has a higher corner.
float hammerCutoffHz(const AdditiveModelParams & params, float frequency, float velocity);

// Exponent of corner against velocity for a felt whose force grows as
// compression^p: contact time goes as velocity^(-(p-1)/(p+1)).
float hammerVelocityExponent(float felt_exponent);

// "1 2.756 5.404" -> {1, 2.756, 5.404}; ignores anything unparseable.
std::vector<float> parseModeRatios(const std::string & text);

std::vector<PartialSpec> buildPartialSpecs(const AdditiveModelParams & params, const NoteContext & note);

#endif
