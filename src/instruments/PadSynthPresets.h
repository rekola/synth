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
  // beating/chorus-like richness without smearing into noise) and a
  // steep-enough rolloff (1/n^2, not the original 1/n - a bare 1/n
  // spectrum is a sawtooth, which reads as bright/buzzy, not "warm"; a
  // real warm pad needs most of its energy concentrated in the low
  // harmonics) that the upper partials this bandwidth still spreads
  // widely stay too quiet to be heard as anything but a gentle richness,
  // rather than the audible high-frequency wash an early listening pass
  // reported ("much high frequencies... doesn't sound like a pad").
  static const PadSynthPresetParams kWarm{
    /* bandwidth_cents            */ 40.0f,
    /* bandwidth_scale_exponent   */ 0.6f,
    /* partial_count              */ 32,
    /* amplitude_rolloff_exponent */ 2.0f,
    /* formants                   */ {},
  };

  // A handful of vowel-like formant resonances (loosely modeled on an "ah"
  // vowel's own F1/F2/F3) boost specific harmonic bands regardless of
  // fundamental, the way a vocal tract's own fixed resonant cavities do.
  // A first attempt at this (gain 1.4-3x, 100-250Hz-wide bumps, a mild
  // 1.3 rolloff) read as "synth string," not vocal, on an actual listen -
  // the boosts were too gentle and too wide relative to a fairly bright
  // background spectrum to stand out as distinct resonant peaks the way a
  // real formant does (which sits 20-30dB above its own neighboring
  // valleys, not +3-10dB). Fixed by making both sides of the contrast much
  // more extreme: narrow bandwidth (15 cents at the fundamental, so each
  // partial is a near-discrete line) plus a steep 2.2 rolloff empties out
  // the background between formants, while far larger, narrower formant
  // gains (6-10x = +16 to +20dB) than before actually punch through it.
  static const PadSynthPresetParams kFormantVocal{
    /* bandwidth_cents            */ 15.0f,
    /* bandwidth_scale_exponent   */ 0.6f,
    /* partial_count              */ 48,
    /* amplitude_rolloff_exponent */ 2.2f,
    /* formants                   */ {
      { /* center_hz */ 700.0f,  /* bandwidth_hz */ 50.0f, /* gain */ 10.0f },  // F1
      { /* center_hz */ 1220.0f, /* bandwidth_hz */ 70.0f, /* gain */ 7.0f },  // F2
      { /* center_hz */ 2600.0f, /* bandwidth_hz */ 100.0f, /* gain */ 4.0f },  // F3
    },
  };

  // A real bowed string is rich in harmonics with only a gentle rolloff
  // (closer to a sawtooth than a clean sine stack), and a wide, fast-
  // growing bandwidth gives each partial its own natural, ensemble-like
  // beating even from a single generated table - no unison/detune voices
  // layered on top, the beating comes purely from the Gaussian spread
  // itself. The original values (rolloff 0.9, bandwidthScale 1.0, 56
  // partials) pushed this too far and read as mostly noise on an actual
  // listen - "gentle rolloff" still needs enough falloff that a fast-
  // growing bandwidth's own upper-harmonic energy doesn't dominate; tamed
  // to a still-rich-but-recognizably-pitched middle ground.
  static const PadSynthPresetParams kBowedEnsemble{
    /* bandwidth_cents            */ 50.0f,
    /* bandwidth_scale_exponent   */ 0.7f,
    /* partial_count              */ 40,
    /* amplitude_rolloff_exponent */ 1.3f,
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
  // Retuned alongside Bowed Ensemble's own fix (rolloff/bandwidthScale too
  // low, read as mostly noise) even though not separately reported on -
  // this preset shared the identical under-damped shape, so it almost
  // certainly had the same problem; not yet re-verified by ear.
  static const PadSynthPresetParams kMellotron{
    /* bandwidth_cents            */ 45.0f,
    /* bandwidth_scale_exponent   */ 0.7f,
    /* partial_count              */ 36,
    /* amplitude_rolloff_exponent */ 1.6f,
    /* formants                   */ {},
  };

  if (name == "formant-vocal") return kFormantVocal;
  if (name == "bowed-ensemble") return kBowedEnsemble;
  if (name == "glass") return kGlass;
  if (name == "mellotron") return kMellotron;
  return kWarm;
}

#endif
