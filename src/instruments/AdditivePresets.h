#ifndef _ADDITIVEPRESETS_H_
#define _ADDITIVEPRESETS_H_

#include <string>

// Compiled-in per-preset default values, resolved once in
// Additive::loadParameters(): default *values* here, not a type selection,
// since every preset is the same Additive class. An explicit XML attribute
// always overrides its preset default. Everything here matches an Additive
// XML attribute one-for-one except `preset` itself and `level`.
struct AdditivePresetParams {
  int partials;
  float tilt;
  float velocityTilt;
  int unisonVoices;
  float unisonDetune;
  float stretch;
  float decayA, decayB, decayP;
  bool tuningMatched;
  float attackNoiseLevel;
  float keyboardSpread;
  float stringSpread;
};

// An unrecognized preset name falls back to `default`, a plain struck
// string: one string, harmonic partials on the tuning, mild tilt, moderate
// decay.
inline const AdditivePresetParams & getAdditivePreset(const std::string & name) {
  static const AdditivePresetParams kDefault{
      /* partials         */ 16,
      /* tilt             */ -6.0f,
      /* velocityTilt     */ 3.0f,
      /* unisonVoices     */ 1,
      /* unisonDetune     */ 1.0f,
      /* stretch          */ 0.0f,
      /* decayA           */ 0.3f,
      /* decayB           */ 0.002f,
      /* decayP           */ 1.2f,
      /* tuningMatched    */ true,
      /* attackNoiseLevel */ 0.0f,
      /* keyboardSpread   */ 0.0f,
      /* stringSpread     */ 0.0f,
  };

  // A piano: three strings a cent apart, every partial on the tuning with a
  // 0.001 bounded stretch, a short noise burst for the hammer. The tilt,
  // decay and noise values are the ones the previous spectrum used and are
  // replaced by the hammer and decay models in later stages.
  static const AdditivePresetParams kPiano{
      /* partials         */ 28,
      /* tilt             */ -6.0f,
      /* velocityTilt     */ 6.0f,
      /* unisonVoices     */ 3,
      /* unisonDetune     */ 1.0f,
      /* stretch          */ 0.001f,
      /* decayA           */ 0.05f,
      /* decayB           */ 0.0001f,
      /* decayP           */ 1.5f,
      /* tuningMatched    */ true,
      /* attackNoiseLevel */ 0.04f,
      /* keyboardSpread   */ 0.0f,
      /* stringSpread     */ 0.0f,
  };

  if (name == "piano") return kPiano;
  return kDefault;
}

#endif
