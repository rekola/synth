#ifndef _ADDITIVEPRESETS_H_
#define _ADDITIVEPRESETS_H_

#include "AdditiveModel.h"

#include <string>
#include <vector>

// Compiled-in per-preset default values, resolved once in
// Additive::loadParameters(): default *values* here, not a type selection,
// since every preset is the same Additive class. An explicit XML attribute
// always overrides its preset default. Everything here matches an Additive
// XML attribute one-for-one except `preset`, `level` and the body table.
//
// Only a few values come from a source or a derivation: the strike point of
// 1/8 and the 1 cent between unison strings (the brief), the stretch of
// 0.001 (the FM pianos'), the felt exponent behind hammerVelocity, and the
// free-free bar mode ratios. Every other number below is a starting value
// chosen to be listened to and adjusted, not a measurement of a real
// instrument.
struct AdditivePresetParams {
  int partials;
  float stretch;
  bool tuningMatched;
  int unisonVoices;
  float unisonDetune;
  const char * modes;
  bool pluck;
  float strike;
  float hammerCutoff;
  float hammerTracking;
  float hammerVelocity;
  float pluckCutoff;
  float partialFloor;
  float decayA, decayB, decayP;
  float decayTracking;
  float decaySpread;
  float thump;
  float thumpWidth;
  float keyboardSpread;
  float stringSpread;
  std::vector<BodyMode> body;
};

namespace additive_presets {
// A felt whose force grows as the cube of the compression: contact time goes
// as velocity^-0.5. A round value, not taken from a measurement.
constexpr float kFeltExponent = 3.0f;
} // namespace additive_presets

// An unrecognized preset name falls back to `default`, a plain struck
// string: one string, partials on the tuning, a mild hammer, moderate decay.
inline const AdditivePresetParams & getAdditivePreset(const std::string & name) {
  static const AdditivePresetParams kDefault{
      /* partials       */ 16,
      /* stretch        */ 0.0f,
      /* tuningMatched  */ true,
      /* unisonVoices   */ 1,
      /* unisonDetune   */ 1.0f,
      /* modes          */ "",
      /* pluck          */ false,
      /* strike         */ 0.125f,
      /* hammerCutoff   */ 4000.0f,
      /* hammerTracking */ 0.0f,
      /* hammerVelocity */ hammerVelocityExponent(additive_presets::kFeltExponent),
      /* pluckCutoff    */ 0.0f,
      /* partialFloor   */ -60.0f,
      /* decayA         */ 0.3f,
      /* decayB         */ 0.002f,
      /* decayP         */ 1.2f,
      /* decayTracking  */ 0.0f,
      /* decaySpread    */ 0.0f,
      /* thump          */ 0.0f,
      /* thumpWidth     */ 0.0f,
      /* keyboardSpread */ 0.0f,
      /* stringSpread   */ 0.0f,
      /* body           */ {},
  };

  // A piano: three strings a cent apart, every partial on the tuning with a
  // 0.001 bounded stretch, struck at 1/8 by a hammer whose corner is fixed in
  // Hz (so the bass has many partials), outer strings decaying half again
  // faster and slower than the middle one, bass ringing longer, and a short
  // soundboard thump spread wide. The decay and thump numbers are starting
  // values for listening.
  static const AdditivePresetParams kPiano{
      /* partials       */ 64,
      /* stretch        */ 0.001f,
      /* tuningMatched  */ true,
      /* unisonVoices   */ 3,
      /* unisonDetune   */ 1.0f,
      /* modes          */ "",
      /* pluck          */ false,
      /* strike         */ 0.125f,
      /* hammerCutoff   */ 2000.0f,
      /* hammerTracking */ 0.0f,
      /* hammerVelocity */ hammerVelocityExponent(additive_presets::kFeltExponent),
      /* pluckCutoff    */ 0.0f,
      /* partialFloor   */ -60.0f,
      /* decayA         */ 0.05f,
      /* decayB         */ 0.0001f,
      /* decayP         */ 1.5f,
      /* decayTracking  */ 0.5f,
      /* decaySpread    */ 0.5f,
      /* thump          */ 0.3f,
      /* thumpWidth     */ 60.0f,
      /* keyboardSpread */ 0.0f,
      /* stringSpread   */ 0.0f,
      /* body           */ {{70.0f, 1.0f, 60.0f}, {120.0f, 0.7f, 50.0f}, {200.0f, 0.5f, 70.0f}},
  };

  // A nylon-string guitar: one string, a soft fingertip pluck, high partials
  // dying quickly. The body modes are placeholders near the low end of a
  // guitar's range.
  static const AdditivePresetParams kGuitarNylon{
      /* partials       */ 30,
      /* stretch        */ 0.0005f,
      /* tuningMatched  */ true,
      /* unisonVoices   */ 1,
      /* unisonDetune   */ 1.0f,
      /* modes          */ "",
      /* pluck          */ true,
      /* strike         */ 0.2f,
      /* hammerCutoff   */ 0.0f,
      /* hammerTracking */ 0.0f,
      /* hammerVelocity */ 0.0f,
      /* pluckCutoff    */ 1500.0f,
      /* partialFloor   */ -60.0f,
      /* decayA         */ 0.4f,
      /* decayB         */ 0.0003f,
      /* decayP         */ 1.5f,
      /* decayTracking  */ 0.3f,
      /* decaySpread    */ 0.0f,
      /* thump          */ 0.25f,
      /* thumpWidth     */ 20.0f,
      /* keyboardSpread */ 0.0f,
      /* stringSpread   */ 0.0f,
      /* body           */ {{100.0f, 1.0f, 40.0f}, {200.0f, 0.6f, 50.0f}},
  };

  // A steel-string guitar: the same machinery with a bright pick, a longer
  // ring, slower-dying high partials and a little more stretch.
  static const AdditivePresetParams kGuitarSteel{
      /* partials       */ 40,
      /* stretch        */ 0.002f,
      /* tuningMatched  */ true,
      /* unisonVoices   */ 1,
      /* unisonDetune   */ 1.0f,
      /* modes          */ "",
      /* pluck          */ true,
      /* strike         */ 0.2f,
      /* hammerCutoff   */ 0.0f,
      /* hammerTracking */ 0.0f,
      /* hammerVelocity */ 0.0f,
      /* pluckCutoff    */ 5000.0f,
      /* partialFloor   */ -60.0f,
      /* decayA         */ 0.25f,
      /* decayB         */ 0.0001f,
      /* decayP         */ 1.5f,
      /* decayTracking  */ 0.3f,
      /* decaySpread    */ 0.0f,
      /* thump          */ 0.25f,
      /* thumpWidth     */ 20.0f,
      /* keyboardSpread */ 0.0f,
      /* stringSpread   */ 0.0f,
      /* body           */ {{110.0f, 1.0f, 35.0f}, {220.0f, 0.6f, 45.0f}},
  };

  // A harp: long decays that lengthen strongly toward the bass, strings
  // spread across the instrument's width.
  static const AdditivePresetParams kHarp{
      /* partials       */ 24,
      /* stretch        */ 0.0005f,
      /* tuningMatched  */ true,
      /* unisonVoices   */ 1,
      /* unisonDetune   */ 1.0f,
      /* modes          */ "",
      /* pluck          */ true,
      /* strike         */ 0.2f,
      /* hammerCutoff   */ 0.0f,
      /* hammerTracking */ 0.0f,
      /* hammerVelocity */ 0.0f,
      /* pluckCutoff    */ 3000.0f,
      /* partialFloor   */ -60.0f,
      /* decayA         */ 0.15f,
      /* decayB         */ 0.00005f,
      /* decayP         */ 1.5f,
      /* decayTracking  */ 0.6f,
      /* decaySpread    */ 0.0f,
      /* thump          */ 0.15f,
      /* thumpWidth     */ 40.0f,
      /* keyboardSpread */ 60.0f,
      /* stringSpread   */ 1.0f,
      /* body           */ {{90.0f, 1.0f, 30.0f}, {160.0f, 0.5f, 40.0f}},
  };

  // A harpsichord: two plucked strings, a bright fixed pluck that does not
  // change with velocity, a fast decay with no aftersound.
  static const AdditivePresetParams kHarpsichord{
      /* partials       */ 30,
      /* stretch        */ 0.0005f,
      /* tuningMatched  */ true,
      /* unisonVoices   */ 2,
      /* unisonDetune   */ 0.5f,
      /* modes          */ "",
      /* pluck          */ true,
      /* strike         */ 0.18f,
      /* hammerCutoff   */ 0.0f,
      /* hammerTracking */ 0.0f,
      /* hammerVelocity */ 0.0f,
      /* pluckCutoff    */ 6000.0f,
      /* partialFloor   */ -60.0f,
      /* decayA         */ 0.8f,
      /* decayB         */ 0.0002f,
      /* decayP         */ 1.5f,
      /* decayTracking  */ 0.3f,
      /* decaySpread    */ 0.0f,
      /* thump          */ 0.0f,
      /* thumpWidth     */ 0.0f,
      /* keyboardSpread */ 0.0f,
      /* stringSpread   */ 0.0f,
      /* body           */ {},
  };

  // An ideal free-free bar: modes at the roots of cos(x)*cosh(x) = 1,
  // 4.730, 7.853, 10.996, 14.137, squared and divided by the first. A
  // marimba's bars are cut to move the second mode near 4:1; this one is not.
  static const AdditivePresetParams kBar{
      /* partials       */ 4,
      /* stretch        */ 0.0f,
      /* tuningMatched  */ true,
      /* unisonVoices   */ 1,
      /* unisonDetune   */ 1.0f,
      /* modes          */ "1 2.756 5.404 8.933",
      /* pluck          */ false,
      /* strike         */ 0.125f,
      /* hammerCutoff   */ 3000.0f,
      /* hammerTracking */ 0.0f,
      /* hammerVelocity */ hammerVelocityExponent(additive_presets::kFeltExponent),
      /* pluckCutoff    */ 0.0f,
      /* partialFloor   */ -60.0f,
      /* decayA         */ 0.4f,
      /* decayB         */ 0.00005f,
      /* decayP         */ 1.5f,
      /* decayTracking  */ 0.3f,
      /* decaySpread    */ 0.0f,
      /* thump          */ 0.0f,
      /* thumpWidth     */ 0.0f,
      /* keyboardSpread */ 0.0f,
      /* stringSpread   */ 0.0f,
      /* body           */ {},
  };

  if (name == "piano") return kPiano;
  if (name == "guitar-nylon") return kGuitarNylon;
  if (name == "guitar-steel") return kGuitarSteel;
  if (name == "harp") return kHarp;
  if (name == "harpsichord") return kHarpsichord;
  if (name == "bar") return kBar;
  return kDefault;
}

#endif
