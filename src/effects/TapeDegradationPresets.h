#ifndef _TAPEDEGRADATIONPRESETS_H_
#define _TAPEDEGRADATIONPRESETS_H_

#include <string>

// Compiled-in per-preset default values, resolved once in
// TapeDegradation::loadParameters() - the same role BusEffectRegistry.h
// plays for bus effects, but default *values* here, not a type selection,
// since every preset is the same TapeDegradation class. An explicit XML
// attribute always overrides its preset default - see loadParameters()'s
// own getFloat(name, preset.field) calls.
struct TapeDegradationPreset {
  float wowRateHz, wowDepthCents;
  bool wowLocked;
  float wowLockedRateHz;
  float flutterRateHz, flutterDepthCents;
  float healthRateHz, healthSensitivity;
  float hissLevelDB;
  float dropoutRateHz, dropoutDepthDB, dropoutDurationMs;
  float clickRateHz, clickGainDB;
  float saturationDriveDB;
  float lowCutHz;
  float hfRolloffHz;
  float headBumpHz, headBumpGainDB;
  float mix;
  float ampFlutterDepth;
  float swoopStartCents, swoopTimeMs;
  float spinDownMs, droopDepthCents;
  float rumbleLevelDB, rumbleHz;
  bool decayMode;
  float decayRatePerMinute;
  float breathingAmount, hissLevelDependent;
  float breathingAttackMs, breathingReleaseMs;
};

// Every preset shares the same underlying TapeDegradation class and full
// parameter set - what makes each preset distinct is purely which knobs it
// sets away from the "tape" baseline, not any different code path:
//
//  - tape/mellotron/studio: no distinguishing machinery of their own,
//    just different wow/flutter/hiss/tone values (and, for Mellotron, the
//    ampFlutterDepth/swoopStartCents/swoopTimeMs fields voice-attached use
//    actually reads).
//  - cassette/opticalFilm: both drive the one shared envelope-follower
//    breathing/grain mechanism (TapeDegradationDsp), aimed at different
//    destinations (breathingAmount -> HF cutoff, hissLevelDependent ->
//    hiss/grain gain).
//  - vinyl: wowLocked + a nonzero rumbleLevelDB + a raised clickRateHz.
//  - disintegration: decayMode on, everything else close to "tape".
//
// An unrecognized preset name falls back to "tape" rather than asserting,
// the same "unrecognized name falls back to a real default" shape
// bus/BusEffectRegistry.h's own <bus> slot resolution already uses.
inline const TapeDegradationPreset & getTapeDegradationPreset(const std::string & name) {
  static const TapeDegradationPreset kTape{
    /* wowRateHz            */ 0.7f,
    /* wowDepthCents        */ 8.0f,
    /* wowLocked            */ false,
    /* wowLockedRateHz      */ 0.5556f,
    /* flutterRateHz        */ 8.0f,
    /* flutterDepthCents    */ 3.0f,
    /* healthRateHz         */ 0.2f,
    /* healthSensitivity    */ 0.5f,
    /* hissLevelDB          */ -48.0f,
    /* dropoutRateHz        */ 0.15f,
    /* dropoutDepthDB       */ -12.0f,
    /* dropoutDurationMs    */ 15.0f,
    /* clickRateHz          */ 0.3f,
    /* clickGainDB          */ -18.0f,
    /* saturationDriveDB    */ 3.0f,
    /* lowCutHz             */ 20.0f,
    /* hfRolloffHz          */ 12000.0f,
    /* headBumpHz           */ 80.0f,
    /* headBumpGainDB       */ 3.0f,
    /* mix                  */ 1.0f,
    /* ampFlutterDepth      */ 0.0f,
    /* swoopStartCents      */ 0.0f,
    /* swoopTimeMs          */ 150.0f,
    /* spinDownMs           */ 300.0f,
    /* droopDepthCents      */ 4.0f,
    /* rumbleLevelDB        */ -100.0f,
    /* rumbleHz             */ 80.0f,
    /* decayMode            */ false,
    /* decayRatePerMinute   */ 0.0f,
    /* breathingAmount      */ 0.0f,
    /* hissLevelDependent   */ 0.0f,
    /* breathingAttackMs    */ 5.0f,
    /* breathingReleaseMs   */ 80.0f,
  };

  // Independent per-note drift/hiss falls out for free from voice
  // attachment (each note gets its own TapeTransportDsp - see
  // TapeDegradation.cpp) - this preset only needs to set the values that
  // make a Mellotron actually sound like one: pronounced wow/flutter, a
  // narrow tape-strip band, audible hiss, the attack pitch-swoop, and the
  // pressure-pad amplitude flutter.
  static const TapeDegradationPreset kMellotron{
    /* wowRateHz            */ 1.1f,
    /* wowDepthCents        */ 25.0f,
    /* wowLocked            */ false,
    /* wowLockedRateHz      */ 0.5556f,
    /* flutterRateHz        */ 8.5f,
    /* flutterDepthCents    */ 7.0f,
    /* healthRateHz         */ 0.3f,
    /* healthSensitivity    */ 0.6f,
    /* hissLevelDB          */ -40.0f,
    /* dropoutRateHz        */ 0.1f,
    /* dropoutDepthDB       */ -8.0f,
    /* dropoutDurationMs    */ 12.0f,
    /* clickRateHz          */ 0.2f,
    /* clickGainDB          */ -20.0f,
    /* saturationDriveDB    */ 2.0f,
    /* lowCutHz             */ 200.0f,
    /* hfRolloffHz          */ 5000.0f,
    /* headBumpHz           */ 100.0f,
    /* headBumpGainDB       */ 2.0f,
    /* mix                  */ 1.0f,
    /* ampFlutterDepth      */ 0.15f,
    /* swoopStartCents      */ 60.0f,
    /* swoopTimeMs          */ 180.0f,
    /* spinDownMs           */ 120.0f,
    /* droopDepthCents      */ 15.0f,
    /* rumbleLevelDB        */ -100.0f,
    /* rumbleHz             */ 80.0f,
    /* decayMode            */ false,
    /* decayRatePerMinute   */ 0.0f,
    /* breathingAmount      */ 0.0f,
    /* hissLevelDependent   */ 0.0f,
    /* breathingAttackMs    */ 5.0f,
    /* breathingReleaseMs   */ 80.0f,
  };

  // 15/30 ips studio machine - near-clean; every fault component is
  // present but turned down close to inaudible, kept mainly for the
  // saturation/head-bump warmth a real studio deck still imparts even
  // when running well.
  static const TapeDegradationPreset kStudio{
    /* wowRateHz            */ 0.5f,
    /* wowDepthCents        */ 1.5f,
    /* wowLocked            */ false,
    /* wowLockedRateHz      */ 0.5556f,
    /* flutterRateHz        */ 10.0f,
    /* flutterDepthCents    */ 0.5f,
    /* healthRateHz         */ 0.15f,
    /* healthSensitivity    */ 0.15f,
    /* hissLevelDB          */ -60.0f,
    /* dropoutRateHz        */ 0.02f,
    /* dropoutDepthDB       */ -6.0f,
    /* dropoutDurationMs    */ 10.0f,
    /* clickRateHz          */ 0.02f,
    /* clickGainDB          */ -24.0f,
    /* saturationDriveDB    */ 2.0f,
    /* lowCutHz             */ 20.0f,
    /* hfRolloffHz          */ 16000.0f,
    /* headBumpHz           */ 60.0f,
    /* headBumpGainDB       */ 1.5f,
    /* mix                  */ 1.0f,
    /* ampFlutterDepth      */ 0.0f,
    /* swoopStartCents      */ 0.0f,
    /* swoopTimeMs          */ 150.0f,
    /* spinDownMs           */ 200.0f,
    /* droopDepthCents      */ 1.0f,
    /* rumbleLevelDB        */ -100.0f,
    /* rumbleHz             */ 80.0f,
    /* decayMode            */ false,
    /* decayRatePerMinute   */ 0.0f,
    /* breathingAmount      */ 0.0f,
    /* hissLevelDependent   */ 0.0f,
    /* breathingAttackMs    */ 5.0f,
    /* breathingReleaseMs   */ 80.0f,
  };

  // Narrow band + Dolby-style HF breathing tied to input level -
  // breathingAmount is the one field that actually defines this preset;
  // everything else is a fairly ordinary consumer-cassette character.
  static const TapeDegradationPreset kCassette{
    /* wowRateHz            */ 1.0f,
    /* wowDepthCents        */ 12.0f,
    /* wowLocked            */ false,
    /* wowLockedRateHz      */ 0.5556f,
    /* flutterRateHz        */ 9.0f,
    /* flutterDepthCents    */ 5.0f,
    /* healthRateHz         */ 0.25f,
    /* healthSensitivity    */ 0.4f,
    /* hissLevelDB          */ -38.0f,
    /* dropoutRateHz        */ 0.08f,
    /* dropoutDepthDB       */ -10.0f,
    /* dropoutDurationMs    */ 12.0f,
    /* clickRateHz          */ 0.1f,
    /* clickGainDB          */ -20.0f,
    /* saturationDriveDB    */ 2.5f,
    /* lowCutHz             */ 100.0f,
    /* hfRolloffHz          */ 8000.0f,
    /* headBumpHz           */ 90.0f,
    /* headBumpGainDB       */ 2.0f,
    /* mix                  */ 1.0f,
    /* ampFlutterDepth      */ 0.0f,
    /* swoopStartCents      */ 0.0f,
    /* swoopTimeMs          */ 150.0f,
    /* spinDownMs           */ 200.0f,
    /* droopDepthCents      */ 5.0f,
    /* rumbleLevelDB        */ -100.0f,
    /* rumbleHz             */ 80.0f,
    /* decayMode            */ false,
    /* decayRatePerMinute   */ 0.0f,
    /* breathingAmount      */ 0.6f,
    /* hissLevelDependent   */ 0.0f,
    /* breathingAttackMs    */ 8.0f,
    /* breathingReleaseMs   */ 120.0f,
  };

  // Rate-locked periodic wow at 33 1/3rpm (0.5556Hz) rather than random,
  // a dense click field, and low rumble - wowLocked/rumbleLevelDB/
  // clickRateHz are what actually define this preset.
  static const TapeDegradationPreset kVinyl{
    /* wowRateHz            */ 0.5556f,
    /* wowDepthCents        */ 15.0f,
    /* wowLocked            */ true,
    /* wowLockedRateHz      */ 0.5556f,
    /* flutterRateHz        */ 6.0f,
    /* flutterDepthCents    */ 2.0f,
    /* healthRateHz         */ 0.2f,
    /* healthSensitivity    */ 0.35f,
    /* hissLevelDB          */ -55.0f,
    /* dropoutRateHz        */ 0.05f,
    /* dropoutDepthDB       */ -8.0f,
    /* dropoutDurationMs    */ 8.0f,
    /* clickRateHz          */ 3.0f,
    /* clickGainDB          */ -16.0f,
    /* saturationDriveDB    */ 1.5f,
    /* lowCutHz             */ 20.0f,
    /* hfRolloffHz          */ 14000.0f,
    /* headBumpHz           */ 50.0f,
    /* headBumpGainDB       */ 1.0f,
    /* mix                  */ 1.0f,
    /* ampFlutterDepth      */ 0.0f,
    /* swoopStartCents      */ 0.0f,
    /* swoopTimeMs          */ 150.0f,
    /* spinDownMs           */ 250.0f,
    /* droopDepthCents      */ 8.0f,
    /* rumbleLevelDB        */ -30.0f,
    /* rumbleHz             */ 50.0f,
    /* decayMode            */ false,
    /* decayRatePerMinute   */ 0.0f,
    /* breathingAmount      */ 0.0f,
    /* hissLevelDependent   */ 0.0f,
    /* breathingAttackMs    */ 5.0f,
    /* breathingReleaseMs   */ 80.0f,
  };

  // Health becomes a monotonic one-way decay (see dsp/TapeTransport.cpp's
  // decayMode handling) - decayRatePerMinute is what actually defines this
  // preset; everything else starts close to the plain "tape" baseline so
  // the erosion is what the ear tracks over the course of playback, not a
  // preset that already sounds degraded from the first note.
  static const TapeDegradationPreset kDisintegration{
    /* wowRateHz            */ 0.7f,
    /* wowDepthCents        */ 8.0f,
    /* wowLocked            */ false,
    /* wowLockedRateHz      */ 0.5556f,
    /* flutterRateHz        */ 8.0f,
    /* flutterDepthCents    */ 3.0f,
    /* healthRateHz         */ 0.2f,
    /* healthSensitivity    */ 0.7f,
    /* hissLevelDB          */ -48.0f,
    /* dropoutRateHz        */ 0.15f,
    /* dropoutDepthDB       */ -12.0f,
    /* dropoutDurationMs    */ 15.0f,
    /* clickRateHz          */ 0.3f,
    /* clickGainDB          */ -18.0f,
    /* saturationDriveDB    */ 3.0f,
    /* lowCutHz             */ 20.0f,
    /* hfRolloffHz          */ 12000.0f,
    /* headBumpHz           */ 80.0f,
    /* headBumpGainDB       */ 3.0f,
    /* mix                  */ 1.0f,
    /* ampFlutterDepth      */ 0.0f,
    /* swoopStartCents      */ 0.0f,
    /* swoopTimeMs          */ 150.0f,
    /* spinDownMs           */ 350.0f,
    /* droopDepthCents      */ 6.0f,
    /* rumbleLevelDB        */ -100.0f,
    /* rumbleHz             */ 80.0f,
    /* decayMode            */ true,
    /* decayRatePerMinute   */ 0.4f,
    /* breathingAmount      */ 0.0f,
    /* hissLevelDependent   */ 0.0f,
    /* breathingAttackMs    */ 5.0f,
    /* breathingReleaseMs   */ 80.0f,
  };

  // Narrow band + more hiss + more flutter, taken to a dictation
  // machine's extreme - a mild amount of breathingAmount too (cheap
  // dictaphones commonly had rudimentary automatic level control, which
  // reads similarly to Cassette's Dolby breathing).
  static const TapeDegradationPreset kDictaphone{
    /* wowRateHz            */ 1.2f,
    /* wowDepthCents        */ 15.0f,
    /* wowLocked            */ false,
    /* wowLockedRateHz      */ 0.5556f,
    /* flutterRateHz        */ 11.0f,
    /* flutterDepthCents    */ 10.0f,
    /* healthRateHz         */ 0.3f,
    /* healthSensitivity    */ 0.6f,
    /* hissLevelDB          */ -32.0f,
    /* dropoutRateHz        */ 0.2f,
    /* dropoutDepthDB       */ -10.0f,
    /* dropoutDurationMs    */ 20.0f,
    /* clickRateHz          */ 0.4f,
    /* clickGainDB          */ -16.0f,
    /* saturationDriveDB    */ 4.0f,
    /* lowCutHz             */ 300.0f,
    /* hfRolloffHz          */ 3500.0f,
    /* headBumpHz           */ 150.0f,
    /* headBumpGainDB       */ 2.0f,
    /* mix                  */ 1.0f,
    /* ampFlutterDepth      */ 0.0f,
    /* swoopStartCents      */ 0.0f,
    /* swoopTimeMs          */ 150.0f,
    /* spinDownMs           */ 150.0f,
    /* droopDepthCents      */ 10.0f,
    /* rumbleLevelDB        */ -100.0f,
    /* rumbleHz             */ 80.0f,
    /* decayMode            */ false,
    /* decayRatePerMinute   */ 0.0f,
    /* breathingAmount      */ 0.3f,
    /* hissLevelDependent   */ 0.0f,
    /* breathingAttackMs    */ 6.0f,
    /* breathingReleaseMs   */ 100.0f,
  };

  // Level-dependent grain noise - hissLevelDependent is what actually
  // defines this preset (grain in optical soundtrack film genuinely
  // tracks how much light/signal is present, unlike magnetic tape hiss,
  // which doesn't); wow/flutter here models sprocket-hole speed
  // irregularity rather than a tape transport's.
  static const TapeDegradationPreset kOpticalFilm{
    /* wowRateHz            */ 0.6f,
    /* wowDepthCents        */ 6.0f,
    /* wowLocked            */ false,
    /* wowLockedRateHz      */ 0.5556f,
    /* flutterRateHz        */ 12.0f,
    /* flutterDepthCents    */ 4.0f,
    /* healthRateHz         */ 0.2f,
    /* healthSensitivity    */ 0.4f,
    /* hissLevelDB          */ -50.0f,
    /* dropoutRateHz        */ 0.05f,
    /* dropoutDepthDB       */ -6.0f,
    /* dropoutDurationMs    */ 8.0f,
    /* clickRateHz          */ 0.15f,
    /* clickGainDB          */ -22.0f,
    /* saturationDriveDB    */ 2.0f,
    /* lowCutHz             */ 60.0f,
    /* hfRolloffHz          */ 10000.0f,
    /* headBumpHz           */ 70.0f,
    /* headBumpGainDB       */ 1.5f,
    /* mix                  */ 1.0f,
    /* ampFlutterDepth      */ 0.0f,
    /* swoopStartCents      */ 0.0f,
    /* swoopTimeMs          */ 150.0f,
    /* spinDownMs           */ 180.0f,
    /* droopDepthCents      */ 3.0f,
    /* rumbleLevelDB        */ -100.0f,
    /* rumbleHz             */ 80.0f,
    /* decayMode            */ false,
    /* decayRatePerMinute   */ 0.0f,
    /* breathingAmount      */ 0.0f,
    /* hissLevelDependent   */ 0.8f,
    /* breathingAttackMs    */ 3.0f,
    /* breathingReleaseMs   */ 60.0f,
  };

  if (name == "mellotron") return kMellotron;
  if (name == "studio") return kStudio;
  if (name == "cassette") return kCassette;
  if (name == "vinyl") return kVinyl;
  if (name == "disintegration") return kDisintegration;
  if (name == "dictaphone") return kDictaphone;
  if (name == "opticalFilm") return kOpticalFilm;
  return kTape;
}

#endif
