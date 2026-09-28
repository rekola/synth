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
  //  - inharmonicity: 0.0008, in the range real struck strings actually
  //    exhibit - a piano's own coefficient runs roughly 0.0001 (low bass
  //    strings) to the low 0.001s (high treble). partialLimit lowered to
  //    3 (from 8) specifically so this preset's own stretch is actually
  //    audible: the inharmonicity model (see docs/additive.md) only
  //    stretches partials *above* partialLimit, so at the default
  //    partialLimit=8 essentially every audible, energetic partial (1-8)
  //    stayed exactly harmonic and only the already-quiet tail stretched
  //    a little - reported as "no metallicity/inharmonicity at all."
  //    partialLimit=3 keeps the fundamental/2nd/3rd harmonic (a struck
  //    string's own biggest energy, and the ones that most want to stay
  //    correctly pitched) locked to the scale, while letting the
  //    remaining, still-clearly-audible partials 4 and up stretch for
  //    real (e.g. partial 8 lands roughly half a semitone sharp of pure
  //    harmonic at this B - clearly audible "metallic" character without
  //    losing the note's own core pitch).
  //  - decayA/decayB/decayP tuned so alpha_n = a + b*f_n^1.5 gives a
  //    fundamental time constant (1/alpha) around 2 seconds - a real
  //    struck string's own natural (damper-off) ring, not the ~0.3s the
  //    original decayB gave (reported as "very dampened") - while a
  //    several-kHz partial's own time constant stays tens of milliseconds,
  //    the audible "high partials vanish first, the fundamental rings on"
  //    character a struck string has either way.
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
    /* inharmonicity    */ 0.0008f,
    /* decayA           */ 0.05f,
    /* decayB           */ 0.0001f,
    /* decayP           */ 1.5f,
    /* tuningMatched    */ true,
    /* partialLimit     */ 3,
    /* attackNoiseLevel */ 0.08f,
  };

  if (name == "struck-string") return kStruckString;
  return kDefault;
}

#endif
