#ifndef _ADDITIVEPRESETS_H_
#define _ADDITIVEPRESETS_H_

#include <string>

// Compiled-in per-preset default values, resolved once in
// Additive::loadParameters() - the same role TapeDegradationPresets.h plays
// for TapeDegradation, and BusEffectRegistry.h for bus effects: default
// *values* here, not a type selection, since every preset is the same
// Additive class. An explicit XML attribute always overrides its preset
// default (Additive::loadParameters()'s own get<float>(name, preset.field)
// calls). Everything in AdditivePresetParams matches an Additive XML
// attribute one-for-one except `preset` itself and `level` (level is a
// generic Instrument-family gain knob, not part of this element's own
// timbral identity).
struct AdditivePresetParams {
  int partials;
  float tilt;
  float velocityTilt;
  int unisonVoices;
  float unisonDetune;
  float inharmonicity;
  float decayA, decayB, decayP;
  bool tuningMatched;
  int partialLimit;
  float attackNoiseLevel;
};

// An unrecognized preset name falls back to this plain default rather than
// asserting - the same "unrecognized name falls back to a real default"
// shape TapeDegradationPresets.h's own getTapeDegradationPreset() and
// bus/BusEffectRegistry.h's slot resolution both already use. A modest,
// generic electric-piano-ish additive tone: harmonic (no stretch), mild
// tilt, single voice (no unison), moderate decay.
inline const AdditivePresetParams & getAdditivePreset(const std::string & name) {
  static const AdditivePresetParams kDefault{
    /* partials         */ 16,
    /* tilt             */ -6.0f,
    /* velocityTilt     */ 3.0f,
    /* unisonVoices     */ 1,
    /* unisonDetune     */ 8.0f,
    /* inharmonicity    */ 0.0f,
    /* decayA           */ 0.3f,
    /* decayB           */ 0.002f,
    /* decayP           */ 1.2f,
    /* tuningMatched    */ true,
    /* partialLimit     */ 8,
    /* attackNoiseLevel */ 0.0f,
  };

  // Plucked/hammered string character - a real struck string's spectrum
  // and decay behavior:
  //  - partials: 28, comfortably covering a piano/guitar-like spectrum
  //    (Nyquist-skip trims the rest for a low-pitched note anyway).
  //  - inharmonicity: 0.0004, in the range real struck strings actually
  //    exhibit - a piano's own coefficient runs roughly 0.0001 (low bass
  //    strings) to the low 0.001s (high treble), and 0.0004 is
  //    representative of a mid-register wound string rather than either
  //    extreme.
  //  - decayA/decayB/decayP tuned so alpha_n = a + b*f_n^1.5 gives a
  //    fundamental time constant (1/alpha) around a second while a
  //    several-kHz partial's own time constant is tens of milliseconds -
  //    the audible "high partials vanish first, the fundamental rings on"
  //    character a struck string has.
  //  - attackNoiseLevel: a short, modest noise burst standing in for the
  //    hammer/pluck transient (see AdditiveVoice.h).
  //  - unisonVoices/unisonDetune: two voices, a few cents apart - just
  //    enough natural chorus/beating for a slightly-detuned real string
  //    pair (piano's own multiple-strings-per-note unisons), not a wide
  //    synth-unison effect.
  static const AdditivePresetParams kStruckString{
    /* partials         */ 28,
    /* tilt             */ -9.0f,
    /* velocityTilt     */ 6.0f,
    /* unisonVoices     */ 2,
    /* unisonDetune     */ 6.0f,
    /* inharmonicity    */ 0.0004f,
    /* decayA           */ 0.1f,
    /* decayB           */ 0.0008f,
    /* decayP           */ 1.5f,
    /* tuningMatched    */ true,
    /* partialLimit     */ 8,
    /* attackNoiseLevel */ 0.08f,
  };

  if (name == "struck-string") return kStruckString;
  return kDefault;
}

#endif
