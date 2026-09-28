#ifndef _PADSYNTHPRESETS_H_
#define _PADSYNTHPRESETS_H_

#include "PadSynthWavetable.h"

#include <string>
#include <vector>

// Compiled-in per-preset default values, resolved once in
// PadSynth::loadParameters() - the same role TapeDegradationPresets.h
// plays for tape degradation. An explicit XML attribute always overrides
// its preset default (see PadSynth::loadParameters()'s own
// get<float>(name, preset.field) calls).
//
// amplitude_rolloff_exponent and formants are this codebase's own
// extension beyond the bare bandwidth/bandwidthScale/partials PADsynth
// parameters (see PadSynthWavetable.h's own doc comment on
// amplitude_rolloff_exponent/PadSynthFormant) - without them, every preset
// would only be able to differ by bandwidth and partial count, which
// can't tell a clean "glass" bell apart from a "formant-vocal" character
// the way a real per-harmonic amplitude shape can.
struct PadSynthPresetParams {
  float bandwidth_cents;
  float bandwidth_scale_exponent;
  int partial_count;
  float amplitude_rolloff_exponent;
  std::vector<PadSynthFormant> formants;
};

// An unrecognized preset name falls back to "warm" rather than asserting -
// the same "unrecognized name falls back to a real default" shape
// TapeDegradationPresets.h's own getTapeDegradationPreset() already uses.
inline const PadSynthPresetParams & getPadSynthPreset(const std::string & name) {
  // The sensible middle-ground default: moderate bandwidth (some
  // beating/chorus-like richness without smearing into noise), a natural
  // 1/n harmonic rolloff, moderate partial count.
  static const PadSynthPresetParams kWarm{
    /* bandwidth_cents            */ 40.0f,
    /* bandwidth_scale_exponent   */ 0.8f,
    /* partial_count              */ 48,
    /* amplitude_rolloff_exponent */ 1.0f,
    /* formants                   */ {},
  };

  // A handful of vowel-like formant resonances (loosely modeled on an "ah"
  // vowel's own F1/F2/F3) boost specific harmonic bands regardless of
  // fundamental, the way a vocal tract's own fixed resonant cavities do -
  // narrow bandwidth (so the formant peaks read as distinct, not just a
  // gentle tilt) and a fairly steep rolloff otherwise, so the boosted
  // bands stand out clearly against the (otherwise quiet) higher partials.
  static const PadSynthPresetParams kFormantVocal{
    /* bandwidth_cents            */ 25.0f,
    /* bandwidth_scale_exponent   */ 0.6f,
    /* partial_count              */ 64,
    /* amplitude_rolloff_exponent */ 1.3f,
    /* formants                   */ {
      { /* center_hz */ 700.0f,  /* bandwidth_hz */ 100.0f, /* gain */ 3.0f },  // F1
      { /* center_hz */ 1220.0f, /* bandwidth_hz */ 150.0f, /* gain */ 2.0f },  // F2
      { /* center_hz */ 2600.0f, /* bandwidth_hz */ 250.0f, /* gain */ 1.4f },  // F3
    },
  };

  // A real bowed string is rich in both odd and even harmonics with only a
  // gentle rolloff (closer to a sawtooth than a clean sine stack), and the
  // wide, fast-growing bandwidth gives each partial its own natural,
  // ensemble-like beating even from a single generated table - no
  // unison/detune voices layered on top, the beating comes purely from the
  // Gaussian spread itself.
  static const PadSynthPresetParams kBowedEnsemble{
    /* bandwidth_cents            */ 70.0f,
    /* bandwidth_scale_exponent   */ 1.0f,
    /* partial_count              */ 56,
    /* amplitude_rolloff_exponent */ 0.9f,
    /* formants                   */ {},
  };

  // Narrow bandwidth and a steep rolloff - most of the energy sits in the
  // fundamental and a handful of clean, minimally-beating overtones, the
  // bell/glass-like clarity a wide band would otherwise smear away.
  static const PadSynthPresetParams kGlass{
    /* bandwidth_cents            */ 8.0f,
    /* bandwidth_scale_exponent   */ 0.5f,
    /* partial_count              */ 32,
    /* amplitude_rolloff_exponent */ 1.5f,
    /* formants                   */ {},
  };

  // A Mellotron's "strings" tape was 3 real violins recorded per note - this
  // preset's own job is purely spectral character (what a massed bowed
  // string ensemble sounds like), not literal unison/detune voices; that
  // comes from <tapeDegradation preset="mellotron"> layered on top instead
  // (built separately - see docs/tape_degradation.md).
  // Close to Bowed Ensemble's own wide, beating bandwidth (a bowed string
  // ensemble is exactly what a real Mellotron strings tape captured) but
  // slightly narrower/steeper, reading a little more "recorded tape"
  // and a little less "live" than Bowed Ensemble's own wider spread.
  static const PadSynthPresetParams kMellotron{
    /* bandwidth_cents            */ 55.0f,
    /* bandwidth_scale_exponent   */ 0.9f,
    /* partial_count              */ 48,
    /* amplitude_rolloff_exponent */ 1.0f,
    /* formants                   */ {},
  };

  if (name == "formant-vocal") return kFormantVocal;
  if (name == "bowed-ensemble") return kBowedEnsemble;
  if (name == "glass") return kGlass;
  if (name == "mellotron") return kMellotron;
  return kWarm;
}

#endif
