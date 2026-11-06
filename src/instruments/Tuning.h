#ifndef _TUNING_H_
#define _TUNING_H_

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>

enum class Tuning {
  PERCUSSION = 0,
  EDO12,
  EDO19,
  EDO31,
  EDO53
};

static inline std::string to_string(Tuning tuning) {
  switch (tuning) {
  case Tuning::PERCUSSION: return "percussion";
  case Tuning::EDO12: return "12edo";
  case Tuning::EDO19: return "19edo";
  case Tuning::EDO31: return "31edo";
  case Tuning::EDO53: return "53edo";
  default: return "";
  }
}

// Steps per octave - 0 for PERCUSSION, which has no interval structure at
// all (a percussion "note" is a fixed GM identity, never a scale degree).
// The single shared definition - LaunchpadLayout::edoSteps() (the pitched
// isomorphic grid's own layout math) and Song::getScaleDegreesWindow() (the step
// sequencer's own scale-to-pitch-class resolution) both need this and
// must never drift apart.
static inline int edoStepsFor(Tuning tuning) {
  switch (tuning) {
  case Tuning::EDO12: return 12;
  case Tuning::EDO19: return 19;
  case Tuning::EDO31: return 31;
  case Tuning::EDO53: return 53;
  case Tuning::PERCUSSION: return 0;
  }
  return 0;
}

// The nominal standard pitch: the frequency every note is computed against
// (getFrequencyFor()), and the A4 of MIDI key 69 wherever a frequency is
// matched to a MIDI key (SoundFont region choice, key tracking). The song's
// own standard pitch is applied on top of it as a ratio, at the voices
// (RenderContext::globalPitchCents()), so the MIDI mapping never moves.
inline constexpr float kNominalStandardPitchHz = 440.0f;

// The note the standard pitch is the frequency of (A4) in each tuning.
// PERCUSSION reuses EDO12's (a percussion note's frequency, when one is
// needed at all, is MIDI's).
constexpr int referenceNoteFor(Tuning tuning) {
  switch (tuning) {
    case Tuning::EDO12:
    case Tuning::PERCUSSION:
      return 69;
    case Tuning::EDO19:
      return 109;
    case Tuning::EDO31:
      return 178;
    case Tuning::EDO53:
      return 304;
  }
  return 69;
}

// The one note-to-frequency formula, at the nominal standard pitch:
// kNominalStandardPitchHz * 2^((note - reference) / steps). Each tuning's
// divisor is a literal so the arithmetic (a reciprocal multiply under
// -ffast-math) stays exactly what renders have always used.
inline float getFrequencyFor(Tuning tuning, int note_value) {
  auto from = [&](Tuning t) { return note_value - referenceNoteFor(t); };
  switch (tuning) {
  case Tuning::EDO12:
  case Tuning::PERCUSSION:
    return kNominalStandardPitchHz * powf(2.0f, from(Tuning::EDO12) / 12.0f);
  case Tuning::EDO19:
    return kNominalStandardPitchHz * powf(2.0f, from(Tuning::EDO19) / 19.0f);
  case Tuning::EDO31:
    return kNominalStandardPitchHz * powf(2.0f, from(Tuning::EDO31) / 31.0f);
  case Tuning::EDO53:
    return kNominalStandardPitchHz * powf(2.0f, from(Tuning::EDO53) / 53.0f);
  }
  return 0.0f;
}

// Neither infinite nor NaN, read from the bits: the build's -ffast-math lets
// the compiler assume std::isfinite() is always true.
inline bool finiteValue(double x) {
  return (std::bit_cast<uint64_t>(x) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
// A usable standard pitch (Hz): positive and finite.
inline bool validStandardPitch(float hz) { return finiteValue(hz) && hz > 0.0f; }

// A standard pitch as cents from the nominal one (0 at 440 Hz).
inline double standardPitchCents(float standard_pitch_hz) {
  if (!validStandardPitch(standard_pitch_hz)) return 0.0;
  return 1200.0 * std::log2(static_cast<double>(standard_pitch_hz) / static_cast<double>(kNominalStandardPitchHz));
}

// The const Note & overload lives in Note.h, not here - Note.h already
// includes this header, so declaring it here too would make the two
// headers include each other.

#endif
